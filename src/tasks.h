// tasks.h
#pragma once

// Long-lived FreeRTOS tasks, declared here so the full task set is visible in
// one place (Architecture.md §1). Bodies live in their feature module's .cpp;
// every xTaskCreate call lives in setup(). Both take the FreeRTOS task-entry
// signature.

// Drains rx_queue, logs each received payload, reports mesh_rx_count.
void mesh_rx_task(void* pvParameters);

// Sends this node's self-identifying payload ("Data from 0x<addr>") to the
// manager once per data slot.
void mesh_tx_task(void* pvParameters);
