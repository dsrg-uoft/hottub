#include "utilities/clinit_analysis.hpp"
#include "precompiled.hpp"
#include "interpreter/bytecodeStream.hpp"
#include "interpreter/linkResolver.hpp"

ClinitAnalysis::ClinitAnalysis(InstanceKlass *ik, Thread *thread) {
  assert(thread == Thread::current(), "sanity check");
  THREAD = thread;
  start_ik = ik;
}

bool ClinitAnalysis::ik_visit_contains(InstanceKlass *this_ik) {
  StackIterator<InstanceKlass*, mtInternal> iter(ik_visit_stack);
  while (!iter.is_empty()) {
    InstanceKlass **ik = iter.next_addr();
    if (this_ik == *ik) {
      return true;
    }
  }
  return false;
}

bool ClinitAnalysis::method_visit_contains(Method *this_m) {
  StackIterator<Method*, mtInternal> iter(method_visit_stack);
  while (!iter.is_empty()) {
    Method **m = iter.next_addr();
    if (this_m == *m) {
      return true;
    }
  }
  return false;
}

bool ClinitAnalysis::push_ik(InstanceKlass *ik) {
  if (ik->re_init_safe() && !ik->re_init && !ik_visit_contains(ik)) {
    ik_stack.push(ik);
    ik_visit_stack.push(ik);
    return true;
  } else {
    return false;
  }
}

// return true when super is successfully added to stack
bool ClinitAnalysis::push_super(InstanceKlass *this_ik) {
  InstanceKlass* super_ik = InstanceKlass::cast(this_ik->super());
  if (super_ik != NULL && !this_ik->is_interface() && push_ik(super_ik)) {
    if (ForkJVMLog) {
      ResourceMark rm;
      tty->print("[hottub][trace][ClinitAnalysis::push_super] class = %s\n",
          super_ik->name()->as_C_string());
    }
    return true;
  } else {
    return false;
  }
}

// return true when super is successfully added to stack
bool ClinitAnalysis::push_super_interface(InstanceKlass *this_ik) {
  if (this_ik->has_default_methods()) {
    for (int i = 0; i < this_ik->local_interfaces()->length(); ++i) {
      Klass* iface = this_ik->local_interfaces()->at(i);
      InstanceKlass* ik = InstanceKlass::cast(iface);
      // recursive case: need to add super first if it has default methods
      if (ik->has_default_methods()) {
        if (push_super_interface(ik)) {
          return true;
        }
      }
      if (push_ik(ik)) {
        if (ForkJVMLog) {
          ResourceMark rm;
          tty->print("[hottub][trace][ClinitAnalysis::push_super_interface] "
              "class = %s\n", ik->name()->as_C_string());
        }
        return true;
      }
    }
  }
  return false;
}

// 1. add seen references directly to ik_stack
//    - we don't worry about order as it cannot be correct without runtime info
// 2. add method invokes to method_stack
// TODO other types of invoke / get / put
void ClinitAnalysis::analyze_method(Method *this_m) {
  if (ForkJVMLog) {
    ResourceMark rm;
    tty->print("[hottub][trace][ClinitAnalysis::analyze_method] method = %s\n",
        this_m->name_and_sig_as_C_string());
  }
  HandleMark hm;

  methodHandle this_mh(THREAD, this_m);
  BytecodeStream bcs(this_mh);
  Bytecodes::Code c;
  while ((c = bcs.next()) >= 0) {

    if (c == Bytecodes::_getstatic ||
        c == Bytecodes::_putstatic) {
      constantPoolHandle pool(this_mh()->constants());
      int index = bcs.get_index_u2_cpcache();

      Klass *k = pool->klass_ref_at(index, CHECK);
      KlassHandle kh(THREAD, k);
      InstanceKlass *ik = InstanceKlass::cast(kh());

      // reference found
      bool result = push_ik(ik);
      if (ForkJVMLog && result) {
        ResourceMark rm;
        tty->print("[hottub][trace][ClinitAnalysis::analyze_method] "
            "get/putstatic = %s\n", ik->name()->as_C_string());
      }

    } else if (c == Bytecodes::_invokestatic) {
      constantPoolHandle pool(this_mh()->constants());
      int index = bcs.get_index_u2_cpcache();

      CallInfo call_info;
      LinkResolver::resolve_invokestatic(call_info, pool, index, THREAD);

      KlassHandle kh = call_info.resolved_klass();
      methodHandle mh = call_info.resolved_method();

      if (!kh.is_null() && !mh.is_null()) {
        InstanceKlass *ik = InstanceKlass::cast(kh());
        if (push_ik(ik)) {
          method_stack.push(mh());
          method_visit_stack.push(mh());
          if (ForkJVMLog) {
            ResourceMark rm;
            tty->print("[hottub][trace][ClinitAnalysis::analyze_method] "
                "invokestatic = %s\n", mh()->name_and_sig_as_C_string());
          }
        }
      }
    }
  }
}

void ClinitAnalysis::run_analysis() {

  // to initialize a class must:
  // 1. initialize all super classes (recursive)
  //    - peek stack, add super to stack, continue to super
  // 2. initialize all super interfaces (recursive)
  //    - same as super
  // 3. initialize all classes referenced by clinit
  //    - will only reach this after all supers and super interfaces are done
  if (ForkJVMLog) {
    ResourceMark rm;
    tty->print("[hottub][info][ClinitAnalysis::run_analysis] start! "
        "class = %s\n", start_ik->name()->as_C_string());
  }

  ik_stack.push(start_ik);
  ik_visit_stack.push(start_ik);

  while (!ik_stack.is_empty()) {
    InstanceKlass *ik = ik_stack.pop();
    ik_stack.push(ik);
    assert(ik->re_init_safe() && !ik->re_init, "bad InstanceKlass");
    if (ForkJVMLog) {
      ResourceMark rm;
      tty->print("[hottub][trace][ClinitAnalysis::run_analysis] analyzing "
          "class = %s\n", ik->name()->as_C_string());
    }

    // step 1: super
    if (push_super(ik)) {
      continue;
    }

    // step 2: super interfaces
    if (push_super_interface(ik)) {
      continue;
    }

    clinit_stack.push(ik);
    // need to pop here as analyze_method will push to ik_stack
    ik_stack.pop();

    // step 3: trace clinit
    Method *clinit = ik->class_initializer();
    if (clinit != NULL) {
      method_stack.push(clinit);
      method_visit_stack.push(clinit);

      while (!method_stack.is_empty()) {
        Method *m = method_stack.pop();
        analyze_method(m);
      }
    }
  }
}

void ClinitAnalysis::run_clinits() {
  // run clinit
  while (!clinit_stack.is_empty()) {
    InstanceKlass *ik = clinit_stack.pop();

    if (ForkJVMLog) {
      ResourceMark rm;
      tty->print("[hottub][info][ClinitAnalysis::run_clinits] class "
          "clinit = %s\n", ik->name()->as_C_string());
    }

    ik->call_class_initializer(THREAD);
    ik->re_init = true;

    if (HAS_PENDING_EXCEPTION) {
      ResourceMark rm(THREAD);
      tty->print("[hottub][error][ClinitAnalysis::run_clinits] "
          "clinit exception: class = %s, exception = %s\n",
          ik->name()->as_C_string(), PENDING_EXCEPTION->print_value_string());
      CLEAR_PENDING_EXCEPTION;
    }
  }
}

void ClinitAnalysis::print_method(methodHandle mh, TRAPS) {
  assert(mh() != NULL, "no null methods");

  ResourceMark rm;
  tty->print("[hottub][info][ClinitAnalysis::print_method] printing: %s\n",
      mh()->name_and_sig_as_C_string());

  BytecodeStream bcs(mh);
  Bytecodes::Code c;
  while ((c = bcs.next()) >= 0) {
    tty->print("%d %s\n", bcs.bci(), Bytecodes::name(c));
    if (//c == Bytecodes::_getfield ||
        //c == Bytecodes::_putfield ||
        c == Bytecodes::_getstatic ||
        c == Bytecodes::_putstatic) {

      constantPoolHandle pool(mh()->constants());
      int index = bcs.get_index_u2_cpcache();

      Klass* k = pool->klass_ref_at(index, CHECK);
      KlassHandle kh(THREAD, k);
      if (kh.is_null()) {
        tty->print("[hottub][info][ClinitAnalysis::print_method] %s kh NULL\n",
            Bytecodes::name(c));
      } else {
        tty->print("[hottub][info][ClinitAnalysis::print_method] field klass: %s\n",
            kh()->name()->as_C_string());
        InstanceKlass* ik = InstanceKlass::cast(kh());
        tty->print("[hottub][info][ClinitAnalysis::print_method] field instance klass: %s\n",
            ik->name()->as_C_string());
      }

    } else if (c == Bytecodes::_invokestatic) {
      constantPoolHandle pool(mh()->constants());
      int index = bcs.get_index_u2_cpcache();

      CallInfo fail_mary;
      LinkResolver::resolve_invokestatic(fail_mary, pool, index, THREAD);

      KlassHandle resolved_kh = fail_mary.resolved_klass();
      methodHandle resolved_mh = fail_mary.resolved_method();

      tty->print("[hottub][info][ClinitAnalysis::print_method] invoke static klass: %s\n",
          resolved_kh->name()->as_C_string());
      tty->print("[hottub][info][ClinitAnalysis::print_method] invoke static method: %s\n",
          resolved_mh()->name_and_sig_as_C_string());
    }
  }
}
