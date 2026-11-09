
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#define LDR_CHANNEL    ADC_CHANNEL_7  // GPIO35
#define ADC_SAMPLES    16
#define READ_INTERVAL  1000           // ms

#define LDR_TASK_PRIORITY  5
#define LDR_TASK_STACK     4096

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
    for (int i = 0; i < ADC_SAMPLES; i++)
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
    return sum / ADC_SAMPLES;
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

// ######################################################## //
//                      LDR-READING                         //
// ######################################################## //

// Main entry point
extern "C" void app_main(void)
{
    LDR_Init();

    xTaskCreate(LDR_Task,"LDR_Task", LDR_TASK_STACK, nullptr, LDR_TASK_PRIORITY, nullptr);

}
