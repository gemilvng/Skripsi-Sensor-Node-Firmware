# Implementation Record

## Purpose

This document is the development guardrail for the sensor node firmware. It defines a fixed sequence of phases. Each phase has a small, observable result. Read the current phase's done-criteria, build only what that phase requires, verify the result on hardware, commit and tag, then move to the next phase. Do not work on a later phase until the current one is done. Do not bundle two phases into one commit.

## Project context

The project builds firmware for a wireless sensor network used in academic research.

- Topology: LoRa mesh of five nodes. One binary is flashed to every node. Each node decides its role at runtime: sensor, router, or sink.
- A sink is a node that has joined a Wi-Fi network and forwards mesh data to a monitoring server over MQTT. A node that cannot join Wi-Fi runs as sensor and router only.
- Hardware: LilyGo TTGO T-Beam V1.2 (ESP32, SX1276 LoRa radio, NEO-6M GPS, AXP2101 power management). GPS is not used by the firmware in this plan.
- Toolchain: PlatformIO 6.1.19 in VS Code, Arduino framework, ESP32 Arduino Core 2.0.17.
- Mesh stack: the LoRaMesher library. Features it already provides should be used through its API rather than reimplemented.
- Region: Indonesia. Outdoor radio operation must comply with Kemenkominfo rules (AS923 band, duty cycle, transmit power).

## Guiding principles

1. Each phase ends with a result that can be observed on hardware or in serial logs. A phase is not done until that result is demonstrated, so untested behavior never becomes the foundation for the next phase.
2. One concern per phase. Do not bring up two new peripherals or two new features in the same phase. If a problem appears, the cause is in the phase being worked on.
3. `main.cpp` is the wiring layer — it calls each phase's setup function and registers tasks, and contains no feature logic — so it stays readable as the firmware's structure at a glance.
4. New code for each phase lives in its own file or folder under `src/`, so the source layout mirrors the phase structure and any phase's code can be located by name.
5. External libraries are declared in `platformio.ini` under `lib_deps` rather than vendored into `src/`, so dependency versions are pinned in one place and the source tree stays project-only code.
6. Commit at every phase boundary, so each commit corresponds to a known-good firmware state and history can be bisected by phase.
7. Every commit message follows the same shape: a one-sentence subject summarizing the change, a blank line, a `Details:` line, and a bulleted list of specific changes — so any commit's scope is enumerated explicitly and the log reads consistently.

---

## Phase 0 — Toolchain and repository hygiene

Set up the build environment and a minimal firmware that prints a startup line on boot.

Feature needs:
- `platformio.ini` selects the TTGO T-Beam board, the Arduino framework, and a pinned platform version.
- Serial monitor baud rate matches the firmware's UART0 baud.
- Compiler warnings are enabled.
- `.gitignore` excludes build artifacts and editor caches.
- `main.cpp` prints a startup line on boot.

Done when:
- The project builds without errors and without unexpected warnings.
- Flashing the board succeeds.
- The serial monitor shows the startup line after every reset.

Implementation:
- `Serial.begin()` is not called. The ESP32 boot chain already configures UART0 at 115200 before `setup()` runs, and `ESP_LOGI` writes there directly. To turn the inherited baud into an observed fact, `setup()` reads it back via `uart_get_baudrate()` and logs the result. Revisit at Phase 5 if a library needs serial input or the Arduino `Stream` API.
- `lib_deps` declares LoRaMesher and XPowersLib ahead of their use in Phases 2 and 3.
- The phase boundary commit was not tagged; phase-to-commit mapping is recorded in this document.
- `uart_get_baudrate()` returns 115201, not 115200. This is a rounding artifact of the integer divisor that produces the UART baud; the UART is effectively at 115200 and the monitor decodes cleanly.
- PSRAM was detected and enabled automatically by the Arduino framework startup, visible in the boot log. Available for future allocation when needed.
- Upload runs on COM5 at 460800 baud (T-Beam board default). The serial monitor remains at 115200; the two are independent.
- A benign upload warning appears: *"Detected crystal freq 41.01MHz is quite different to normalized freq 40MHz."* This is a known T-Beam V1.2 characteristic and does not affect operation.
- Phase 0 closes at commit `a62889b`.

## Phase 1 — Serial sanity

Confirm board, USB, and toolchain end-to-end before adding any peripherals.

Feature needs:
- The firmware prints an incrementing uptime counter on serial at a fixed interval.

Done when:
- The serial counter increments without gaps or resets.

Implementation:
- Feature logic stays in `loop()` rather than introducing a FreeRTOS task. The counter is a local-`static` `uint32_t` incremented and logged on each iteration; cadence is set by `vTaskDelay(1000 / portTICK_PERIOD_MS)`. Reason: Phase 1 has one concurrent activity, so the `tasks.h` + `xTaskCreate` pattern would be scaffolding without a need. The "loop() is unused" rule in `Architecture.md` describes the steady-state shape and resolves naturally at Phase 2 when real tasks arrive.
- No new module created; `main.cpp` keeps its single TAG `"Main"` for both setup and loop log lines. A separate TAG arrives when the uptime behavior moves into its own module.
- Counter log line follows the data-bearing shape: `[Main] uptime_tick count=N`.
- Observed cadence is ~1006 ms between log lines, not 1000 ms exactly. `vTaskDelay` waits a fixed interval *after* the loop body runs, so log-formatting and UART-write overhead add on top. Acceptable for Phase 1; switch to `vTaskDelayUntil` if drift-free cadence becomes a requirement.
- Upload port shifted from COM5 (Phase 0) to COM4 (Phase 1) between runs. This is host-side USB-serial enumeration variance; `platformio.ini` does not pin a port and PlatformIO auto-detects each upload.
- Soak: counter incremented from 1 to 134 contiguously over ~134 seconds — no gaps, no resets, no spontaneous boot-banner reappearance.

## Phase 2 — AXP2101 power-on for required rails

Bring up the power management chip so the LoRa radio (and any other peripheral the firmware will need) has power. On the T-Beam V1.2, several peripherals are powered through the AXP2101 and are otherwise dead.

Feature needs:
- The firmware talks to the AXP2101 over I2C at boot.
- The rail that feeds the LoRa radio is enabled. Other rails that are not needed in this plan stay off.
- Battery voltage and charging status are read and printed periodically on serial.

Done when:
- The AXP2101 responds and is configured without errors.
- Battery voltage and charging status appear on serial and change sensibly when USB power is connected or removed.
- The LoRa rail is on, which will be confirmed in the next phase.

## Phase 3 — LoRaMesher two-node hello

Bring the mesh stack up on hardware with the smallest possible network.

Feature needs:
- Configure the LoRa radio pins for the T-Beam V1.2.
- Configure radio parameters (frequency, spreading factor, bandwidth, coding rate, transmit power) for the AS923 band.
- Two boards run the same firmware. Each board has a distinct node address.
- One board sends a short fixed payload to the other on a fixed interval.
- The receiving board logs the source address and payload on serial.

Done when:
- Both boards discover each other and each appears in the other's routing table.
- The receiving board prints the expected payload on serial within a few seconds of each transmission.
- Roles of sender and receiver can be swapped without firmware changes.

## Phase 4 — LoRaMesher three-node relay

Verify that traffic actually traverses an intermediate node, not just the direct link.

Feature needs:
- A third board, same firmware.
- Three boards arranged so node A cannot directly reach node C. Force this either by physical separation, by attenuation, or by reducing transmit power.
- A sends a payload addressed to C. B is the only path.

Done when:
- A's transmission reaches C, observed in C's serial log.
- The routing table on A shows C reachable through B with a hop count greater than one.
- Powering B off causes A to lose its route to C. Powering B back on restores it.

## Phase 5 — Sensor bring-up

Add a sensor source. The exact sensor is not yet chosen, so a stand-in is acceptable for this phase.

Feature needs:
- A sensor module produces a reading on a fixed sampling interval.
- If a physical sensor is available, it is wired and read through its driver.
- If no physical sensor is chosen yet, a stub returns a fixed or pseudo-random value of the right shape.
- Each reading is printed on serial with a timestamp counter.

Done when:
- Readings appear on serial at the configured cadence, with stable formatting.
- The sampling loop does not block mesh activity from earlier phases.

## Phase 6 — Application payload encode and decode

Move sensor data from one node to another through the mesh in a defined byte layout.

Feature needs:
- A defined payload format that includes at least: node address, sequence number, sensor reading. The format is documented in a comment or a short header file.
- A sender encodes the payload from current sensor data and sends it through the mesh to a target node.
- A receiver decodes the payload in the data-received path and logs the decoded fields on serial.
- Sequence numbers increase monotonically per source node.

Done when:
- A reading produced on the sending node is reconstructed byte-identical on the receiving node.
- Sequence numbers on the receiver side increase by one per packet from the same source, with no decoding errors over a sustained run of at least a few minutes.

## Phase 7 — Sink role with Wi-Fi and MQTT

Add the sink behavior: a node that joins Wi-Fi promotes itself, receives payloads from the mesh, and forwards them to an MQTT broker.

Feature needs:
- On boot, the node tries to join a configured Wi-Fi network within a timeout.
- If Wi-Fi joins successfully, the node promotes itself to the role that acts as the mesh gateway and advertises a gateway capability to the mesh.
- If Wi-Fi is unavailable or is lost later, the node demotes itself back to a non-gateway role and stops advertising the gateway capability.
- When acting as sink, the node connects to a configured MQTT broker, then for every payload received from the mesh, publishes it to a configured topic. Each MQTT message carries a timestamp added at publish time.
- Wi-Fi reconnects automatically after a drop, with backoff.
- MQTT reconnects automatically after a drop, with backoff.
- During a transient broker outage, recently received payloads are held in a small bounded buffer and published after reconnect. When the buffer is full, the oldest entries are dropped.

Done when:
- One sink and one sensor node are running. Readings published by the sink appear at the broker, observable with a generic MQTT subscriber.
- Wi-Fi drop test: disable the access point or the broker while running. The sink stops publishing without crashing, logs the failure, and continues to participate in the mesh. When connectivity returns, publishing resumes automatically within the configured backoff window.
- Role demotion test: start the sink-capable node with Wi-Fi unreachable. It comes up as a non-gateway node. When Wi-Fi later becomes reachable, it promotes to gateway and other nodes start seeing a gateway available in the mesh.

## Phase 8 — Five-node demo and resilience checks

Run the full network and verify it behaves correctly under node failures.

Feature needs:
- Five boards: one sink, four sensor and router nodes.
- The physical layout forces at least one multi-hop path between some sensor node and the sink.
- All nodes run for a sustained period long enough to characterize behavior, for example one hour.
- Logs from the sink record every received payload. Each sensor node logs its own transmissions.

Done when:
- Steady-state: the broker receives readings from all four sensor nodes for the duration of the run. Packet loss, end-to-end latency, and battery drain are recorded.
- Kill-a-node test: while the network is running, power off one intermediate node that is being used as a relay. The mesh reconverges: affected sensor nodes find an alternate route, or rejoin the network through another path. The broker continues to receive readings from the remaining sensor nodes, possibly with a gap during reconvergence. Powering the killed node back on, it rejoins and resumes publishing.
- Sink-kill test: power off the sink while the network is running. The surviving nodes detect the loss and enter their recovery behavior. If another node is sink-capable and Wi-Fi is reachable, it promotes; otherwise the mesh continues without a gateway until the original sink returns. The observed outcome is documented either way.

---

## Deferred items

The following are not separate phases. They are revisited only when Phase 8 data motivates the work.

- Power policy tuning. After Phase 8 records battery drain, decide whether to allocate more sleep slots to leaf sensor nodes, whether to power down sensor rails between samples, and whether any role should adjust its sleep behavior. Routers cannot deep-sleep because they must remain available to relay.
- Application-layer recovery refinements. After Phase 8 resilience tests, decide whether the sink's buffering policy, retry intervals, or role demotion thresholds need adjustment.
- GPS use. Not part of this plan. Revisit only if a future requirement introduces geotagging or absolute wall-clock timestamps.
- Security. Not part of this plan. The mesh and the MQTT link run without encryption or authentication for the demo. Note this as a known limitation in any written report.

## Practical flags

- Power five nodes from a powered hub or charged 18650 cells. Laptop USB ports sag under five T-Beams and produce intermittent faults that look like firmware bugs.
- On the bench, two T-Beams within a few centimeters of each other talk through direct RF leakage regardless of routing intent. To verify multi-hop behavior, separate boards physically, reduce transmit power, or shield as needed.
- Verify the current Kemenkominfo rules for AS923 band, duty cycle, and transmit power before any outdoor deployment. Adjust radio configuration to comply.
