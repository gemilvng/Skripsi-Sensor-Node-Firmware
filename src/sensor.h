// sensor.h
#pragma once

// Initialize the chip-temperature sensor source. On this board the sensor is
// the ESP32's internal die-temperature sensor, read through the Arduino core's
// temperatureRead(). The value is the uncalibrated silicon die temperature in
// degrees Celsius: useful for trends and thermal-stress detection, not for
// precise ambient temperature (+-several degC absolute, reads above ambient
// because of self-heating). It tracks temperature *changes* faithfully.
//
// On the original ESP32 target there is no hardware init to perform and
// temperatureRead() cannot report a failure, so this function only takes and
// logs one boot reading to prove the sensor responds, then returns true. The
// periodic sampler is sensor_sample_task (declared in tasks.h).
bool init_sensor();
