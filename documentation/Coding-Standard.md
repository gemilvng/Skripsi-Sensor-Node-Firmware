# Coding Standard

## Scope

This document defines code style and language-subset rules for firmware development. The rules are project-agnostic — they apply to any ESP32 Arduino / ESP-IDF firmware in this author's work, not just one project — and are meant to be loaded as cross-session context by any AI assistant working on the codebase, so generated code matches hand-written code without reinventing conventions each session.

Project-specific decisions — the runtime architecture, the role and configuration model, library-specific error-handling facts, and the project's logging shape — live in `documentation/Architecture.md`. Per-phase plan and implementation history live in `documentation/Implementation-Record.md`. This document references neither and should remain useful across firmware projects.

## Section 1 — Paradigm and C++ subset

**Code is procedural and module-oriented.** A module is a `.cpp/.h` pair that exposes free functions. Module-private state is declared `static` in the `.cpp` file.
*Why:* the unit of work in firmware is the task and the module, not the object; module-scope `static` gives the encapsulation a class would, without the boilerplate.

**A module exposes only the entry points its callers actually need.** A module with no asynchronous behavior may have only `init_*`; one with periodic work also exposes `*_task`. Other entry points (getters, deinit, poll triggers) are added only when a concrete caller needs them.
*Why:* a public API is a commitment; minimize it.

**`init_*` functions return `bool` indicating success.** Callers gate dependent operations on this return value.
*Why:* makes inter-module dependencies explicit rather than implicit through call order.

**Classes are written only when state, lifetime, and paired operations cluster naturally** — for example, an RAII wrapper around a FreeRTOS mutex's acquire and release. Singletons of which there is only one instance in the firmware are not wrapped in classes.
*Why:* a class adds boilerplate without clarifying anything when there is exactly one of the thing.

**OOP usage at library boundaries follows the library's idiom.** Held instances live as `static` variables inside the module that owns them.
*Why:* the library decides its own API shape; fighting it produces friction without payoff.

**When a module wraps a library, the module's public header does not include the library's headers.** Library `#include` directives and types stay in the `.cpp` file; other modules use the wrapper's public API.
*Why:* the library is encapsulated in one place; updating or replacing it touches one file.

**The allowed C++ feature set is:** C++20 language features, `std::unique_ptr` and `std::shared_ptr`, `std::vector`, `std::optional`, `enum class`, `constexpr`, references, RAII, namespaces, `static_assert`.

**The following features are not used in own code:** exceptions, RTTI and `dynamic_cast`, `std::function` in hot paths, `std::string`, virtual functions in classes you write (unless multiple implementations are dispatched at runtime), multiple inheritance, template metaprogramming beyond simple `constexpr` helpers.
*Why:* each has measurable cost in flash, RAM, latency, or predictability on an MCU, and none are needed by the typical embedded data flow.

**Heap allocation is permitted during `setup()` only.** Containers may grow during `setup()` (including `std::vector::reserve()`); after `setup()` returns, containers must not grow beyond their reserved capacity, and `new`/`malloc` are not called from task code.
*Why:* steady-state heap stability avoids fragmentation and unpredictable latency once the firmware is running.

## Section 2 — Error handling

**Every return value is checked at the call site where it is produced.** Library-specific error types do not propagate across module boundaries.
*Why:* checking at the call site keeps the source of failure local; not propagating library types prevents one module's failure vocabulary from leaking into another.

**Wrapper public functions return `bool` by default; a project-defined `enum class` is used only when callers need to distinguish failure modes.** The enum represents the project's concerns, not a relabel of the library's error categories.
*Why:* `bool` is the simplest commitment; an enum is needed only when caller branches require it.

**On a failure path, the wrapper logs the library's error code at ERROR via `ESP_LOGE`** in the `keyword key=value` shape (e.g. `i2c_init_failed reason=ESP_ERR_TIMEOUT`), then returns the converted result.
*Why:* the library error code is the diagnostic information `bool` discards; logging at the failure path preserves it.

**Wrappers convert and return; they do not retry, count, or absorb failures.** Those decisions belong to the caller.
*Why:* the caller knows its role in the system; the wrapper doesn't.

**Failures in task code are logged and recovered from; they do not panic or reboot the node.** Recovery means an early return from the current iteration, an updated counter, and the task continuing on its next trigger.
*Why:* a deployed sensor node must not reboot on a transient I²C glitch, a missed mesh sync, or a momentary broker disconnect.

**`ESP_ERROR_CHECK` is allowed only inside `setup()` and only for failures that genuinely make further execution impossible.** In task code it is not used.
*Why:* its panic-and-reboot behavior is acceptable only when the firmware has not yet entered steady state.

## Section 3 — Logging

**All logs use the ESP-IDF `ESP_LOGx` macros.** Each module defines `static const char* TAG = "<module>";` at the top of its `.cpp` file and uses that tag in every log call.
*Why:* per-module tags let a log parser group events by source.

**`init_*` functions log their internal progress at INFO level.** Each significant configuration step emits an `ESP_LOGI` line; prose is acceptable since these are one-shot boot announcements, not data-bearing metrics.
*Why:* the boot log is the only window into startup behavior; uninstrumented inits hide failures until they manifest later.

**One event produces one log line.** Multi-line log blocks, banner art, and pretty-printed structures are not used.
*Why:* log analysis is line-oriented; multi-line outputs require bespoke parsing per call site.

**Data-bearing log lines follow a fixed shape:** the module's `TAG`, then a single event keyword, then zero or more `key=value` fields separated by spaces. The keyword and field names are stable across runs. One-shot boot announcements may use prose.
*Why:* a fixed shape lets a single parser script extract metrics without per-line regex.

**Timestamps inside log message content come from `esp_timer_get_time()`.** Mixed time sources (`millis`, RTOS ticks, NTP) are not used inside log content. The `ESP_LOGx` framework auto-prefix is a separate concern and is not modified.
*Why:* analysis is only valid when all events sit on the same monotonic clock; the framework's tick prefix is informational only.

**Each task maintains a small set of named counters** counting what its job does. Counters are incremented at the relevant event and emitted in a periodic data-bearing log line by their owning task.
*Why:* metrics analysis depends on counters, not anecdotes.

## Section 4 — Callbacks

**Callback bodies push event data onto a queue and return.** No logging, decoding, routing, blocking calls, heap allocation, or mutex use inside a callback. The consuming task does the work.
*Why:* the calling context is paused for the callback's full duration; long callbacks stall its other work and break timing predictability.

**Kernel API calls inside a callback use the variant appropriate for the calling context.** From ISR context, use `*FromISR` variants (`xQueueSendFromISR`, `xTaskNotifyFromISR`); from task context, use the regular variants.
*Why:* regular FreeRTOS APIs may take internal locks and are undefined behavior from an ISR.

**Callbacks that must return a value to the library decide inline from fast, local state** — only data passed to them and module-local state safe to read without synchronization. The defer pattern does not apply.
*Why:* the library is waiting for the answer; it cannot be deferred to a task.

## Section 5 — Naming, formatting, file layout

**Source layout mirrors the project's phase structure.** New code for each phase lives in its own file or folder under `src/`, named for the phase or feature.
*Why:* the source tree's shape matches the project plan; any phase's code can be located by name.

**`main.cpp` is the wiring layer only.** It calls each module's `init_*()` function in order, creates the firmware's long-lived tasks, and contains no feature logic.
*Why:* keeps the firmware's structure readable at a glance from a single file.

**Mechanical formatting** — whitespace, braces, line breaks, pointer alignment, include ordering — is enforced by `clang-format` using Google's preset as the baseline. A `.clang-format` file lives at the project root; brace placement, line length, and similar rules are delegated to that file and not restated here.
*Why:* Google's preset has the strongest ecosystem support across IDEs and AI tooling, which reduces friction for a single developer.

**This document does not adopt Google's full C++ Style Guide.** Only the mechanical formatting preset is borrowed. Naming, paradigm, design rules, and library use in this document supersede anything Google's broader guide would otherwise prescribe.
*Why:* a style guide is written for its authoring organization's problems; this project's own naming and design choices are made for embedded-firmware reasons and must take precedence.

**Naming inside your own code:** `snake_case` for free functions, local variables, and module-scope state; `PascalCase` for types and `enum class` definitions; `UPPER_SNAKE_CASE` for `constexpr` constants and enumerators.
*Why:* `snake_case` matches the surrounding ecosystem the code calls into most often (ESP-IDF, FreeRTOS, the C standard library) and keeps own-code identifiers visually distinct from the `PascalCase` and `camelCase` of C++ libraries the project consumes.

**At library boundaries, call the library using the spelling the library publishes.** Library `PascalCase` or `camelCase` method names are written as the library defines them; no local rename layer is introduced to relabel them.
*Why:* a library's published API spelling is a fact you cannot change at a call site, and wrapping every call to relabel it adds an indirection layer with no benefit.

## Section 6 — What this standard deliberately omits

This standard does not require MISRA-C compliance, unit-testing coverage, Doxygen comments on every function, or numeric code-coverage targets. MISRA-C is overkill for thesis-scale or hobby-scale firmware and consumes timeline without producing defensible value. Unit testing is omitted because hardware testing is the primary verification path. Doxygen on every function adds friction without payoff at this scope; comments are written where the reason behind the code is not obvious from the code itself. Coverage targets imply a test infrastructure that is not in scope.

Fine-grained formatting and mechanical style rules are also omitted from this document; they are delegated to `clang-format`. If a rule cannot be enforced by either `clang-format` or a one-paragraph human review, it does not belong here.
