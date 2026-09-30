// Native fibers on POSIX ucontext.
#ifndef __EMSCRIPTEN__

#include "fiber.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ucontext.h>

namespace sim {

struct Fiber {
    ucontext_t context;
    void* stack = nullptr;
    FiberEntry entry = nullptr;
    void* arg = nullptr;
};

namespace {
ucontext_t g_main_context;

// makecontext passes int arguments; split the pointer for portability.
void trampoline(int lo, int hi) {
    const uintptr_t bits = static_cast<uintptr_t>(static_cast<uint32_t>(lo)) |
                           (sizeof(uintptr_t) > 4
                                ? (static_cast<uintptr_t>(static_cast<uint32_t>(hi)) << 16 << 16)
                                : 0);
    Fiber* fiber = reinterpret_cast<Fiber*>(bits);
    fiber->entry(fiber->arg);
    // Entry functions never return (the scheduler marks the task deleted and leaves).
    std::fprintf(stderr, "sim: fiber entry returned\n");
    std::abort();
}
}  // namespace

void fiber_init_main() {}

Fiber* fiber_create(FiberEntry entry, void* arg, size_t stack_bytes) {
    Fiber* fiber = new Fiber();
    fiber->entry = entry;
    fiber->arg = arg;
    fiber->stack = std::malloc(stack_bytes);
    if (!fiber->stack || getcontext(&fiber->context) != 0) {
        std::fprintf(stderr, "sim: fiber allocation failed\n");
        std::abort();
    }
    fiber->context.uc_stack.ss_sp = fiber->stack;
    fiber->context.uc_stack.ss_size = stack_bytes;
    fiber->context.uc_link = nullptr;
    const uintptr_t bits = reinterpret_cast<uintptr_t>(fiber);
    const int lo = static_cast<int>(static_cast<uint32_t>(bits));
    const int hi = sizeof(uintptr_t) > 4 ? static_cast<int>(static_cast<uint32_t>(bits >> 16 >> 16)) : 0;
    makecontext(&fiber->context, reinterpret_cast<void (*)()>(trampoline), 2, lo, hi);
    return fiber;
}

void fiber_destroy(Fiber* fiber) {
    if (!fiber) return;
    std::free(fiber->stack);
    delete fiber;
}

void fiber_enter(Fiber* fiber) {
    swapcontext(&g_main_context, &fiber->context);
}

void fiber_leave(Fiber* self) {
    swapcontext(&self->context, &g_main_context);
}

}  // namespace sim

#endif
