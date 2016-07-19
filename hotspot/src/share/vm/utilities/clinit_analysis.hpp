#ifndef SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP
#define SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP

#include "memory/allocation.hpp"
#include "runtime/handles.hpp"
#include "utilities/stack.hpp"

class ClinitAnalysis: public StackObj {
private:
  Thread *THREAD;
  InstanceKlass *start_ik;
  Stack<Method*       , mtInternal> method_stack;
  Stack<Method*       , mtInternal> method_visit_stack;
  Stack<InstanceKlass*, mtInternal> ik_stack;
  Stack<InstanceKlass*, mtInternal> ik_visit_stack;
  Stack<InstanceKlass*, mtInternal> clinit_stack;

  bool ik_visit_contains(InstanceKlass *this_ik);
  bool method_visit_contains(Method *this_m);

  bool push_ik(InstanceKlass *this_ik);
  bool push_super(InstanceKlass *this_ik);
  bool push_super_interface(InstanceKlass *this_ik);

  void analyze_method(Method *m);

public:
  ClinitAnalysis(InstanceKlass *ik, Thread *thread);
  void run_analysis();
  void run_clinits();

  static void print_method(methodHandle mh, TRAPS);
};

#endif // SHARE_VM_UTILITIES_CLINIT_ANALYSIS_HPP
