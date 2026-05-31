// queues.h
#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"

// One received mesh payload, copied by value through rx_queue. A fixed-size
// POD (no pointers, no owning containers) so the LoRaMesher receive callback
// can enqueue it without heap allocation — the callback runs in protocol
// context where allocation and blocking are forbidden (Coding-Standard.md §4).
struct MeshRxItem {
    uint16_t src;                           // source node address
    uint8_t len;                            // valid bytes in `bytes`
    uint8_t bytes[MESH_MAX_PAYLOAD_BYTES];  // payload, clamped to capacity
};

// Receive queue: filled by the LoRaMesher data callback, drained by
// mesh_rx_task. Created in init_mesh(); defined in mesh.cpp.
extern QueueHandle_t rx_queue;
