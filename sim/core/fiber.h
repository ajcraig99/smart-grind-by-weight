// Minimal fiber abstraction: ucontext natively, emscripten fibers (ASYNCIFY) in WASM.
// The scheduler only ever switches between the main context and one task fiber.
#pragma once

#include <cstddef>

namespace sim {

struct Fiber;

using FiberEntry = void (*)(void* arg);

// Initialise the main (scheduler) context. Call once before creating fibers.
void fiber_init_main();

// Create a fiber that runs entry(arg) on its own stack the first time it is switched to.
Fiber* fiber_create(FiberEntry entry, void* arg, size_t stack_bytes);

// Free a fiber that is not running.
void fiber_destroy(Fiber* fiber);

// Switch from the main context into `fiber`. Returns when the fiber switches back.
void fiber_enter(Fiber* fiber);

// Switch from the running fiber back to the main context.
void fiber_leave(Fiber* self);

}  // namespace sim
