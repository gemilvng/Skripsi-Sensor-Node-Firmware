// pmu.h

#pragma once

// Initializes the AXP2101 PMU over I2C and configures power rails for
// the TTGO T-Beam V1.2:
//   - DCDC1 (ESP32 SoC):       observed and locked via setProtectedChannel
//   - ALDO2 (SX1276 LoRa):     enabled at 3300 mV
//   - ALDO3 (NEO-6M GPS):      disabled (GPS not used in this firmware)
//   - VBACKUP (GPS coin cell): disabled (coupled to ALDO3)
//   - All other rails:         explicitly disabled
//
// Returns true iff the AXP2101 responded on I2C (the chip's init() succeeded).
// Does NOT verify that individual rail writes took effect — XPowersLib
// configuration setters return void and offer no per-call status.
//
// Caller may treat a true return as: "an AXP2101 is present and addressable."
// Caller MUST NOT treat a true return as proof that any specific rail is at
// its intended voltage; if such proof matters later, perform a follow-up read.
//
// Pre-conditions:
//   - Wire (I2C bus 0) has been initialized successfully by the caller.
bool init_pmu();
