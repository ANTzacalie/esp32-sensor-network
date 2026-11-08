#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "ESP32_TEST";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 pornit cu succes!");

    int counter = 0;

    while (true)
    {
        ESP_LOGI(TAG, "Test ESP-IDF: %d", counter++);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}