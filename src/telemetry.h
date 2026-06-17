// telemetry.h
#pragma once

#include <cstddef>
#include <cstdint>

// Combined telemetry + routing-table payload
// (dev-resource/routing-table-reporting.md §4). It rides inside the LoRaMesher
// DATA body sent to the network manager. The source address is delivered
// separately as the receive callback's `source` argument, so it is not repeated
// in the payload.
//
// Packed, little-endian:
//   offset 0  seq          uint16  per-node send counter (low 16 bits)
//   offset 2  timestamp    uint32  ms since node boot, captured at send time
//   offset 6  chip_temp    int8    ESP32 die temperature in degC, rounded
//   offset 7  entry_count  uint8   number of routing entries that follow
//   offset 8  entries[]    6 bytes each:
//               +0 destination  uint16
//               +2 next_hop     uint16
//               +4 hop_count    uint8
//               +5 flags        uint8  (bit0 valid, bit1 is_network_manager)
//
// Byte order is little-endian and MUST be matched exactly by the sink parser;
// the pack/parse functions below are the single definition of that order.

constexpr size_t TELEMETRY_HEADER_BYTES = 8;
constexpr size_t TELEMETRY_ROUTE_ENTRY_BYTES = 6;

// Largest routing-table that still keeps the whole DATA frame within
// MESH_MAX_PACKET_SIZE (64): 64 - 10 (DATA header) = 54 payload bytes;
// (54 - 8) / 6 = 7 entries.
constexpr uint8_t TELEMETRY_MAX_ROUTE_ENTRIES = 7;

// Largest combined payload, for sizing the transmit buffer.
constexpr size_t TELEMETRY_MAX_PAYLOAD_BYTES =
    TELEMETRY_HEADER_BYTES +
    TELEMETRY_ROUTE_ENTRY_BYTES * TELEMETRY_MAX_ROUTE_ENTRIES;  // 50

// Route-entry flag bits.
constexpr uint8_t TELEMETRY_ROUTE_FLAG_VALID = 0x01;
constexpr uint8_t TELEMETRY_ROUTE_FLAG_MANAGER = 0x02;

struct TelemetryHeader {
    uint16_t seq;
    uint32_t timestamp_ms;
    int8_t chip_temp_c;
    uint8_t entry_count;
};

struct TelemetryRouteEntry {
    uint16_t destination;
    uint16_t next_hop;
    uint8_t hop_count;
    uint8_t flags;
};

// --- Transmit: build the payload ---

// Write the 8-byte header at `out` (must have >= TELEMETRY_HEADER_BYTES room).
void telemetry_pack_header(const TelemetryHeader& hdr, uint8_t* out);

// Write one 6-byte route entry at `out` (must have >= TELEMETRY_ROUTE_ENTRY_BYTES
// room). The caller positions `out` at the entry's slot in the payload buffer.
void telemetry_pack_route_entry(const TelemetryRouteEntry& entry, uint8_t* out);

// --- Receive: parse in place (zero-copy) ---

// Validate and read the header from a received payload of length `len`. Returns
// false (leaving `out` untouched) unless `len` is at least the header size, the
// entry count is within TELEMETRY_MAX_ROUTE_ENTRIES, and `len` is exactly
// TELEMETRY_HEADER_BYTES + entry_count * TELEMETRY_ROUTE_ENTRY_BYTES — so a
// truncated or non-telemetry frame is rejected rather than misread.
bool telemetry_parse_header(const uint8_t* in, size_t len, TelemetryHeader& out);

// Read the route entry at `index` directly from the payload `in`. The caller
// must have validated the header first and pass index < entry_count; this does
// no bounds check so the receive loop stays branch-free.
void telemetry_parse_route_entry(const uint8_t* in, uint8_t index,
                                 TelemetryRouteEntry& out);
