// sensor.h
#pragma once

#include <cstdint>

// Initialize the chip-temperature sensor source. On this board the sensor is
// the ESP32's internal die-temperature sensor, read through the Arduino core's
// temperatureRead(). The value is the uncalibrated silicon die temperature in
// degrees Celsius: useful for trends and thermal-stress detection, not for
// precise ambient temperature (+-several degC absolute, reads above ambient
// because of self-heating). It tracks temperature *changes* faithfully.
//
// On the original ESP32 target there is no hardware init to perform and
// temperatureRead() cannot report a failure, so this function only takes and
// logs one boot reading to prove the sensor responds, then returns true. It also
// seeds the published latest value (below) so it is valid before the first
// periodic sample. The periodic sampler is sensor_sample_task (declared in
// tasks.h).
bool init_sensor();

// Latest chip-temperature sample, rounded to the int8 that the telemetry packet
// carries (dev-resource/packet-structure.md §2). sensor_sample_task refreshes it
// every SENSOR_SAMPLE_INTERVAL_MS and init_sensor() seeds it. mesh_tx_task reads
// this at send time, so the value transmitted is exactly the sample logged on
// serial. It is a single byte written by one task and read by another; a byte
// load/store is atomic on the ESP32, so no lock is needed.
int8_t sensor_latest_chip_temp_c();
