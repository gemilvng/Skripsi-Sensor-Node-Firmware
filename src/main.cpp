// main.cpp

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <Wire.h>

#include "pmu.h"
#include "mesh.h"
#include "config.h"
#include "tasks.h"

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

    // Mesh node initialization
    bool mesh_ok = init_mesh();
    if (!mesh_ok) {
        ESP_LOGE(TAG, "mesh_init_failed_in_main");
        return;
    }

    // Mesh receive/transmit tasks. All xTaskCreate calls live here in setup()
    // (Architecture.md §1); the queue they use is created inside init_mesh().
    BaseType_t rx_created =
        xTaskCreate(mesh_rx_task, "mesh_rx", MESH_APP_TASK_STACK, nullptr,
                    MESH_APP_TASK_PRIORITY, nullptr);
    if (rx_created != pdPASS) {
        ESP_LOGE(TAG, "mesh_rx_task_create_failed");
        return;
    }

    BaseType_t tx_created =
        xTaskCreate(mesh_tx_task, "mesh_tx", MESH_APP_TASK_STACK, nullptr,
                    MESH_APP_TASK_PRIORITY, nullptr);
    if (tx_created != pdPASS) {
        ESP_LOGE(TAG, "mesh_tx_task_create_failed");
        return;
    }
}

void loop() {}
