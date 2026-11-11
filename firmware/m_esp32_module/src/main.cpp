
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include <cstddef>
#include <cstdlib>
#include "driver/i2c_master.h"
#include "driver/gpio.h"


/*
    LA FLASH ACESTA VARIABILA SA VA MODIFICA PENTRU NODUL CU SR602(TRUE) / CEL FARA SR602(FALSE);
*/
const constexpr uint8_t HAS_SR602 = true; 

constexpr adc_channel_t LDR_CHANNEL = ADC_CHANNEL_7; // GPIO35
constexpr int ADC_SAMPLES_LDR = 16;
constexpr uint32_t READ_INTERVAL = 3600000; // ms

const constexpr UBaseType_t LDR_TASK_PRIORITY = 5;
const constexpr uint16_t LDR_TASK_STACK = 4096;

static const char *TAG = "LDR";
static adc_oneshot_unit_handle_t adc_handle = nullptr;

// ######################################################## //
//                      LDR-READING                         //
// ######################################################## //

// Initialize ADC1 on GPIO35
void LDR_Init(void)
{
    // Configurare citire ADC si calibrare (ESP-IDF>5);
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;

    // Verifica daca adc_oneshot functioneaza
    ESP_ERROR_CHECK(adc_oneshot_new_unit(
        &unit_cfg,
        &adc_handle
    ));

    // Canalul si atenuarea;
    adc_oneshot_chan_cfg_t channel_cfg = {};
    channel_cfg.atten = ADC_ATTEN_DB_12;
    channel_cfg.bitwidth = ADC_BITWIDTH_12;

    // verifica daca canalul&atenuarea a fost configurat corect;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle,
        LDR_CHANNEL,
        &channel_cfg
    ));

    ESP_LOGI(TAG, "LDR initialized on GPIO35");
}

// Read ADC and calculate average
int LDR_Read(void)
{
    int32_t sum = 0;

    // Face 16 samples;
    for (int i = 0; i < ADC_SAMPLES_LDR; i++)
    {
        int raw = 0;

        ESP_ERROR_CHECK(adc_oneshot_read(
            adc_handle,
            LDR_CHANNEL,
            &raw
        ));

        sum += raw;
    }

    // media aritmetica, 16 citiri adunate / nr de samples;
    return sum / ADC_SAMPLES_LDR;
}

// FreeRTOS LDR task
void LDR_Task(void *pvParameters)
{
    while (true)
    {
        // bucla de citire;
        int adc_value = LDR_Read();

        // Afisaza valoarea medie;
        ESP_LOGI(TAG, "LDR ADC: %d / 4095", adc_value);

        // bucla se executa cu un delay de 1 sec intre ele;
        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL));
    }
}

// BMP280 configuration;
constexpr gpio_num_t BMP280_SCL = GPIO_NUM_21;
constexpr gpio_num_t BMP280_SDA = GPIO_NUM_22;
constexpr uint16_t BMP280_ADDR = 0x77; // IF SD0 IS AT GND, ADDR = 0x76 , IF AT 3.3V ADDR = 0x77;

constexpr int BMP280_SAMPLES = 16;
constexpr uint32_t BMP280_INTERVAL_MS = 30000;
constexpr UBaseType_t BMP280_PRIORITY = 15;
constexpr uint32_t BMP280_STACK = 4096;

// BMP280 registers;
constexpr uint8_t BMP280_REG_ID    = 0xD0;
constexpr uint8_t BMP280_REG_CTRL  = 0xF4;
constexpr uint8_t BMP280_REG_DATA  = 0xF7;
constexpr uint8_t BMP280_REG_CALIB = 0x88;

// BMP280 configuration values;
constexpr uint8_t BMP280_CHIP_ID = 0x58;
constexpr uint8_t BMP280_CTRL_FORCED_X16 = 0xB5;

constexpr uint32_t BMP280_CONVERSION_MS = 90;
constexpr uint32_t BMP280_I2C_TIMEOUT_MS = 1000;
constexpr uint32_t BMP280_I2C_FREQ_HZ = 100000;

static const char *BMP_TAG = "BMP280";

static i2c_master_bus_handle_t bmp_bus = nullptr;
static i2c_master_dev_handle_t bmp_dev = nullptr;

// Coeficienti de calibratie;
struct BMP280_Calibration {
    uint16_t T1;
    int16_t T2, T3;
    uint16_t P1;
    int16_t P2, P3, P4, P5;
    int16_t P6, P7, P8, P9;
};

static BMP280_Calibration cal = {};

// citim registri BMP280;
esp_err_t BMP280_ReadReg(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(
        bmp_dev,
        &reg,
        1,
        data,
        length,
        BMP280_I2C_TIMEOUT_MS
    );
}

// Scriem registri BMP280;
esp_err_t BMP280_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};

    return i2c_master_transmit(
        bmp_dev,
        data,
        sizeof(data),
        BMP280_I2C_TIMEOUT_MS
    );
}

// Convertim biti de calibratie;
uint16_t BMP280_U16(const uint8_t *p)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

int16_t BMP280_S16(const uint8_t *p)
{
    return static_cast<int16_t>(BMP280_U16(p));
}

// Initializare I2C si a BMP280;
void BMP280_Init(void)
{
    i2c_master_bus_config_t bus_cfg = {};

    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = BMP280_SDA;
    bus_cfg.scl_io_num = BMP280_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;

    ESP_ERROR_CHECK(i2c_new_master_bus(
        &bus_cfg, &bmp_bus
    ));

    i2c_device_config_t dev_cfg = {};

    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BMP280_ADDR;
    dev_cfg.scl_speed_hz = BMP280_I2C_FREQ_HZ;

    ESP_ERROR_CHECK(i2c_master_bus_add_device(
        bmp_bus, &dev_cfg, &bmp_dev
    ));

    // Verificam senzor;
    uint8_t id = 0;

    ESP_ERROR_CHECK(BMP280_ReadReg(
        BMP280_REG_ID, &id, 1
    ));

    if (id != BMP280_CHIP_ID) {
        ESP_LOGE(BMP_TAG,
                 "Invalid sensor ID: 0x%02X", id);
        abort();
    }

    // Citim si calibram registrele;
    uint8_t b[24] = {};

    ESP_ERROR_CHECK(BMP280_ReadReg(
        BMP280_REG_CALIB, b, sizeof(b)
    ));

    cal.T1 = BMP280_U16(&b[0]);
    cal.T2 = BMP280_S16(&b[2]);
    cal.T3 = BMP280_S16(&b[4]);

    cal.P1 = BMP280_U16(&b[6]);
    cal.P2 = BMP280_S16(&b[8]);
    cal.P3 = BMP280_S16(&b[10]);
    cal.P4 = BMP280_S16(&b[12]);
    cal.P5 = BMP280_S16(&b[14]);
    cal.P6 = BMP280_S16(&b[16]);
    cal.P7 = BMP280_S16(&b[18]);
    cal.P8 = BMP280_S16(&b[20]);
    cal.P9 = BMP280_S16(&b[22]);

    if (cal.P1 == 0) {
        ESP_LOGE(BMP_TAG, "Invalid calibration");
        abort();
    }

    ESP_LOGI(BMP_TAG, "BMP280 initialized on SDA=22 SCL=21");
}

// Read one compensated measurement;
esp_err_t BMP280_Read(float *temperature, float *pressure)
{
    if (temperature == nullptr || pressure == nullptr)
        return ESP_ERR_INVALID_ARG;

    // Temperature x16, pressure x16, in mod fortat;
    // osrs_t=101, osrs_p=101, mode=01
    esp_err_t err = BMP280_WriteReg(
        BMP280_REG_CTRL,
        BMP280_CTRL_FORCED_X16
    );

    if (err != ESP_OK)
        return err;

    // Lasam timp ca sa se termine conversia;
    vTaskDelay(pdMS_TO_TICKS(BMP280_CONVERSION_MS));

    uint8_t data[6] = {};

    err = BMP280_ReadReg(
        BMP280_REG_DATA, data, sizeof(data)
    );

    if (err != ESP_OK)
        return err;

    int32_t adc_P =
        (static_cast<int32_t>(data[0]) << 12) |
        (static_cast<int32_t>(data[1]) << 4) |
        (data[2] >> 4);

    int32_t adc_T =
        (static_cast<int32_t>(data[3]) << 12) |
        (static_cast<int32_t>(data[4]) << 4) |
        (data[5] >> 4);

    constexpr int32_t ADC_INVALID = 0x80000;

    if (adc_T == ADC_INVALID || adc_P == ADC_INVALID)
        return ESP_ERR_INVALID_RESPONSE;

    // Compensatie a temperaturi, conofrm Bosch;
    double v1 =(adc_T / 16384.0 - cal.T1 / 1024.0) * cal.T2;

    double v2 = (adc_T / 131072.0 - cal.T1 / 8192.0);

    v2 = v2 * v2 * cal.T3;

    double t_fine = v1 + v2;

    *temperature = static_cast<float>(t_fine / 5120.0);

    // Compensare de presiune (Bosch)
    v1 = t_fine / 2.0 - 64000.0;

    v2 = v1 * v1 * cal.P6 / 32768.0;
    v2 += v1 * cal.P5 * 2.0;
    v2 = v2 / 4.0 + cal.P4 * 65536.0;

    v1 = (cal.P3 * v1 * v1 / 524288.0 + cal.P2 * v1) / 524288.0;

    v1 = (1.0 + v1 / 32768.0) * cal.P1;

    if (v1 == 0.0)
        return ESP_ERR_INVALID_STATE;

    double p = 1048576.0 - adc_P;

    p = (p - v2 / 4096.0) * 6250.0 / v1;

    v1 = cal.P9 * p * p / 2147483648.0;
    v2 = p * cal.P8 / 32768.0;

    p += (v1 + v2 + cal.P7) / 16.0;

    *pressure = static_cast<float>(p / 100.0); // hPa

    return ESP_OK;
}

// Citeste de 16 ori si calculeaza mediile, 16 samples;
esp_err_t BMP280_ReadAverage(float *temperature, float *pressure)
{
    if (temperature == nullptr || pressure == nullptr)
        return ESP_ERR_INVALID_ARG;

    double sum_T = 0.0;
    double sum_P = 0.0;

    for (int i = 0; i < BMP280_SAMPLES; i++)
    {
        float t = 0.0f;
        float p = 0.0f;

        esp_err_t err = BMP280_Read(&t, &p);

        if (err != ESP_OK)
            return err;

        sum_T += t;
        sum_P += p;
    }

    *temperature = static_cast<float>(
        sum_T / BMP280_SAMPLES
    );

    *pressure = static_cast<float>(
        sum_P / BMP280_SAMPLES
    );

    return ESP_OK;
}

// FreeRTOS task, afisaza temperatura si umiditatea mediata, si raporteaza eroare daca una exista;
void BMP280_Task(void *pvParameters)
{
    (void)pvParameters;

    while (true)
    {
        float temperature = 0.0f;
        float pressure = 0.0f;

        esp_err_t err = BMP280_ReadAverage(
            &temperature, &pressure
        );

        if (err == ESP_OK)
        {
            ESP_LOGI(
                BMP_TAG,
                "Temperature: %.2f C | Pressure: %.2f hPa",
                temperature,
                pressure
            );
        }
        else {
            ESP_LOGE(
                BMP_TAG,
                "Read failed: %s",
                esp_err_to_name(err)
            );
        }

        vTaskDelay(pdMS_TO_TICKS(BMP280_INTERVAL_MS));
    }
}


// ######################################################## //
//                      SR602-READING                       //
// ######################################################## //

constexpr gpio_num_t SR602_PIN = GPIO_NUM_27;

constexpr uint32_t SR602_INTERVAL_MS = 1000;
constexpr UBaseType_t SR602_PRIORITY = 10;
constexpr uint32_t SR602_STACK = 2048;

static const char *SR602_TAG = "SR602";

// Initializare GPIO27 ca intrare digitala
void SR602_Init(void)
{
    gpio_config_t io_cfg = {};

    io_cfg.pin_bit_mask = (1ULL << SR602_PIN);
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type = GPIO_INTR_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&io_cfg));

    ESP_LOGI(SR602_TAG, "SR602 initialized on GPIO27");
}

// Citire stare digitala
int SR602_Read(void)
{
    return gpio_get_level(SR602_PIN);
}

// FreeRTOS task pentru detectare miscare
void SR602_Task(void *pvParameters)
{
    (void)pvParameters;

    int previous_state = -1;

    while (true)
    {
        int current_state = SR602_Read();

        // Afisam doar la schimbarea starii
        if (current_state != previous_state)
        {
            if (current_state == 1)
            {
                ESP_LOGI(
                    SR602_TAG,
                    "Motion detected!"
                );
            }
            else
            {
                ESP_LOGI(
                    SR602_TAG,
                    "No motion"
                );
            }

            previous_state = current_state;
        }

        vTaskDelay(pdMS_TO_TICKS(SR602_INTERVAL_MS));
    }
}


 // ######################################################## //
 //                      AHT20-READING                       //
 // ######################################################## //

constexpr uint16_t AHT20_ADDR = 0x38;
constexpr int AHT20_SAMPLES = 16;

constexpr uint32_t AHT20_INTERVAL_MS = 120000;
constexpr uint32_t AHT20_CONVERSION_MS = 85;
constexpr uint32_t AHT20_I2C_TIMEOUT_MS = 1000;

constexpr UBaseType_t AHT20_PRIORITY = 10;
constexpr uint32_t AHT20_STACK = 4096;

constexpr uint32_t AHT20_I2C_FREQ_HZ = 100000;
constexpr uint8_t AHT20_STATUS_CALIBRATED = 0x08;

static const char *AHT_TAG = "AHT20";
static i2c_master_dev_handle_t aht_dev = nullptr;

// Calcul CRC-8 pentru verificarea datelor
uint8_t AHT20_CRC8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
                crc = static_cast<uint8_t>((crc << 1) ^ 0x31);
            else
                crc = static_cast<uint8_t>(crc << 1);
        }
    }

    return crc;
}


esp_err_t AHT20_ReadStatus(uint8_t *status)
{
    if (status == nullptr)
        return ESP_ERR_INVALID_ARG;

    if (aht_dev == nullptr)
        return ESP_ERR_INVALID_STATE;

    // Citim octetul de status al AHT20
    return i2c_master_receive(
        aht_dev,
        status,
        1,
        AHT20_I2C_TIMEOUT_MS
    );
}

// Initializare AHT20 pe aceeasi magistrala cu BMP280

esp_err_t AHT20_Init(void)
{
    if (bmp_bus == nullptr)
        return ESP_ERR_INVALID_STATE;

    // Verificam daca AHT20 raspunde la 0x38
    esp_err_t err = i2c_master_probe(
        bmp_bus, AHT20_ADDR, AHT20_I2C_TIMEOUT_MS
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(AHT_TAG,
                 "AHT20 not detected at 0x38: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Adaugam AHT20 la magistrala existenta
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = AHT20_ADDR;
    dev_cfg.scl_speed_hz = AHT20_I2C_FREQ_HZ;

    err = i2c_master_bus_add_device(
        bmp_bus, &dev_cfg, &aht_dev
    );

    if (err != ESP_OK)
        return err;

    // Timp de stabilizare dupa alimentare
    vTaskDelay(pdMS_TO_TICKS(150));

    uint8_t status = 0;
    err = AHT20_ReadStatus(&status);

    if (err == ESP_OK &&
        (status & AHT20_STATUS_CALIBRATED) == 0)
    {
        const uint8_t init_cmd[3] = {
            0xBE, 0x08, 0x00
        };

        err = i2c_master_transmit(
            aht_dev, init_cmd, sizeof(init_cmd),
            AHT20_I2C_TIMEOUT_MS
        );

        if (err == ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
            err = AHT20_ReadStatus(&status);
        }
    }

    if (err == ESP_OK &&
        (status & AHT20_STATUS_CALIBRATED) == 0)
    {
        err = ESP_ERR_INVALID_STATE;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(AHT_TAG,
                 "AHT20 initialization failed: %s",
                 esp_err_to_name(err));

        i2c_master_bus_rm_device(aht_dev);
        aht_dev = nullptr;
        return err;
    }

    ESP_LOGI(AHT_TAG, "AHT20 initialized at 0x38");

    return ESP_OK;
}


// Citire individuala umiditate
esp_err_t AHT20_Read(float *humidity)
{
    if (humidity == nullptr)
        return ESP_ERR_INVALID_ARG;

    // Comanda de declansare a masurarii
    uint8_t measure_cmd[3] = {0xAC, 0x33, 0x00};

    esp_err_t err = i2c_master_transmit(
        aht_dev,
        measure_cmd,
        sizeof(measure_cmd),
        AHT20_I2C_TIMEOUT_MS
    );

    if (err != ESP_OK)
        return err;

    // Asteptam finalizarea conversiei
    vTaskDelay(pdMS_TO_TICKS(AHT20_CONVERSION_MS));

    uint8_t data[7] = {};

    err = i2c_master_receive(
        aht_dev,
        data,
        sizeof(data),
        AHT20_I2C_TIMEOUT_MS
    );

    if (err != ESP_OK)
        return err;

    // Verificam bitul BUSY
    if (data[0] & 0x80)
        return ESP_ERR_INVALID_STATE;

    // Verificam CRC-ul celor 6 octeti de date
    if (AHT20_CRC8(data, 6) != data[6])
        return ESP_ERR_INVALID_RESPONSE;

    // Extragem valoarea bruta pe 20 biti
    uint32_t raw_humidity =
        (static_cast<uint32_t>(data[1]) << 12) |
        (static_cast<uint32_t>(data[2]) << 4) |
        (static_cast<uint32_t>(data[3]) >> 4);

    // Conversie in umiditate relativa (%RH)
    *humidity = static_cast<float>(
        (raw_humidity * 100.0) / 1048576.0
    );

    return ESP_OK;
}

// Media aritmetica a 16 masuratori
esp_err_t AHT20_ReadAverage(float *humidity)
{
    if (humidity == nullptr)
        return ESP_ERR_INVALID_ARG;

    double sum_H = 0.0;

    for (int i = 0; i < AHT20_SAMPLES; i++)
    {
        float h = 0.0f;

        esp_err_t err = AHT20_Read(&h);

        if (err != ESP_OK)
            return err;

        sum_H += h;
    }

    *humidity = static_cast<float>(
        sum_H / AHT20_SAMPLES
    );

    return ESP_OK;
}

// FreeRTOS task pentru umiditate
void AHT20_Task(void *pvParameters)
{
    (void)pvParameters;

    while (true)
    {
        float humidity = 0.0f;

        esp_err_t err = AHT20_ReadAverage(&humidity);

        if (err == ESP_OK)
        {
            ESP_LOGI(
                AHT_TAG,
                "Humidity: %.2f %%RH",
                humidity
            );
        }
        else
        {
            ESP_LOGE(
                AHT_TAG,
                "Read failed: %s",
                esp_err_to_name(err)
            );
        }

        vTaskDelay(pdMS_TO_TICKS(AHT20_INTERVAL_MS));
    }
}

/*
    - Citirile senzorului BMP280 sunt la fiecare 30 secunde, !!! INITIAL !!!
    - Citirile SR602 sunt constante la o rata de 1000ms.
    - Citirile AHT20 sunt la fiecare 120 secunde, !!! INITIAL !!!
    - Citirile Fotorezistorului Typ5537 sunt constante la o rata de 1 ora.
    * PENTRU A MENTINE O FUNCTIONARE CAT MAI INDELUNGATA PE BATERIE, SENZORII AHT20 SI BMP280 VA TREBUI SA FIE PUSI IN SLEEP, dar pentru aceasta aplicatie nu este ncecesar. *
*/

// Main entry point
extern "C" void app_main(void)
{

    LDR_Init();
    BMP280_Init();

    // Se verifica daca si exista AHT20 la acest nod;
    esp_err_t check_err = AHT20_Init(); 
    
    // Se verifica daca si exista SR602 la acest nod;
    SR602_Init();  

    vTaskDelay(pdMS_TO_TICKS(3000));

    if(check_err == 0) {
         xTaskCreate(AHT20_Task,"AHT20_Task",AHT20_STACK,nullptr,AHT20_PRIORITY,nullptr);
    }

    if(HAS_SR602) {
        xTaskCreate(SR602_Task, "SR602_Task", SR602_STACK, nullptr, SR602_PRIORITY, nullptr);
    }

    xTaskCreate(LDR_Task, "LDR_Task", LDR_TASK_STACK, nullptr, LDR_TASK_PRIORITY, nullptr);
    xTaskCreate(BMP280_Task, "BMP280_Task", 4096, nullptr, BMP280_PRIORITY, nullptr);

}
