// telemetry.cpp

#include "telemetry.h"

// Explicit little-endian byte order (dev-resource/routing-table-reporting.md §4).
// Reads/writes are done by hand rather than memcpy'ing structs so the wire format
// does not depend on the compiler's struct layout/padding and stays portable to
// the separate sink parser. These helpers are the single definition of the order.

static inline void write_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static inline void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static inline uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void telemetry_pack_header(const TelemetryHeader& hdr, uint8_t* out) {
    write_u16(out + 0, hdr.seq);
    write_u32(out + 2, hdr.timestamp_ms);
    out[6] = static_cast<uint8_t>(hdr.chip_temp_c);
    out[7] = hdr.entry_count;
}

void telemetry_pack_route_entry(const TelemetryRouteEntry& entry, uint8_t* out) {
    write_u16(out + 0, entry.destination);
    write_u16(out + 2, entry.next_hop);
    out[4] = entry.hop_count;
    out[5] = entry.flags;
}

bool telemetry_parse_header(const uint8_t* in, size_t len, TelemetryHeader& out) {
    if (len < TELEMETRY_HEADER_BYTES) {
        return false;
    }
    uint8_t entry_count = in[7];
    if (entry_count > TELEMETRY_MAX_ROUTE_ENTRIES) {
        return false;
    }
    if (len != TELEMETRY_HEADER_BYTES +
                   static_cast<size_t>(entry_count) * TELEMETRY_ROUTE_ENTRY_BYTES) {
        return false;
    }
    out.seq = read_u16(in + 0);
    out.timestamp_ms = read_u32(in + 2);
    out.chip_temp_c = static_cast<int8_t>(in[6]);
    out.entry_count = entry_count;
    return true;
}

void telemetry_parse_route_entry(const uint8_t* in, uint8_t index,
                                 TelemetryRouteEntry& out) {
    const uint8_t* p = in + TELEMETRY_HEADER_BYTES +
                       static_cast<size_t>(index) * TELEMETRY_ROUTE_ENTRY_BYTES;
    out.destination = read_u16(p + 0);
    out.next_hop = read_u16(p + 2);
    out.hop_count = p[4];
    out.flags = p[5];
}
