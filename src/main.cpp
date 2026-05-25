// main.cpp

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/uart.h>

static const char* TAG = "Main";

void setup() {
    // Serial initialization verification
    uint32_t baud = 0;
    if (uart_get_baudrate(UART_NUM_0, &baud) == ESP_OK) {
        ESP_LOGI(TAG, "UART0 running at %u baud", baud);
    }

    ESP_LOGI(TAG, "Logging from setup function.");
}

void loop() {
    static uint32_t uptime_tick_count = 0;
    uptime_tick_count++;
    ESP_LOGI(TAG, "uptime_tick count=%u", uptime_tick_count);
    vTaskDelay(1000/portTICK_PERIOD_MS);
}
