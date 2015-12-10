

#ifndef SHARE_VM_RUNTIME__BDEL_HPP
#define SHARE_VM_RUNTIME__BDEL_HPP

/*
 * Cpp source will be in `share/vm/sharedRuntime.cpp`
 */

#include <sys/time.h>
#include <stdint.h>

extern volatile uint64_t _i_total;
extern volatile uint64_t _c_total;
/*
 * - 0 for interpreted
 * - 1 for compiled
 * - 2 for native (jni) TODO
 */
extern __thread uint8_t _jvm_state;
/*
 * Scenario:
 * - in compiled code
 * - call interpreted method
 * - `_jvm_state` set to interpreted
 * - method exits, need to set `_jvm_state` back to compiled
 * - suppose interpreted method calls compiled method
 * - hmmm...
 */
extern __thread uint8_t _i_from_c;
extern __thread uint32_t _i_levels;
extern __thread uint64_t _i_timestamp;
extern __thread uint64_t _c_timestamp;
extern __thread uint64_t _i_counter;
extern __thread uint64_t _c_counter;

int64_t _bdel_sys_gettid();
uint64_t _now();
void _bdel_knell(const char*);

#endif // SHARE_VM_RUNTIME__BDEL_HPP
