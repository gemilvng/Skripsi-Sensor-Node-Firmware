// mesh.h
#pragma once

// Initialize and start the LoRaMesher mesh stack on the LoRa rail.
// Returns true iff the stack is running and ready to participate in
// the mesh. Does not register any application data callback; the
// receive and transmit paths are added in a later phase.
bool init_mesh();
