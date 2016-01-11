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

class Method;

extern volatile uint64_t _i_total;
extern volatile uint64_t _c_total;
/*
 * - 0 for interpreted
 * - 1 for compiled
 * - 2 for native (jni) TODO
 */
extern __thread int8_t _jvm_state;

extern __thread uint64_t _i_timestamp;
extern __thread uint64_t _c_timestamp;
extern __thread uint64_t _i_counter;
extern __thread uint64_t _c_counter;

uint64_t _now();
void _bdel_knell(const char*);

void _i2c_entry(JavaThread*, Method*);

void _i2c_ret_push(void*, Method*);
void* _i2c_ret_pop();
void _i2c_ret_handler(JavaThread*);

void _print_value(JavaThread*, void*);


#endif // SHARE_VM_RUNTIME__BDEL_HPP
