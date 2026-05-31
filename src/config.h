// config.h
#pragma once

#include <array>
#include <cstdint>

#include "types/configurations/radio_configuration.hpp"

// LoRa radio pins for TTGO T-Beam V1.2 (SX1276).
// Names are prefixed LORA_PIN_* to avoid colliding with the LORA_*
// macros defined by the ESP32 Arduino Core T-Beam board variant.
constexpr int8_t LORA_PIN_CS = 18;
constexpr int8_t LORA_PIN_RST = 23;
constexpr int8_t LORA_PIN_IRQ = 26;
constexpr int8_t LORA_PIN_IO1 = 33;
constexpr int8_t LORA_PIN_SCK = 5;
constexpr int8_t LORA_PIN_MISO = 19;
constexpr int8_t LORA_PIN_MOSI = 27;

// LoRa radio parameters. Frequency targets AS923 for Indonesia;
// re-verify against current Kemenkominfo rules before outdoor deployment.
constexpr loramesher::RadioType LORA_RADIO_TYPE = loramesher::RadioType::kSx1276;
constexpr float LORA_FREQUENCY_MHZ = 921.200F;
constexpr uint8_t LORA_SPREADING_FACTOR = 7;
constexpr float LORA_BANDWIDTH_KHZ = 125.0F;
constexpr uint8_t LORA_CODING_RATE = 7;
constexpr int8_t LORA_POWER_DBM = 6;
constexpr uint8_t LORA_SYNC_WORD = 20;
constexpr bool LORA_CRC_ENABLED = true;
constexpr uint16_t LORA_PREAMBLE_LENGTH = 8;

// LoRaMesh protocol parameters. Set explicitly because
// LoRaMeshProtocolConfig's constructor default for max_hops (5) overrides
// its in-class initializer (10); setMaxHops() is the documented mitigation.
constexpr uint8_t MESH_MAX_HOPS = 5;

// Mesh-formation role is fixed by address match (not the library's default
// AUTO election, which forms networks probabilistically across staggered
// boots). The node whose hardware-derived address equals this becomes
// LoRaMesher NETWORK_MANAGER; every other node is NODE_ONLY. Interim Phase 3
// arrangement; at Phase 7 the manager follows the Wi-Fi/sink decision.
// (See Architecture.md §2, Implementation-Record.md Phase 3.)
constexpr uint16_t MESH_MANAGER_ADDRESS = 0x2510;

// Receive path. The data callback copies each payload into a fixed-size POD
// (MeshRxItem in queues.h) and hands it to mesh_rx_task over rx_queue.
// MESH_MAX_PAYLOAD_BYTES sits well above the ~232-byte single-frame usable
// max at SF7/BW125 cap for our needs while staying small; RX_QUEUE_DEPTH is
// how many items buffer before the callback drops (silently, never blocks).
constexpr uint8_t MESH_MAX_PAYLOAD_BYTES = 64;
constexpr uint8_t RX_QUEUE_DEPTH = 8;

// Interval at which mesh_rx_task emits its mesh_rx_count line. Doubles as the
// task's queue-read timeout so the counter is reported even with no traffic.
constexpr uint32_t MESH_RX_REPORT_INTERVAL_MS = 30000;

// Transmit path. mesh_tx_task aligns each send to the next data slot via
// GetTimeUntilNextDataSlot(); when that returns 0 (not yet joined / no slot)
// it falls back to this fixed delay before retrying.
constexpr uint32_t MESH_TX_FALLBACK_DELAY_MS = 10000;

// Fixed Phase-3 test payload, sent each data slot. Spells "PING"; replaced by
// an encoded sensor reading at Phase 6.
constexpr std::array<uint8_t, 4> MESH_HELLO_PAYLOAD = {0x50, 0x49, 0x4E, 0x47};

// App task scheduling. Priority 2 sits below every LoRaMesher internal task
// (3/14/15) so the mesh always preempts our housekeeping, and above the
// Arduino loopTask (1). One shared pair: both app tasks use the same values.
constexpr uint32_t MESH_APP_TASK_STACK = 4096;
constexpr uint8_t MESH_APP_TASK_PRIORITY = 2;
