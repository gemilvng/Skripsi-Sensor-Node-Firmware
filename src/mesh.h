// mesh.h
#pragma once

// Initialize and start the LoRaMesher mesh stack on the LoRa rail.
// Returns true iff the stack is running and ready to participate in
// the mesh. Registers the application data callback and starts both
// the receive and transmit paths (mesh_rx_task / mesh_tx_task).
bool init_mesh();
