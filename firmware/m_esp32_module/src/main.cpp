
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#define LDR_CHANNEL    ADC_CHANNEL_7  // GPIO35
#define ADC_SAMPLES    16
#define READ_INTERVAL  1000           // ms

static const char *TAG = "LDR";

extern "C" void app_main(void)
{
    // 1. Initializare ADC1
    adc_oneshot_unit_handle_t adc_handle = nullptr;

    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;

    ESP_ERROR_CHECK(adc_oneshot_new_unit(
        &unit_cfg,
        &adc_handle
    ));

    // 2. Configurare GPIO35 (ADC1_CH7)
    adc_oneshot_chan_cfg_t channel_cfg = {};
    channel_cfg.atten = ADC_ATTEN_DB_12;
    channel_cfg.bitwidth = ADC_BITWIDTH_12;

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle,
        LDR_CHANNEL,
        &channel_cfg
    ));

    ESP_LOGI(TAG, "LDR initialized on GPIO35");

    // 3. Citire periodica
    while (true)
    {
        int32_t sum = 0;

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

        int adc_value = sum / ADC_SAMPLES;

        ESP_LOGI(TAG, "LDR ADC: %d / 4095",
                 adc_value);

        vTaskDelay(pdMS_TO_TICKS(READ_INTERVAL));
    }
}
