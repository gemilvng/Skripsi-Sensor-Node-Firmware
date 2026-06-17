// config.h
#pragma once

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
constexpr int8_t LORA_POWER_DBM = 17;
constexpr uint8_t LORA_SYNC_WORD = 20;
constexpr bool LORA_CRC_ENABLED = true;
constexpr uint16_t LORA_PREAMBLE_LENGTH = 8;

// LoRaMesh protocol parameters. Set explicitly because
// LoRaMeshProtocolConfig's constructor default for max_hops (5) overrides
// its in-class initializer (10); setMaxHops() is the documented mitigation.
constexpr uint8_t MESH_MAX_HOPS = 5;

// On-air frame size cap (header + payload), passed to setMaxPacketSize(). The
// library otherwise defaults this to the SF7/BW125 max of 242 B and sizes both
// the TDMA slot and the duty-cycle padding for that worst case, which inflates
// the superframe to ~65 s. Our largest real frame (a routing-table update for
// a few nodes) is well under 64 B, so capping here shrinks the slot and the
// superframe (~65 s -> ~33 s) while keeping the 1% duty cycle for regulation.
// Must stay above the largest frame the protocol sends; 64 B leaves margin for
// the current node count. (See dev-resource/LoRaMesher-OSI-layer.md, Finding 2.)
constexpr uint8_t MESH_MAX_PACKET_SIZE = 64;

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

// Per-node data payload, sent each data slot, is the 7-byte little-endian
// telemetry packet (seq, timestamp, chip_temp) defined in telemetry.h and built
// in mesh_tx_task; see dev-resource/packet-structure.md. 7 B payload + ~10 B
// DATA header = ~17 B on air, well under MESH_MAX_PACKET_SIZE (64). This
// replaced the Phase 3 self-identifying string "Data from 0x%04x": the source
// address that string carried is already delivered out-of-band as the receive
// callback's source argument, so the packet spends no bytes repeating it.

// App task scheduling. Priority 2 sits below every LoRaMesher internal task
// (3/14/15) so the mesh always preempts our housekeeping, and above the
// Arduino loopTask (1). One shared pair: both app tasks use the same values.
constexpr uint32_t MESH_APP_TASK_STACK = 4096;
constexpr uint8_t MESH_APP_TASK_PRIORITY = 2;

// Sensor sampling. The ESP32 internal die-temperature sensor is read on this
// cadence. 30 s matches the ~33 s mesh superframe (so the value is at most one
// send-period stale when it feeds the Phase 6 packet) and the existing
// MESH_RX_REPORT_INTERVAL_MS heartbeat. The die signal moves over tens of
// seconds and the reading is coarse (int8 / +-several degC), so faster sampling
// adds no information; >60 s would miss thermal trends.
constexpr uint32_t SENSOR_SAMPLE_INTERVAL_MS = 30000;

// sensor_sample_task is housekeeping of the same class as the mesh app tasks:
// same priority (below every LoRaMesher internal task so the mesh always
// preempts), expressed by reference so the derivation is explicit.
constexpr uint8_t SENSOR_SAMPLE_TASK_PRIORITY = MESH_APP_TASK_PRIORITY;  // = 2
constexpr uint32_t SENSOR_SAMPLE_TASK_STACK = MESH_APP_TASK_STACK;       // = 4096
