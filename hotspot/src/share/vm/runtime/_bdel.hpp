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
 * - 2 for native/JNI
 */
extern __thread int8_t _jvm_state;
extern __thread int8_t _bdel_is_java_thread;

extern __thread uint64_t _i_timestamp;
extern __thread uint64_t _c_timestamp;
extern __thread uint64_t _i_counter;
extern __thread uint64_t _c_counter;

uint64_t _now();
void _bdel_knell(const char*);

typedef struct {
  void* rax;
  void* rdx;
} _rax_rdx;

extern "C" {
  void _i2c_ret_push(void*, Method*);
  _rax_rdx _i2c_ret_pop();
  _rax_rdx _i2c_ret_verify_and_pop();
  _rax_rdx _i2c_ret_verify_location_and_pop(void*);
  void* _i2c_ret_peek();
  void _i2c_ret_handler();

  void _i2c_ret_badness();
  void _dump_i2c_stack();
  void _i2c_verify_stack();

  void _i2c_pop_nil();

  void _native_call_begin();
  void _native_call_end();
}

void _print_value(JavaThread*, void*);


void _noop();
void _noop2();
void _noop3();
void _noop4();
void _noop5();
void _noop10();

extern "C" void _noop11();
extern "C" void _noop12();
extern "C" void _noop13();
extern "C" void _noop14();
extern "C" void _noop15();
extern "C" void _noop16();
extern "C" void _noop20();
extern "C" void _noop21();
extern "C" void _noop30();
extern "C" void _noop31();
extern "C" void _noop32();
extern "C" void _noop33(void*);
extern "C" void _noop40();
extern "C" void _noop41();

extern "C" void _print_method(Method*);

extern "C" void _saw_uncommon_trap();
extern "C" void _deopt_blob_start();
extern "C" void _deopt_blob_exception_case();
extern "C" void _deopt_blob_normal();
extern "C" void _deopt_blob_test(void*);
extern "C" void _deopt_verified(void*);

extern "C" void _saw_safepoint_return_handler();
extern "C" void _saw_call_stub();
extern "C" void _saw_call_stub2();

#endif // SHARE_VM_RUNTIME__BDEL_HPP
