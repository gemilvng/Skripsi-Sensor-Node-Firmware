// sensor.cpp

#include "sensor.h"

#include <Arduino.h>  // temperatureRead()
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "tasks.h"

static const char* TAG = "Sensor";

// temperatureRead() returns the ESP32 internal die temperature in degC. On the
// original ESP32 target (CONFIG_IDF_TARGET_ESP32) the Arduino core defines it as
// (temprature_sens_read() - 32) / 1.8 in esp32-hal-misc.c; it needs no init and
// cannot fail. NOTE: this is deliberately *not* the ESP-IDF temperature_sensor
// driver named in dev-resource/packet-structure.md, which is compiled only for
// the S2/S3/C-series targets and does not link for ESP32-D0WDQ6-V3.

bool init_sensor() {
    float first_temp = temperatureRead();
    ESP_LOGI(TAG, "sensor_init_ok first_temp_c=%.2f", first_temp);
    return true;
}

// Single periodic trigger (Architecture.md §1): vTaskDelayUntil gives a
// drift-free cadence. The work per tick is one register read plus one log line,
// so the task is runnable for microseconds per interval and never blocks the
// mesh tasks it shares priority with.
void sensor_sample_task(void* /*pvParameters*/) {
    static uint32_t sensor_sample_count = 0;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS);

    for (;;) {
        float temp_c = temperatureRead();
        sensor_sample_count++;
        ESP_LOGI(TAG, "sensor_sample temp_c=%.2f count=%u uptime_ms=%llu",
                 temp_c, sensor_sample_count,
                 static_cast<unsigned long long>(esp_timer_get_time() / 1000));

        vTaskDelayUntil(&last_wake, period);
    }
}
