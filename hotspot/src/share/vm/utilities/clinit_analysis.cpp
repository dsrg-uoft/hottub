#include "utilities/clinit_analysis.hpp"
#include "precompiled.hpp"
#include "interpreter/linkResolver.hpp"

GrowableArray<Method*>* ClinitAnalysis::visited_method_set = NULL;

// TODO: double check resource mark cleans this...
void ClinitAnalysis::initialize() {
  visited_method_set = new GrowableArray<Method*>();
}

ClinitAnalysis::ClinitAnalysis(Thread *thread) {
  assert(thread == Thread::current(), "sanity check");
  THREAD = thread;
}

int ClinitAnalysis::Implementations::length() {
  int klen = klasses.length();
  int mlen = methods.length();
  assert(klen == mlen, "must be 1:1");
  return klen;
}

bool ClinitAnalysis::Implementations::contains(InstanceKlass *ik, Method *m) {
  for (int i = 0; i < length(); i++) {
    if (klasses.at(i) == ik && methods.at(i) == m) {
      return true;
    }
  }
  return false;
}

bool ClinitAnalysis::Implementations::append_if_missing(InstanceKlass *ik, Method *m) {
  if (!contains(ik, m)) {
    klasses.append(ik);
    methods.append(m);
    return true;
  } else {
    return false;
  }
}

/*
// return true when super is successfully added to stack
bool ClinitAnalysis::push_super(InstanceKlass *this_ik) {
  InstanceKlass* super_ik = InstanceKlass::cast(this_ik->super());
  if (super_ik != NULL && !this_ik->is_interface() && push_ik(super_ik)) {
    if (HotTubLog) {
      tty->print("[HotTub][trace][ClinitAnalysis::push_super] class = %s\n",
          super_ik->name()->as_C_string());
    }
    return true;
  } else {
    return false;
  }
}

// return true when super is successfully added to stack
bool ClinitAnalysis::push_super_interfaces(InstanceKlass *this_ik) {
  if (this_ik->has_default_methods()) {
    for (int i = 0; i < this_ik->local_interfaces()->length(); ++i) {
      Klass* iface = this_ik->local_interfaces()->at(i);
      InstanceKlass* ik = InstanceKlass::cast(iface);
      // recursive case: need to add super first if it has default methods
      if (ik->has_default_methods()) {
        if (push_super_interfaces(ik)) {
          return true;
        }
      }
      if (push_ik(ik)) {
        if (HotTubLog) {
          tty->print("[HotTub][trace][ClinitAnalysis::push_super_interfaces] "
              "class = %s\n", ik->name()->as_C_string());
        }
        return true;
      }
    }
  }
  return false;
}
*/

void ClinitAnalysis::call_clinit(InstanceKlass *ik) {
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::call_clinit] ik = %s\n",
        ik->name()->as_C_string());
  }
  ik->call_class_initializer(THREAD);
  ik->reinit = true;

  if (HAS_PENDING_EXCEPTION) {
    tty->print("[HotTub][error][ClinitAnalysis::call_clinit] "
        "clinit exception: class = %s, exception = %s\n",
        ik->name()->as_C_string(), PENDING_EXCEPTION->print_string());
    CLEAR_PENDING_EXCEPTION;
  }
}

void ClinitAnalysis::handle_get_put(constantPoolHandle pool, BytecodeStream *bcs) {
  assert(pool.not_null(), "null pool");
  assert(bcs, "null bytecode stream");
  assert(bcs->code() == Bytecodes::_getstatic || bcs->code() == Bytecodes::_putstatic ||
         bcs->code() == Bytecodes::_getfield || bcs->code() == Bytecodes::_putfield,
         "not get/put static/field");

  int index = bcs->get_index_u2_cpcache();
  // inline LinkResolver::resolve_klass because private
  Klass* k = pool->klass_ref_at(index, CHECK);
  InstanceKlass *ik = InstanceKlass::cast(k);

  if (!ik) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_get_put] (%s) [%d]%s = null\n",
        bcs->method()->name_and_sig_as_C_string(), bcs->bci(), Bytecodes::name(bcs->code()));
    return;
  }
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::handle_get_put] (%s) [%d]%s = %s\n",
        bcs->method()->name_and_sig_as_C_string(), bcs->bci(),
        Bytecodes::name(bcs->code()), ik->name()->as_C_string());
  }

  // TODO: REMOVE ME?
  if (HAS_PENDING_EXCEPTION) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_get_put] (%s)"
        "[%d]%s exception = %s\n", bcs->method()->name_and_sig_as_C_string(),
        bcs->bci(), Bytecodes::name(bcs->code()), PENDING_EXCEPTION->print_string());
    CLEAR_PENDING_EXCEPTION;
  }

  if (ik->should_reinit() && visited_method_set->append_if_missing(ik->class_initializer())) {
    analyze(ik);
  }
}

void ClinitAnalysis::find_implementations(Implementations *impls, InstanceKlass *this_ik,
    Symbol *method_name, Symbol *method_signature) {
  // - if the class is a system class pretty much everything might implement it...
  // - this will try to run clinit on almost everything even though almost all
  // implementations aren't actually possible
  // - we would need to try and detect what the actual possible implementations are...
  if (!this_ik->class_loader()) {
    return;
  }

  if (this_ik->child_set != NULL) {
    for (int i = 0; i < this_ik->child_set->length(); i++) {
      InstanceKlass *child_ik = this_ik->child_set->at(i);
      find_implementations(impls, child_ik, method_name, method_signature);
    }
  } else {
    tty->print("[HotTub][error][ClinitAnalysis::find_implementations] "
        "this_ik = %s method_name = %s method_signature = %s child_set = null\n",
        this_ik->name()->as_C_string(), method_name->as_C_string(),
        method_signature->as_C_string());
  }

  methodHandle result_mh;
  KlassHandle this_kh(THREAD, this_ik);
  LinkResolver::lookup_method_in_klasses(result_mh, this_kh, method_name,
      method_signature, true, false, CHECK);

  // TODO: REMOVE ME?
  if (HAS_PENDING_EXCEPTION) {
    tty->print("[HotTub][error][ClinitAnalysis::find_implementations] "
        "this_ik = %s method_name = %s method_signature = %s exception = %s\n",
        this_ik->name()->as_C_string(), method_name->as_C_string(),
        method_signature->as_C_string(), PENDING_EXCEPTION->print_string());
    CLEAR_PENDING_EXCEPTION;
  }
  if (result_mh.is_null()) {
    tty->print("[HotTub][error][ClinitAnalysis::find_implementations] "
        "this_ik = %s method_name = %s method_signature = %s result_mh = null\n",
        this_ik->name()->as_C_string(), method_name->as_C_string(),
        method_signature->as_C_string());
    return;
  }
  if (HotTubLog) { //TODO make trace flag
    tty->print("[HotTub][trace][ClinitAnalysis::find_implementations] "
        "this_ik = %s method_name = %s method_signature = %s result_mh = %s "
        "mholder = %s\n", this_ik->name()->as_C_string(),
        method_name->as_C_string(), method_signature->as_C_string(),
        result_mh()->name_and_sig_as_C_string(),
        result_mh()->method_holder()->name()->as_C_string());
  }

  impls->append_if_missing(result_mh()->method_holder(), result_mh());
}

void ClinitAnalysis::handle_invoke_virtual_interface(constantPoolHandle pool,
    BytecodeStream *bcs) {
  assert(pool.not_null(), "null pool");
  assert(bcs, "null bytecode stream");
  assert(bcs->code() == Bytecodes::_invokevirtual ||
      bcs->code() == Bytecodes::_invokeinterface,
      "not invokevirtual or invokeinterface");

  int index = bcs->get_index_u2_cpcache();
  Klass* base_k = pool->klass_ref_at(index, CHECK);
  if (!base_k || !base_k->oop_is_instance()) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_invoke_virtual_interface]"
        " (%s) [%d]%s | base_k = %s oop_is_instance = %s\n",
        bcs->method()->name_and_sig_as_C_string(), bcs->bci(), Bytecodes::name(bcs->code()),
        base_k ? base_k->name()->as_C_string() : "null",
        base_k ? (base_k->oop_is_instance() ? "ture" : "false" ) : "null");
    return;
  }
  InstanceKlass *base_ik = InstanceKlass::cast(base_k);
  Symbol *method_name      = pool->name_ref_at(index);
  Symbol *method_signature = pool->signature_ref_at(index);

  if (!base_ik || !method_name || !method_signature) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_invoke_virtual_interface]"
        " (%s) [%d]%s | base_ik = %s method_name = %s method_signature = %s\n",
        bcs->method()->name_and_sig_as_C_string(), bcs->bci(), Bytecodes::name(bcs->code()),
        base_ik ? base_ik->name()->as_C_string() : "null",
        method_name ? method_name->as_C_string() : "null",
        method_signature ? method_signature->as_C_string() : "null");
    return;
  }
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::handle_invoke_virtual_interface]"
        " (%s) [%d]%s | base_ik = %s method_name = %s method_signature = %s\n",
        bcs->method()->name_and_sig_as_C_string(), bcs->bci(),
        Bytecodes::name(bcs->code()), base_ik->name()->as_C_string(),
        method_name->as_C_string(), method_signature->as_C_string());
  }

  Implementations impls;
  find_implementations(&impls, base_ik, method_name, method_signature);

  for (int i = 0; i < impls.length(); i++) {
    InstanceKlass *ik = impls.klasses.at(i);
    Method *m = impls.methods.at(i);
    // TODO re-evaluate
    // if the implementation (child) is unsafe we don't need to trace it
    // theoretically it is possible for a call path to be:
    //  user code -> system code -> user code
    // if the user code passed an object to the system code and the system code
    // uses it to invoke virtual/interface to call back into user code
    if (ik->should_reinit() &&
        visited_method_set->append_if_missing(ik->class_initializer())) {
      if (HotTubLog) {
        tty->print("[HotTub][trace][ClinitAnalysis::handle_invoke_virtual_interface]"
            " (%s) [%d]%s adding impl ik = %s m = %s\n",
            bcs->method()->name_and_sig_as_C_string(), bcs->bci(), Bytecodes::name(bcs->code()),
            ik->name()->as_C_string(), m->name_and_sig_as_C_string());
      }
      analyze(ik);
    }
    // system classes/methods cause issues with constant pool (array assert)
    // avoid by checking if the ik is reinit_safe
    if (ik->reinit_safe() && visited_method_set->append_if_missing(m)) {
      analyze(m);
    }
  }
}

void ClinitAnalysis::handle_invoke_static_special(constantPoolHandle pool,
    BytecodeStream *bcs) {
  assert(bcs->code() == Bytecodes::_invokestatic ||
         bcs->code() == Bytecodes::_invokespecial,
         "not invokestatic or invokespecial");

  int index = bcs->get_index_u2_cpcache();
  CallInfo call_info;
  if (bcs->code() == Bytecodes::_invokestatic) {
    LinkResolver::resolve_invokestatic(call_info, pool, index, THREAD);
  } else { // Bytecodes::_invokespecial
    LinkResolver::resolve_invokespecial(call_info, pool, index, THREAD);
  }
  KlassHandle kh = call_info.resolved_klass();
  methodHandle mh = call_info.resolved_method();

  // TODO: REMOVE ME?
  if (HAS_PENDING_EXCEPTION) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_invoke_static_special] "
        " (%s) [%d]%s exception = %s\n", bcs->method()->name_and_sig_as_C_string(),
        bcs->bci(), Bytecodes::name(bcs->code()),
        PENDING_EXCEPTION->print_string());
    CLEAR_PENDING_EXCEPTION;
  }

  if (kh.is_null() || mh.is_null()) {
    tty->print("[HotTub][error][ClinitAnalysis::handle_invoke_static_special] "
        " (%s) [%d]%s kh = %s mh = %s\n", bcs->method()->name_and_sig_as_C_string(),
        bcs->bci(), Bytecodes::name(bcs->code()),
        kh.is_null() ? "null" : kh->name()->as_C_string(),
        mh.is_null() ? "null" : mh->name_and_sig_as_C_string());
    return;
  }
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::handle_invoke_static_special]"
        " (%s) [%d]%s = %s\n", bcs->method()->name_and_sig_as_C_string(), bcs->bci(),
        Bytecodes::name(bcs->code()), mh()->name_and_sig_as_C_string());
  }

  InstanceKlass *ik = InstanceKlass::cast(kh());
  if (ik->should_reinit() &&
      visited_method_set->append_if_missing(ik->class_initializer())) {
    analyze(ik);
  }
  // system classes/methods cause issues with constant pool (array assert)
  // avoid by checking if the ik is reinit_safe
  if (ik->reinit_safe() && visited_method_set->append_if_missing(mh())) {
    analyze(mh());
  }
}

// TODO: implement more bytecodes (get/putfield, invokevirtual...)
// TODO: don't use linkresolver? does it initialize classes itself?
//       we might be causing unnecessary linking/loading...
// return false if analyze didn't run and true if it did
void ClinitAnalysis::analyze(Method *this_m) {
  assert(this_m, "null method");
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::analyze] start = %s\n",
        this_m->name_and_sig_as_C_string());
  }

  methodHandle this_mh(THREAD, this_m);
  constantPoolHandle pool(this_mh()->constants());
  BytecodeStream bcs(this_mh);
  Bytecodes::Code c;
  while ((c = bcs.next()) >= 0) {
    switch(c) {
      case Bytecodes::_getstatic:
      case Bytecodes::_putstatic:
      case Bytecodes::_getfield:
      case Bytecodes::_putfield:
        handle_get_put(pool, &bcs);
        break;
      case Bytecodes::_invokestatic:
      case Bytecodes::_invokespecial:
        handle_invoke_static_special(pool, &bcs);
        break;
      case Bytecodes::_invokevirtual:
      case Bytecodes::_invokeinterface:
        handle_invoke_virtual_interface(pool, &bcs);
        break;
    }
  }
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::analyze] end = %s\n",
        this_m->name_and_sig_as_C_string());
  }
}

void ClinitAnalysis::analyze(InstanceKlass *ik) {
  assert(ik->should_reinit(), "shouldn't reinit ik");
  analyze(ik->class_initializer());
  call_clinit(ik);
}

void ClinitAnalysis::run(InstanceKlass *ik, TRAPS) {
  assert(ik->should_reinit(), "shouldn't reinit ik");
  if (HotTubLog) {
    tty->print("[HotTub][trace][ClinitAnalysis::run] start! "
        "%s\n", ik->name()->as_C_string());
  }
  assert(!visited_method_set->contains(ik->class_initializer()), "already seen");
  visited_method_set->append(ik->class_initializer());
  ClinitAnalysis clinit_analysis(THREAD);
  clinit_analysis.analyze(ik);
}
