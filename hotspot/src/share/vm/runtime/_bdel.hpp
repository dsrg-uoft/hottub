#ifndef SHARE_VM_RUNTIME__BDEL_HPP
#define SHARE_VM_RUNTIME__BDEL_HPP

/*
 * Cpp source will be in `share/vm/sharedRuntime.cpp`
 */

#include <stdint.h>
#include <time.h>
#include <sys/syscall.h>

#define _likely(x) __builtin_expect((x), 1)
#define _unlikely(x) __builtin_expect((x), 0)

#define _bdel_sys_gettid() ((int64_t) syscall(SYS_gettid))
#define _MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * Notes
 * =====
 * Hmmm.
 *
 * ## i2c stack
 * We patch the return address for compiled calls in i2c to point to our handler so we know when the function finishes.
 * We need to save the "real" return address so we know where to jump back to,
 * or what to repatch/unpatch the return address to in case of deoptimization or exception thrown.
 * We also save the location we placed the return address (for verification purposes).
 *
 * ## Transition stack
 * On function return, we need to transition back to the caller's state.
 * Technically, we should have compiled method returns, but sometimes we see two i2cs...
 * So, we treat interpreter entry, i2c, osr migration begin, and native wrapper, and n2i
 * only as entries, making no assumptions about the previous state.
 *
 * ## Marking activations in safepoints
 * At a safepoint, the JVM scans the stack, tracing call frames via rbp and building a call stack from return addresses.
 * Before it does this, we need to unpatch the return addresses and (re)patch them afterwards,
 * so it knows what nmethods are active.
 *
 * ## TODO
 * - race conditions for safepoints
 *
 * Hmmm.
 */

uint64_t _now();
void _bdel_knell(const char*);

void _jvm_transitions_clock(JavaThread*, int8_t);

typedef struct {
  void* rax;
  void* rdx;
} _rax_rdx;

extern "C" {
  void* _i2c_ret_push(JavaThread*, void*, void*, Method*);
  _rax_rdx _i2c_ret_pop(JavaThread*);
  void* _i2c_ret_verify_location_and_pop(JavaThread*, void*);
  void _i2c_ret_handler();
  void _i2c_unpatch(JavaThread*, const char*);
  void _i2c_repatch(JavaThread*, const char*);
}
extern "C" {
  void* _c2i_ret_push(JavaThread*, void*, void*, Method*);
  _rax_rdx _c2i_ret_pop(JavaThread*, int);
  void* _c2i_ret_verify_location_and_pop(JavaThread*, void*, int);
  void _c2i_ret_handler();
  void _c2i_unpatch(JavaThread*, const char*);
  void _c2i_repatch(JavaThread*, const char*);
}
extern "C" {
  void _native_call_begin(JavaThread*, Method*, int);
  void _native_call_end(JavaThread*, Method*, int);
}

extern "C" {
  void _i2c_dump_stack();
  void _i2c_verify_stack();
  void _c2i_dump_stack(JavaThread*);
  void _c2i_verify_stack(JavaThread*);
}

extern "C" {
  void _noop10();
  void _noop11();
}

#endif // SHARE_VM_RUNTIME__BDEL_HPP
