# Architecture

## Scope

This document records the project-specific architecture and design decisions for the sensor node firmware: the runtime task graph, the role and configuration model, library-specific facts that affect error handling, and the project's logging shape. Companion documents: `documentation/Coding-Standard.md` for code style and language rules (project-agnostic), and `documentation/Implementation-Record.md` for per-phase plan and implementation history. Project context — hardware, toolchain, libraries, region — is described in `Implementation-Record.md`.

The firmware's overarching design driver is **observability** — the runtime must emit data that can be analyzed for packet loss, latency, and reconnect counts — and **inspectability** — a reader must be able to derive task-state and timing diagrams from the source without running it. Every architectural rule below exists to satisfy that requirement, or to respect a library constraint.

## Runtime architecture

The firmware drives behavior through four long-lived FreeRTOS tasks, created once in `setup()` and never destroyed. Arduino's `loop()` is unused.

- **`mesh_rx_task`** — drains `rx_queue` (fed by the LoRaMesher receive callback), logs every received payload, and when the node is acting as sink forwards the payload onto `mqtt_queue` with drop-oldest overflow behavior.
- **`sensor_sample_task`** — reads the sensor on a fixed cadence. On non-sink nodes it sends the encoded reading through LoRaMesher addressed to the sink; on sink nodes it pushes the reading directly into `mqtt_queue`.
- **`connectivity_task`** — owns Wi-Fi state and the sink-role decision: join, backoff reconnect, promotion or demotion.
- **`mqtt_publish_task`** — drains `mqtt_queue` and publishes to the broker when the node is sink and the broker is reachable; otherwise it idles on its queue.

The current role is held in a single `std::atomic<Role> g_role`, written only by `connectivity_task` and read by the others. The AXP2101 is initialized once in `setup()` to enable the LoRa power rail; it does not have a dedicated task. All tunable values live in `src/config.h`.

## Section 1 — Architecture rules

**All long-running tasks are created in `setup()` and declared in a single header `src/tasks.h`.** There are no `xTaskCreate` calls anywhere else in the codebase.
*Why:* the task diagram is finite and drawable only if every task is visible in one place.

**Inter-task communication uses FreeRTOS queues declared in `src/comms.h`.** Shared mutable globals are not used as a backchannel between tasks.
*Why:* the queue set is the system's nervous system; making it implicit through globals breaks the timing and state diagrams.

**Each task has exactly one trigger: either a periodic delay via `vTaskDelayUntil`, or a blocking queue read.** A mixed-trigger task must be justified in a comment at the top of its function.
*Why:* a single trigger gives the task one row on the timing diagram and one entry transition on the state diagram.

**Tasks that have no work for the current role block on their queue or sleep; they are not destroyed and re-created.**
*Why:* the task count stays constant across role transitions, so the diagram does not have conditional boxes.

**The role is held in a single `std::atomic<Role> g_role`.** Only `connectivity_task` writes to it; all other tasks may read it but must not write.
*Why:* a single writer means the role state machine has one source of truth and one place to inspect.

**The LoRaMesher receive callback pushes the payload onto `rx_queue` and does no other work.** Decoding, logging, and routing into `mqtt_queue` happen in `mesh_rx_task`.
*Why:* LoRaMesher documents that the callback runs in protocol context and long work there stalls the mesh; deferring also ensures all logging passes through a task we own.

**The `mqtt_queue` is bounded and uses drop-oldest behavior when full.** The bound is a named constant in `config.h`.
*Why:* required by Phase 7 (`Implementation-Record.md`); bounded memory and a documented overflow policy are necessary for predictable behavior during broker outages.

## Section 2 — Role and configuration model

**There is one binary for all nodes.** Role-dependent behavior branches on `g_role` at runtime; `#ifdef`-style role variants are not allowed.
*Why:* eliminates a class of build-time mistakes and keeps the firmware story simple in the thesis.

**All values that tune behavior live as `constexpr` constants in `src/config.h`.** This includes cadences, timeouts, queue depths, task stack sizes, pin numbers, Wi-Fi credentials, MQTT broker address and topic, and LoRa radio parameters. The `Role` `enum class` definition also lives in `src/config.h`.
*Why:* a reader can find every parameter that shapes the timing diagram in one file; tuning in Phase 8 changes one file.

**Literal numeric values for timing, sizing, addresses, or pins do not appear inline in task code.** They are referenced through their named constant.
*Why:* magic numbers in task bodies break the link between the source and the timing diagram.

## Section 3 — Library-specific error-handling facts

This section complements `Coding-Standard.md` Section 2, which states the general rules (check at call site, no library types across module boundaries, no panic-and-reboot in task code).

**The libraries and platform return three error-shape types:**

- `esp_err_t` from ESP-IDF (`ESP_OK` on success).
- A `Result` value from LoRaMesher.
- `bool` or a status code from XPowersLib.

Each is checked at the call site it originates from. Internal helpers convert these to `bool` or early-returns before crossing module boundaries.

**LoRaMesher is consumed through its `Builder` and a held `std::unique_ptr<LoraMesher>`.** **XPowersLib is consumed through an `XPowersAXP2101` instance held in module-scope state.** Both objects live as `static` variables inside their respective modules.
*Why:* the libraries decide their own API shape; the project follows it.

## Section 4 — Project-specific logging shape

This section complements `Coding-Standard.md` Section 3, which states the general rules (one event per line, fixed `TAG keyword key=value` shape for data-bearing lines, `esp_timer_get_time()` timestamps inside content, no logging in ISR or callback).

**The inherited IDF console is the log sink.** UART0 is configured at 115200 by the boot chain before `setup()` runs; `ESP_LOGx` writes there directly. No explicit `Serial.begin()` is called. (Decision recorded in `Implementation-Record.md` Phase 0.)

**The LoRaMesher receive callback explicitly does not log** — doubled emphasis on the general no-logging-in-callback rule because LoRaMesher documents that callbacks run in protocol context where stalls break the mesh.

**Named counters per task that this project relies on for Phase 8 analysis include at least:**

- `mesh_rx_count` (in `mesh_rx_task`).
- `mqtt_publish_ok`, `mqtt_publish_fail` (in `mqtt_publish_task`).
- `wifi_reconnect_count`, `mqtt_reconnect_count` (in `connectivity_task`).
- `sensor_sample_count` (in `sensor_sample_task`).

Each counter is incremented at its event and emitted in a periodic data-bearing log line by its owning task. The shape of the log line is defined in `Coding-Standard.md` Section 3.
