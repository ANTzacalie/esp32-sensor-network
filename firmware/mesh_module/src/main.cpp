
#include <cstdio>
#include <cstdint>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include <initializer_list>
// =====================================================
// CONFIGURARE PINI
// =====================================================

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         GPIO_NUM_21
#define I2C_SCL         GPIO_NUM_22
#define I2C_FREQUENCY   100000

#define PIR_PIN         GPIO_NUM_26

#define AHT20_ADDRESS   0x38
#define BMP280_ADDR_1   0x76
#define BMP280_ADDR_2   0x77

#define READ_INTERVAL   2000

static const char* TAG = "ESP32_SENSORS";

// =====================================================
// FUNCTII I2C
// =====================================================

esp_err_t i2c_write(uint8_t address,
                    const uint8_t* data,
                    size_t length)
{
    return i2c_master_write_to_device(
        I2C_PORT,
        address,
        data,
        length,
        pdMS_TO_TICKS(1000)
    );
}

esp_err_t i2c_read(uint8_t address,
                   uint8_t* data,
                   size_t length)
{
    return i2c_master_read_from_device(
        I2C_PORT,
        address,
        data,
        length,
        pdMS_TO_TICKS(1000)
    );
}

esp_err_t i2c_read_register(uint8_t address,
                            uint8_t reg,
                            uint8_t* data,
                            size_t length)
{
    return i2c_master_write_read_device(
        I2C_PORT,
        address,
        &reg,
        1,
        data,
        length,
        pdMS_TO_TICKS(1000)
    );
}

// =====================================================
// INITIALIZARE I2C
// =====================================================

void init_i2c()
{
    i2c_config_t config = {};

    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = I2C_SDA;
    config.scl_io_num = I2C_SCL;

    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;

    config.master.clk_speed = I2C_FREQUENCY;

    ESP_ERROR_CHECK(
        i2c_param_config(I2C_PORT, &config)
    );

    ESP_ERROR_CHECK(
        i2c_driver_install(
            I2C_PORT,
            config.mode,
            0,
            0,
            0
        )
    );

    ESP_LOGI(TAG, "I2C initializat");
}

// =====================================================
// SENZOR AHT20
// TEMPERATURA + UMIDITATE
// =====================================================

class AHT20
{
public:

    bool begin()
    {
        vTaskDelay(pdMS_TO_TICKS(100));

        uint8_t status = 0;

        if (i2c_read(AHT20_ADDRESS, &status, 1) != ESP_OK)
        {
            return false;
        }

        // Verifica bitul de calibrare.
        if ((status & 0x08) == 0)
        {
            uint8_t init_command[3] = {
                0xBE, 0x08, 0x00
            };

            if (i2c_write(AHT20_ADDRESS,
                          init_command, 3) != ESP_OK)
            {
                return false;
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }

        return true;
    }

    bool read(float& temperature, float& humidity)
    {
        uint8_t command[3] = {
            0xAC, 0x33, 0x00
        };

        if (i2c_write(AHT20_ADDRESS,
                      command, 3) != ESP_OK)
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(90));

        uint8_t data[7] = {};

        if (i2c_read(AHT20_ADDRESS, data, 7) != ESP_OK)
        {
            return false;
        }

        // Bitul 7 indica senzor ocupat.
        if (data[0] & 0x80)
        {
            return false;
        }

        uint32_t raw_humidity =
            ((uint32_t)data[1] << 12) |
            ((uint32_t)data[2] << 4) |
            ((uint32_t)data[3] >> 4);

        uint32_t raw_temperature =
            (((uint32_t)data[3] & 0x0F) << 16) |
            ((uint32_t)data[4] << 8) |
            (uint32_t)data[5];

        humidity =
            (raw_humidity * 100.0f) / 1048576.0f;

        temperature =
            (raw_temperature * 200.0f) / 1048576.0f
            - 50.0f;

        return true;
    }
};

// =====================================================
// SENZOR BMP280
// TEMPERATURA + PRESIUNE
// =====================================================

class BMP280
{
private:

    uint8_t address = 0;

    uint16_t dig_T1, dig_P1;
    int16_t dig_T2, dig_T3;
    int16_t dig_P2, dig_P3, dig_P4;
    int16_t dig_P5, dig_P6, dig_P7;
    int16_t dig_P8, dig_P9;

    double t_fine = 0.0;

    static uint16_t read_u16(const uint8_t* p)
    {
        return (uint16_t)p[0] |
               ((uint16_t)p[1] << 8);
    }

    static int16_t read_s16(const uint8_t* p)
    {
        return (int16_t)read_u16(p);
    }

public:

    bool begin()
    {
        uint8_t chip_id = 0;

        // Cauta BMP280 la cele doua adrese posibile.
        for (uint8_t addr : {BMP280_ADDR_1,
                             BMP280_ADDR_2})
        {
            if (i2c_read_register(
                    addr, 0xD0,
                    &chip_id, 1) == ESP_OK
                && chip_id == 0x58)
            {
                address = addr;
                break;
            }
        }

        if (address == 0)
        {
            return false;
        }

        // Citeste coeficientii de calibrare.
        uint8_t calib[24] = {};

        if (i2c_read_register(
                address, 0x88,
                calib, 24) != ESP_OK)
        {
            return false;
        }

        dig_T1 = read_u16(&calib[0]);
        dig_T2 = read_s16(&calib[2]);
        dig_T3 = read_s16(&calib[4]);

        dig_P1 = read_u16(&calib[6]);
        dig_P2 = read_s16(&calib[8]);
        dig_P3 = read_s16(&calib[10]);
        dig_P4 = read_s16(&calib[12]);
        dig_P5 = read_s16(&calib[14]);
        dig_P6 = read_s16(&calib[16]);
        dig_P7 = read_s16(&calib[18]);
        dig_P8 = read_s16(&calib[20]);
        dig_P9 = read_s16(&calib[22]);

        if (dig_P1 == 0)
        {
            return false;
        }

        // Configurare:
        // Temperatura x1
        // Presiune x4
        // Mod normal
        uint8_t ctrl_meas[2] = {
            0xF4, 0x2F
        };

        if (i2c_write(address,
                      ctrl_meas, 2) != ESP_OK)
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG,
                 "BMP280 detectat la 0x%02X",
                 address);

        return true;
    }

    bool read(float& temperature, float& pressure)
    {
        if (address == 0)
        {
            return false;
        }

        uint8_t data[6] = {};

        if (i2c_read_register(
                address, 0xF7,
                data, 6) != ESP_OK)
        {
            return false;
        }

        int32_t adc_P =
            ((int32_t)data[0] << 12) |
            ((int32_t)data[1] << 4) |
            ((int32_t)data[2] >> 4);

        int32_t adc_T =
            ((int32_t)data[3] << 12) |
            ((int32_t)data[4] << 4) |
            ((int32_t)data[5] >> 4);

        if (adc_P == 0x80000 || adc_T == 0x80000)
        {
            return false;
        }

        // Compensare temperatura.
        double var1 =
            ((double)adc_T / 16384.0 -
             (double)dig_T1 / 1024.0)
            * (double)dig_T2;

        double var2 =
            ((double)adc_T / 131072.0 -
             (double)dig_T1 / 8192.0);

        var2 = var2 * var2 * (double)dig_T3;

        t_fine = var1 + var2;

        temperature = (float)(t_fine / 5120.0);

        // Compensare presiune.
        var1 = t_fine / 2.0 - 64000.0;

        var2 = var1 * var1 *
               (double)dig_P6 / 32768.0;

        var2 += var1 * (double)dig_P5 * 2.0;

        var2 = var2 / 4.0 +
               (double)dig_P4 * 65536.0;

        var1 =
            ((double)dig_P3 * var1 * var1 /
             524288.0 +
             (double)dig_P2 * var1) /
            524288.0;

        var1 =
            (1.0 + var1 / 32768.0) *
            (double)dig_P1;

        if (var1 == 0.0)
        {
            return false;
        }

        double p = 1048576.0 - (double)adc_P;

        p = (p - var2 / 4096.0) *
            6250.0 / var1;

        var1 =
            (double)dig_P9 * p * p /
            2147483648.0;

        var2 =
            p * (double)dig_P8 / 32768.0;

        p += (var1 + var2 +
              (double)dig_P7) / 16.0;

        // Pa -> hPa
        pressure = (float)(p / 100.0);

        return true;
    }
};

// =====================================================
// SENZOR PIR SR602
// =====================================================

class PIRSensor
{
public:

    void begin()
    {
        gpio_config_t config = {};

        config.pin_bit_mask = (1ULL << PIR_PIN);
        config.mode = GPIO_MODE_INPUT;
        config.pull_up_en = GPIO_PULLUP_DISABLE;
        config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        config.intr_type = GPIO_INTR_DISABLE;

        ESP_ERROR_CHECK(gpio_config(&config));

        ESP_LOGI(TAG, "PIR initializat");
    }

    bool motionDetected()
    {
        return gpio_get_level(PIR_PIN) == 1;
    }
};

// =====================================================
// OBIECTE SENZORI
// =====================================================

AHT20 aht20;
BMP280 bmp280;
PIRSensor pir;

// =====================================================
// FUNCTIA PRINCIPALA ESP-IDF
// =====================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Pornire sistem senzori ESP32");

    init_i2c();
    pir.begin();

    bool aht_ok = aht20.begin();
    bool bmp_ok = bmp280.begin();

    if (aht_ok)
    {
        ESP_LOGI(TAG, "AHT20 initializat OK");
    }
    else
    {
        ESP_LOGE(TAG, "AHT20 nu a fost detectat");
    }

    if (bmp_ok)
    {
        ESP_LOGI(TAG, "BMP280 initializat OK");
    }
    else
    {
        ESP_LOGE(TAG, "BMP280 nu a fost detectat");
    }

    // Permite stabilizarea initiala a senzorului PIR.
    vTaskDelay(pdMS_TO_TICKS(3000));

    bool last_motion = pir.motionDetected();

    TickType_t last_read = xTaskGetTickCount();

    while (true)
    {
        bool motion = pir.motionDetected();

        // Detectare modificare stare PIR.
        if (motion != last_motion)
        {
            if (motion)
            {
                ESP_LOGW(TAG, "MISCARE DETECTATA!");
            }
            else
            {
                ESP_LOGI(TAG, "Miscarea s-a oprit");
            }

            last_motion = motion;
        }

        TickType_t now = xTaskGetTickCount();

        if ((now - last_read) >=
            pdMS_TO_TICKS(READ_INTERVAL))
        {
            last_read = now;

            float aht_temp = 0.0f;
            float humidity = 0.0f;

            float bmp_temp = 0.0f;
            float pressure = 0.0f;

            printf("\n");
            printf("================================\n");
            printf("       ESP32 SENSOR DATA\n");
            printf("================================\n");

            // AHT20
            if (aht_ok &&
                aht20.read(aht_temp, humidity))
            {
                printf("AHT20:\n");
                printf("  Temperatura: %.2f C\n",
                       aht_temp);
                printf("  Umiditate:   %.2f %%\n",
                       humidity);
            }
            else
            {
                printf("AHT20: EROARE CITIRE\n");
            }

            // BMP280
            if (bmp_ok &&
                bmp280.read(bmp_temp, pressure))
            {
                printf("BMP280:\n");
                printf("  Temperatura: %.2f C\n",
                       bmp_temp);
                printf("  Presiune:    %.2f hPa\n",
                       pressure);
            }
            else
            {
                printf("BMP280: EROARE CITIRE\n");
            }

            // SR602
            printf("SR602 PIR:\n");
            printf("  Miscare: %s\n",
                   motion ? "DA" : "NU");

            printf("================================\n");
            fflush(stdout);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
