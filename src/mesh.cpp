// mesh.cpp

#include "mesh.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "loramesher.hpp"

#include "config.h"
#include "queues.h"
#include "sensor.h"
#include "tasks.h"
#include "telemetry.h"

static const char* TAG = "Mesh";

static std::unique_ptr<loramesher::LoraMesher> mesher;

// Defined here, declared extern in queues.h.
QueueHandle_t rx_queue = nullptr;

// LoRaMesher receive callback. Runs in the protocol task context: it only
// copies the payload into a by-value POD and enqueues it, then returns. No
// logging, decoding, blocking, or allocation here (Coding-Standard.md §4).
static void on_data_received(loramesher::AddressType source,
                             const std::vector<uint8_t>& data) {
    MeshRxItem item;
    item.src = source;
    size_t len = data.size();
    if (len > MESH_MAX_PAYLOAD_BYTES) {
        len = MESH_MAX_PAYLOAD_BYTES;
    }
    item.len = static_cast<uint8_t>(len);
    memcpy(item.bytes, data.data(), len);

    // Timeout 0: never block the protocol task. A full queue drops the item.
    xQueueSend(rx_queue, &item, 0);
}

bool init_mesh() {
    loramesher::AddressType derived =
        loramesher::LoraMesher::GenerateAddressFromHardware();
    ESP_LOGI(TAG, "address_from_hardware addr=0x%04x", derived);

    loramesher::PinConfig pin_config(LORA_PIN_CS, LORA_PIN_RST, LORA_PIN_IRQ,
                                     LORA_PIN_IO1, LORA_PIN_SCK, LORA_PIN_MISO,
                                     LORA_PIN_MOSI);
    ESP_LOGI(TAG, "pin_config_built cs=%d rst=%d irq=%d io1=%d sck=%d miso=%d mosi=%d",
             LORA_PIN_CS, LORA_PIN_RST, LORA_PIN_IRQ, LORA_PIN_IO1, LORA_PIN_SCK,
             LORA_PIN_MISO, LORA_PIN_MOSI);

    loramesher::RadioConfig radio_config(
        LORA_RADIO_TYPE, LORA_FREQUENCY_MHZ, LORA_SPREADING_FACTOR,
        LORA_BANDWIDTH_KHZ, LORA_CODING_RATE, LORA_POWER_DBM, LORA_SYNC_WORD,
        LORA_CRC_ENABLED, LORA_PREAMBLE_LENGTH);
    ESP_LOGI(TAG,
             "radio_config_built freq_mhz=%.3f sf=%u bw_khz=%.1f cr=%u "
             "power_dbm=%d sync=0x%02x crc=%d preamble=%u",
             LORA_FREQUENCY_MHZ, LORA_SPREADING_FACTOR, LORA_BANDWIDTH_KHZ,
             LORA_CODING_RATE, LORA_POWER_DBM, LORA_SYNC_WORD, LORA_CRC_ENABLED,
             LORA_PREAMBLE_LENGTH);

    loramesher::LoRaMeshProtocolConfig mesh_config;
    mesh_config.setMaxHops(MESH_MAX_HOPS);
    mesh_config.setMaxPacketSize(MESH_MAX_PACKET_SIZE);
    ESP_LOGI(TAG, "mesh_config_built max_hops=%u max_packet_size=%u",
             MESH_MAX_HOPS, MESH_MAX_PACKET_SIZE);

    loramesher::NodeRole mesh_role =
        (derived == MESH_MANAGER_ADDRESS)
            ? loramesher::NodeRole::NETWORK_MANAGER
            : loramesher::NodeRole::NODE_ONLY;
    mesh_config.setNodeRole(mesh_role);
    ESP_LOGI(TAG, "mesh_role_selected addr=0x%04x is_manager=%d", derived,
             derived == MESH_MANAGER_ADDRESS);

    rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(MeshRxItem));
    if (rx_queue == nullptr) {
        ESP_LOGE(TAG, "rx_queue_create_failed depth=%u item_bytes=%u",
                 RX_QUEUE_DEPTH, static_cast<unsigned>(sizeof(MeshRxItem)));
        return false;
    }
    ESP_LOGI(TAG, "rx_queue_created depth=%u item_bytes=%u", RX_QUEUE_DEPTH,
             static_cast<unsigned>(sizeof(MeshRxItem)));

    mesher = loramesher::LoraMesher::Builder()
                 .withPinConfig(pin_config)
                 .withRadioConfig(radio_config)
                 .withLoRaMeshProtocol(mesh_config)
                 .Build();
    ESP_LOGI(TAG, "mesher_built");

    mesher->SetDataCallback(on_data_received);
    ESP_LOGI(TAG, "data_callback_registered");

    loramesher::Result start_result = mesher->Start();
    if (!start_result) {
        ESP_LOGE(TAG, "mesh_start_failed reason=%s",
                 start_result.GetErrorMessage().c_str());
        return false;
    }

    loramesher::AddressType node_addr = mesher->GetNodeAddress();
    ESP_LOGI(TAG, "mesh_init_ok address=0x%04x", node_addr);
    return true;
}

// Mixed trigger (justified per Architecture.md §1): blocks on rx_queue, but
// with a finite timeout so the mesh_rx_count line is still emitted on the
// fixed reporting interval when no traffic is arriving.
void mesh_rx_task(void* /*pvParameters*/) {
    static uint32_t mesh_rx_count = 0;

    MeshRxItem item;
    int64_t last_report_us = esp_timer_get_time();

    for (;;) {
        BaseType_t got = xQueueReceive(rx_queue, &item,
                                       pdMS_TO_TICKS(MESH_RX_REPORT_INTERVAL_MS));
        if (got == pdTRUE) {
            // Parse the combined telemetry + routing-table payload in place
            // (zero-copy: the fields are read straight out of item.bytes, no
            // allocation, no intermediate buffer). One summary line carries the
            // telemetry and route count; each route entry the sender reported is
            // then printed so the manager's serial log shows every node's routing
            // table — the topology view. A frame that fails validation (wrong
            // length / not telemetry) is dumped as raw hex instead of misread.
            TelemetryHeader hdr;
            if (telemetry_parse_header(item.bytes, item.len, hdr)) {
                ESP_LOGI(TAG,
                         "mesh_rx src=0x%04x seq=%u ts_ms=%u temp_c=%d routes=%u",
                         item.src, hdr.seq, hdr.timestamp_ms, hdr.chip_temp_c,
                         hdr.entry_count);
                for (uint8_t i = 0; i < hdr.entry_count; ++i) {
                    TelemetryRouteEntry entry;
                    telemetry_parse_route_entry(item.bytes, i, entry);
                    ESP_LOGI(TAG,
                             "mesh_route_report src=0x%04x dst=0x%04x "
                             "next=0x%04x hops=%u valid=%u mgr=%u",
                             item.src, entry.destination, entry.next_hop,
                             entry.hop_count,
                             (entry.flags & TELEMETRY_ROUTE_FLAG_VALID) ? 1 : 0,
                             (entry.flags & TELEMETRY_ROUTE_FLAG_MANAGER) ? 1
                                                                          : 0);
                }
            } else {
                char hex[MESH_MAX_PAYLOAD_BYTES * 2 + 1];
                size_t pos = 0;
                for (uint8_t i = 0; i < item.len; ++i) {
                    pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x",
                                    item.bytes[i]);
                }
                hex[pos] = '\0';
                ESP_LOGW(TAG, "mesh_rx_malformed src=0x%04x len=%u bytes=%s",
                         item.src, item.len, hex);
            }
            mesh_rx_count++;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >=
            static_cast<int64_t>(MESH_RX_REPORT_INTERVAL_MS) * 1000) {
            ESP_LOGI(TAG, "mesh_rx_stats count=%u", mesh_rx_count);
            // Dump the routing table so multi-hop reachability is observable:
            // for the route to the manager, next is the relay and hops>1 means
            // the path is indirect (Phase 4 done-criteria #2 and #3). rssi/snr
            // are the direct-neighbour link quality (0 = unknown / not a direct
            // neighbour); they drive a walk test to find where a link drops out.
            for (const loramesher::RouteEntry& route : mesher->GetRoutingTable()) {
                ESP_LOGI(TAG,
                         "mesh_route dst=0x%04x next=0x%04x hops=%u valid=%d "
                         "mgr=%d rssi=%.1f snr=%.1f",
                         route.destination, route.next_hop, route.hop_count,
                         route.is_valid, route.is_network_manager,
                         route.last_rssi, route.last_snr);
            }
            last_report_us = now_us;
        }
    }
}

// Periodic trigger: sleeps until the next TX data slot (or a fixed fallback
// before the node has joined), then sends the hello payload to the first
// valid non-self route in the table. Not vTaskDelayUntil because the slot
// cadence is set at runtime by the mesh scheduler, not a fixed period.
void mesh_tx_task(void* /*pvParameters*/) {
    // Per-node send counter. 32 bits internally so reboot detection stays
    // unambiguous; only the low 16 bits ride the wire (packet-structure.md §3).
    // Incremented only after a successful send, so a retried slot reuses the same
    // seq and the receiver sees a contiguous, gap-free sequence.
    static uint32_t tx_seq = 0;
    // Pre-sized to the largest combined payload so the per-iteration assign
    // overwrites bytes in place (no realloc, no heap in the task —
    // Coding-Standard.md §1); Send() takes it by const reference.
    static std::vector<uint8_t> tx_payload(TELEMETRY_MAX_PAYLOAD_BYTES);

    for (;;) {
        uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot();
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 0 ? wait_ms
                                             : MESH_TX_FALLBACK_DELAY_MS));

        std::vector<loramesher::RouteEntry> routes = mesher->GetRoutingTable();
        loramesher::AddressType self = mesher->GetNodeAddress();
        loramesher::AddressType dst = 0;
        bool have_dst = false;
        for (const loramesher::RouteEntry& route : routes) {
            // Destination = the network manager, learned dynamically (no
            // hardcoded address). The manager's only manager-route is to
            // itself, so the destination != self guard leaves the manager
            // sending nothing.
            if (route.is_valid && route.is_network_manager &&
                route.destination != self) {
                dst = route.destination;
                have_dst = true;
                break;
            }
        }
        if (!have_dst) {
            continue;  // manager route not learned yet; retry next slot
        }

        loramesher::Result ready = mesher->IsReadyToSend(dst);
        if (!ready) {
            // Expected during discovery/sync before a TX slot is allocated.
            ESP_LOGD(TAG, "mesh_tx_not_ready dst=0x%04x reason=%s", dst,
                     ready.GetErrorMessage().c_str());
            continue;
        }

        // Build the combined telemetry + routing-table payload at send time
        // (dev-resource/routing-table-reporting.md §4): the telemetry header
        // (current seq, a fresh boot-relative timestamp, the latest published
        // chip-temperature sample), followed by this node's own routing table so
        // the manager can reconstruct the mesh topology. `routes` is the table
        // already fetched above for destination selection. Packed into a stack
        // buffer, then assigned into the pre-sized tx_payload so Send() adds no
        // heap.
        uint8_t buf[TELEMETRY_MAX_PAYLOAD_BYTES];

        uint8_t entry_count = 0;
        for (const loramesher::RouteEntry& route : routes) {
            if (route.destination == self) {
                continue;  // skip self: the manager already learns our address
            }
            if (entry_count >= TELEMETRY_MAX_ROUTE_ENTRIES) {
                break;  // stay within the 64 B frame cap
            }
            TelemetryRouteEntry entry;
            entry.destination = route.destination;
            entry.next_hop = route.next_hop;
            entry.hop_count = route.hop_count;
            entry.flags = 0;
            if (route.is_valid) {
                entry.flags |= TELEMETRY_ROUTE_FLAG_VALID;
            }
            if (route.is_network_manager) {
                entry.flags |= TELEMETRY_ROUTE_FLAG_MANAGER;
            }
            telemetry_pack_route_entry(
                entry, buf + TELEMETRY_HEADER_BYTES +
                           entry_count * TELEMETRY_ROUTE_ENTRY_BYTES);
            entry_count++;
        }

        TelemetryHeader hdr;
        hdr.seq = static_cast<uint16_t>(tx_seq & 0xFFFF);
        hdr.timestamp_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        hdr.chip_temp_c = sensor_latest_chip_temp_c();
        hdr.entry_count = entry_count;
        telemetry_pack_header(hdr, buf);

        size_t payload_len =
            TELEMETRY_HEADER_BYTES +
            static_cast<size_t>(entry_count) * TELEMETRY_ROUTE_ENTRY_BYTES;
        tx_payload.assign(buf, buf + payload_len);

        loramesher::Result send_result = mesher->Send(dst, tx_payload);
        if (send_result) {
            ESP_LOGI(TAG,
                     "mesh_tx dst=0x%04x seq=%u temp_c=%d routes=%u len=%u", dst,
                     hdr.seq, hdr.chip_temp_c, entry_count,
                     static_cast<unsigned>(tx_payload.size()));
            tx_seq++;
        } else {
            ESP_LOGE(TAG, "mesh_tx_failed dst=0x%04x reason=%s", dst,
                     send_result.GetErrorMessage().c_str());
        }
    }
}
