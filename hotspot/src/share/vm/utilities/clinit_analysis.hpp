#ifndef SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP
#define SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP

#include "memory/allocation.hpp"
#include "runtime/handles.hpp"
#include "utilities/growableArray.hpp"
#include "interpreter/bytecodeStream.hpp"


class ClinitAnalysis: public StackObj {
private:
  class Implementations {
  public:
    GrowableArray<InstanceKlass*> klasses;
    GrowableArray<Method*> methods;
    int length();
    bool contains(InstanceKlass *ik, Method *m);
    bool append_if_missing(InstanceKlass *ik, Method *m);
  };

  Thread *THREAD;
  // having this static is an optimization
  // if we have already seen a method tracing into it again will not produce
  // any class not already initialized
  static GrowableArray<Method*> *visited_method_set;

  ClinitAnalysis(Thread *thread);
  void call_clinit(InstanceKlass *ik);
  void handle_get_put(constantPoolHandle pool, BytecodeStream *bcs);
  void handle_invoke_static_special(constantPoolHandle pool, BytecodeStream *bcs);
  void find_implementations(Implementations *impls, InstanceKlass *this_ik,
      Symbol *method_name, Symbol *method_signature);
  void handle_invoke_virtual_interface(constantPoolHandle pool, BytecodeStream *bcs);
  void analyze(Method *m);
  void analyze(InstanceKlass *ik);

public:
  static void initialize();
  static void run(InstanceKlass *ik, TRAPS);
};

#endif // SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP
