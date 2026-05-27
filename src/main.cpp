// main.cpp

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <Wire.h>

#include "pmu.h"

static const char* TAG = "Main";

void setup() {
    // Serial initialization verification
    uint32_t baud = 0;
    if (uart_get_baudrate(UART_NUM_0, &baud) == ESP_OK) {
        ESP_LOGI(TAG, "UART0 running at %u baud", baud);
    }

    // I2C initialization
    bool i2c_ok = Wire.begin(SDA, SCL);
    if (!i2c_ok) {
        ESP_LOGE(TAG, "i2c_init_failed bus=0 sda=%d scl=%d", SDA, SCL);
        return;
    }

    // PMU initialization
    bool pmu_ok = init_pmu();
    if (!pmu_ok) {
        ESP_LOGE(TAG, "pmu_init_failed_in_main");
        return;
    }
}

void loop() {}
