// WASM fibers on emscripten_fiber_* (requires -sASYNCIFY).
#ifdef __EMSCRIPTEN__

#include "fiber.h"

#include <cstdio>
#include <cstdlib>
#include <emscripten/fiber.h>

namespace sim {

struct Fiber {
    emscripten_fiber_t context;
    void* c_stack = nullptr;
    void* asyncify_stack = nullptr;
    FiberEntry entry = nullptr;
    void* arg = nullptr;
};

namespace {
emscripten_fiber_t g_main_context;
char g_main_asyncify_stack[256 * 1024];
bool g_main_initialised = false;

void trampoline(void* arg) {
    Fiber* fiber = static_cast<Fiber*>(arg);
    fiber->entry(fiber->arg);
    std::fprintf(stderr, "sim: fiber entry returned\n");
    std::abort();
}
}  // namespace

void fiber_init_main() {
    if (g_main_initialised) return;
    emscripten_fiber_init_from_current_context(&g_main_context, g_main_asyncify_stack,
                                               sizeof(g_main_asyncify_stack));
    g_main_initialised = true;
}

Fiber* fiber_create(FiberEntry entry, void* arg, size_t stack_bytes) {
    fiber_init_main();
    Fiber* fiber = new Fiber();
    fiber->entry = entry;
    fiber->arg = arg;
    fiber->c_stack = std::malloc(stack_bytes);
    const size_t asyncify_bytes = stack_bytes;
    fiber->asyncify_stack = std::malloc(asyncify_bytes);
    if (!fiber->c_stack || !fiber->asyncify_stack) {
        std::fprintf(stderr, "sim: fiber allocation failed\n");
        std::abort();
    }
    emscripten_fiber_init(&fiber->context, trampoline, fiber, fiber->c_stack, stack_bytes,
                          fiber->asyncify_stack, asyncify_bytes);
    return fiber;
}

void fiber_destroy(Fiber* fiber) {
    if (!fiber) return;
    std::free(fiber->c_stack);
    std::free(fiber->asyncify_stack);
    delete fiber;
}

void fiber_enter(Fiber* fiber) {
    emscripten_fiber_swap(&g_main_context, &fiber->context);
}

void fiber_leave(Fiber* self) {
    emscripten_fiber_swap(&self->context, &g_main_context);
}

}  // namespace sim

#endif
