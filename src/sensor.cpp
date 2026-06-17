// sensor.cpp

#include "sensor.h"

#include <cmath>  // lroundf

#include <Arduino.h>  // temperatureRead()
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "tasks.h"

static const char* TAG = "Sensor";

// Latest sample, rounded to the int8 the telemetry packet carries. Single-byte,
// one writer (init_sensor / sensor_sample_task) and one reader (mesh_tx_task);
// a byte access is atomic on the ESP32, so no lock is needed.
static volatile int8_t s_latest_chip_temp_c = 0;

// Round a degC float to the packet's int8 field, clamping to the int8 range so a
// wild reading cannot wrap to a plausible-looking value.
static int8_t round_chip_temp(float temp_c) {
    long r = lroundf(temp_c);
    if (r > 127) {
        r = 127;
    } else if (r < -128) {
        r = -128;
    }
    return static_cast<int8_t>(r);
}

int8_t sensor_latest_chip_temp_c() {
    return s_latest_chip_temp_c;
}

// temperatureRead() returns the ESP32 internal die temperature in degC. On the
// original ESP32 target (CONFIG_IDF_TARGET_ESP32) the Arduino core defines it as
// (temprature_sens_read() - 32) / 1.8 in esp32-hal-misc.c; it needs no init and
// cannot fail. NOTE: this is deliberately *not* the ESP-IDF temperature_sensor
// driver named in dev-resource/packet-structure.md, which is compiled only for
// the S2/S3/C-series targets and does not link for ESP32-D0WDQ6-V3.

bool init_sensor() {
    float first_temp = temperatureRead();
    s_latest_chip_temp_c = round_chip_temp(first_temp);
    ESP_LOGI(TAG, "sensor_init_ok first_temp_c=%.2f chip_temp_c=%d", first_temp,
             s_latest_chip_temp_c);
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
        s_latest_chip_temp_c = round_chip_temp(temp_c);
        sensor_sample_count++;
        ESP_LOGI(TAG,
                 "sensor_sample temp_c=%.2f chip_temp_c=%d count=%u uptime_ms=%llu",
                 temp_c, s_latest_chip_temp_c, sensor_sample_count,
                 static_cast<unsigned long long>(esp_timer_get_time() / 1000));

        vTaskDelayUntil(&last_wake, period);
    }
}
