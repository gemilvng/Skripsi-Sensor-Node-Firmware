// pmu.cpp

#include "pmu.h"

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <esp_log.h>

static const char* TAG = "PMU";

static XPowersPMU pmu;

bool init_pmu() {
    bool init_ok = pmu.init();
    if (!init_ok) {
        ESP_LOGE(TAG, "pmu_init_failed");
        return false;
    }
    ESP_LOGI(TAG, "pmu_init_ok");

    // DCDC1 powers the ESP32 SoC and is brought up by board hardware.
    // Lock it against accidental disable from later code or library bugs.
    pmu.setProtectedChannel(XPOWERS_DCDC1);
    bool dcdc1_on = pmu.isEnableDC1();
    uint16_t dcdc1_mv = pmu.getDC1Voltage();
    ESP_LOGI(TAG, "dcdc1_protected on=%d voltage_mv=%u", dcdc1_on, dcdc1_mv);

    // ALDO2 powers the SX1276 LoRa radio. Required on for Phase 3 onward.
    pmu.setALDO2Voltage(3300);
    pmu.enableALDO2();
    bool aldo2_on = pmu.isEnableALDO2();
    uint16_t aldo2_mv = pmu.getALDO2Voltage();
    ESP_LOGI(TAG, "aldo2_lora_enabled on=%d voltage_mv=%u", aldo2_on, aldo2_mv);

    // ALDO3 powers the NEO-6M GPS. GPS is out of scope for this firmware,
    // so the rail and its coin-cell backup charger are explicitly off.
    pmu.disableALDO3();
    pmu.disableButtonBatteryCharge();
    bool aldo3_on = pmu.isEnableALDO3();
    ESP_LOGI(TAG, "aldo3_gps_disabled on=%d", aldo3_on);

    // Every other rail has no consumer on this board; disable to avoid
    // sourcing into nothing and to make rail policy explicit.
    pmu.disableDC2();
    pmu.disableDC3();
    pmu.disableDC4();
    pmu.disableDC5();
    pmu.disableALDO1();
    pmu.disableALDO4();
    pmu.disableBLDO1();
    pmu.disableBLDO2();
    pmu.disableDLDO1();
    pmu.disableDLDO2();
    ESP_LOGI(TAG, "unused_rails_disabled");

    return true;
}
