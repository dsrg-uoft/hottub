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

/*
 * Scenario:
 * - in compiled code
 * - call interpreted method
 * - `_jvm_state` set to interpreted
 * - method exits, need to set `_jvm_state` back to compiled
 * - but suppose interpreted method calls compiled method
 * - that compiled method may call another interpreted method
 * - hmmm...
 */

extern volatile uint64_t _i_total;
extern volatile uint64_t _c_total;

uint64_t _now();
void _bdel_knell(const char*);

typedef struct {
  void* rax;
  void* rdx;
} _rax_rdx;

extern "C" {
  void _i2c_ret_push(JavaThread*, void*, Method*);
  _rax_rdx _i2c_ret_pop(JavaThread*);
  void* _i2c_ret_verify_location_and_pop(JavaThread*, void*);
  void _i2c_ret_handler();
  void _native_call_begin(JavaThread*, Method*, int);
  void _native_call_end(JavaThread*, Method*, int);
}

extern "C" {
  void _dump_i2c_stack();
  void _i2c_verify_stack();
}

/*
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

extern "C" void _saw_uncommon_trap();
extern "C" void _deopt_blob_start();
extern "C" void _deopt_blob_exception_case();
extern "C" void _deopt_blob_normal();
extern "C" void _deopt_blob_test(void*);
extern "C" void _deopt_verified(void*);

extern "C" void _saw_safepoint_return_handler();
extern "C" void _saw_call_stub();
extern "C" void _saw_call_stub2();

*/

#endif // SHARE_VM_RUNTIME__BDEL_HPP
