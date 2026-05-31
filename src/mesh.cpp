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
#include "tasks.h"

static const char* TAG = "Mesh";

static std::unique_ptr<loramesher::LoraMesher> mesher;

// Defined here, declared extern in queues.h.
QueueHandle_t rx_queue = nullptr;

// Fixed hello payload, built once from MESH_HELLO_PAYLOAD in init_mesh() so
// mesh_tx_task can pass it to Send() by const reference without allocating on
// each iteration (Coding-Standard.md §1: no heap in task code).
static std::vector<uint8_t> hello_payload;

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
    ESP_LOGI(TAG, "mesh_config_built max_hops=%u", MESH_MAX_HOPS);

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

    hello_payload.assign(MESH_HELLO_PAYLOAD.begin(), MESH_HELLO_PAYLOAD.end());
    ESP_LOGI(TAG, "hello_payload_built len=%u",
             static_cast<unsigned>(hello_payload.size()));

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
            char hex[MESH_MAX_PAYLOAD_BYTES * 2 + 1];
            size_t pos = 0;
            for (uint8_t i = 0; i < item.len; ++i) {
                pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x",
                                item.bytes[i]);
            }
            hex[pos] = '\0';
            ESP_LOGI(TAG, "mesh_rx src=0x%04x len=%u bytes=%s", item.src,
                     item.len, hex);
            mesh_rx_count++;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_report_us >=
            static_cast<int64_t>(MESH_RX_REPORT_INTERVAL_MS) * 1000) {
            ESP_LOGI(TAG, "mesh_rx_stats count=%u", mesh_rx_count);
            last_report_us = now_us;
        }
    }
}

// Periodic trigger: sleeps until the next TX data slot (or a fixed fallback
// before the node has joined), then sends the hello payload to the first
// valid non-self route in the table. Not vTaskDelayUntil because the slot
// cadence is set at runtime by the mesh scheduler, not a fixed period.
void mesh_tx_task(void* /*pvParameters*/) {
    for (;;) {
        uint32_t wait_ms = mesher->GetTimeUntilNextDataSlot();
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 0 ? wait_ms
                                             : MESH_TX_FALLBACK_DELAY_MS));

        std::vector<loramesher::RouteEntry> routes = mesher->GetRoutingTable();
        loramesher::AddressType self = mesher->GetNodeAddress();
        loramesher::AddressType dst = 0;
        bool have_dst = false;
        for (const loramesher::RouteEntry& route : routes) {
            if (route.is_valid && route.destination != self) {
                dst = route.destination;
                have_dst = true;
                break;
            }
        }
        if (!have_dst) {
            continue;  // no peer yet; retry next slot
        }

        loramesher::Result ready = mesher->IsReadyToSend(dst);
        if (!ready) {
            // Expected during discovery/sync before a TX slot is allocated.
            ESP_LOGD(TAG, "mesh_tx_not_ready dst=0x%04x reason=%s", dst,
                     ready.GetErrorMessage().c_str());
            continue;
        }

        loramesher::Result send_result = mesher->Send(dst, hello_payload);
        if (send_result) {
            ESP_LOGI(TAG, "mesh_tx dst=0x%04x len=%u", dst,
                     static_cast<unsigned>(hello_payload.size()));
        } else {
            ESP_LOGE(TAG, "mesh_tx_failed dst=0x%04x reason=%s", dst,
                     send_result.GetErrorMessage().c_str());
        }
    }
}
