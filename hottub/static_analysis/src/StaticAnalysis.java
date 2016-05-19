import java.io.BufferedReader;
import java.io.FileReader;

import java.util.List;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Stack;

import org.objectweb.asm.Opcodes;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.tree.ClassNode;
import org.objectweb.asm.tree.MethodNode;
import org.objectweb.asm.tree.FieldNode;
import org.objectweb.asm.tree.AbstractInsnNode;
import org.objectweb.asm.tree.InsnList;
import org.objectweb.asm.tree.FieldInsnNode;
import org.objectweb.asm.tree.MethodInsnNode;

public class StaticAnalysis implements Opcodes {

    static boolean log_enable = false;

    public static void log_ln (String s) {
            System.out.println("[StaticAnalysis]" + s);
    }
    public static void log_f (String format, Object... args) {
            System.out.printf("[StaticAnalysis]" + format, args);
    }

    // static statistics variables
    static int class_count = 0;
    static int clinit_count = 0;
    static int safe_classes = 0;
    static int bad_ref_field = 0;
    static int bad_ref_getstatic = 0;
    static int bad_ref_putstatic = 0;
    static int bad_ref_invokevirtual = 0;
    static int bad_ref_invokeinterface = 0;
    static int native_lib = 0;
    static int super_unsafe = 0;
    static int interface_unsafe = 0;
    static int dependence_unsafe = 0;
    static int error_unsafe = 0;
    static int bad_throw = 0;
    static int bad_invoke_dynamic = 0;

    static final HashSet<String> known_safe_list;
    static {
        known_safe_list = new HashSet<String>();
        known_safe_list.add("java/lang/String");
        known_safe_list.add("java/lang/System");
        known_safe_list.add("java/lang/ThreadGroup");
        known_safe_list.add("java/lang/Thread");
        known_safe_list.add("java/lang/Class");
        known_safe_list.add("java/lang/reflect/Method");
        known_safe_list.add("java/lang/ref/Finalizer");
        known_safe_list.add("java/lang/OutOfMemoryError");
        known_safe_list.add("java/lang/NullPointerException");
        known_safe_list.add("java/lang/ClassCastException");
        known_safe_list.add("java/lang/ArrayStoreException");
        known_safe_list.add("java/lang/ArithmeticException");
        known_safe_list.add("java/lang/StackOverflowError");
        known_safe_list.add("java/lang/IllegalMonitorStateException");
        known_safe_list.add("java/lang/IllegalArgumentException");
        known_safe_list.add("java/lang/Compiler");
        known_safe_list.add("java/lang/invoke/MethodHandle");
        known_safe_list.add("Ljava/lang/invoke/MemberName;");
        known_safe_list.add("java/lang/invoke/MethodHandleNatives");
    }

    // note: this implementation relies on each class having a unique name
    // set of loaded classes
    HashSet<ClassNode> loaded_class_set;
    // classname -> classnode
    HashMap<String, ClassNode> class_map;
    // classname + methodname + methoddescription -> methodnode
    HashMap<String, MethodNode> method_map;
    // classname -> is safe
    HashMap<String, Boolean> safe_map;
    // classnode -> referenced classnodes (list of dependencies for a class caused by references)
    HashMap<ClassNode, HashSet<ClassNode>> class_dependence_map;

    public StaticAnalysis() {
        loaded_class_set = new HashSet<ClassNode>();
        class_map = new HashMap<String, ClassNode>();
        method_map = new HashMap<String, MethodNode>();
        safe_map = new HashMap<String, Boolean>();
        class_dependence_map = new HashMap<ClassNode, HashSet<ClassNode>>();

        for (String class_name : known_safe_list)
            safe_map.put(class_name, true);
    }

    public void addClassDependence(ClassNode cn, ClassNode dep_cn) {
        if (!class_dependence_map.containsKey(cn))
            class_dependence_map.put(cn, new HashSet<ClassNode>());
        class_dependence_map.get(cn).add(dep_cn);
    }

    public void addClassDependence(ClassNode cn, String class_name) {
        ClassNode dep_cn = class_map.get(class_name);
        if (dep_cn != null)
            addClassDependence(cn, dep_cn);
        else
            log_ln("[error][fatal][cause:class_map] !class_name.get(class) class: " + class_name);
    }

    public static void main(String[] args) {

        if (args.length < 1) {
            log_ln("[error][fatal][main] requires classlist file");
            System.exit(1);
        } else if (args.length == 2 && args[1].equals("log")) {
            log_enable = true;
        }

        StaticAnalysis sa = new StaticAnalysis();

        // read classlist and populate maps
        sa.readClassList(args[0]);

        // check for safety of each class in the list
        sa.checkSafety();

        // output reslts
        sa.output();

        // update safe classes for stats
        if (log_enable)
            sa.printStats();
    }

    public ClassNode readClass(String class_name) {
        try {
            ClassReader cr = new ClassReader(class_name);
            ClassNode cn = new ClassNode();
            cr.accept(cn, ClassReader.SKIP_DEBUG);

            if (!class_map.containsKey(cn.name))
                class_map.put(cn.name, cn);
            else
                log_ln("[error][fatal][cause:class_map] name not unique: " + cn.name);

            for (int i = 0; i < cn.methods.size(); i++) {
                MethodNode mn = (MethodNode) cn.methods.get(i);
                String key = cn.name + mn.name + mn.desc;

                if (!method_map.containsKey(key))
                    method_map.put(key, mn);
                else
                    log_ln("[error][fatal][cause:method_map] name not unique: " + key);
            }
        } catch (Exception e) {
            log_ln("[error][fatal][cause:asm] class: " + class_name + " " + e);
        }
        return class_map.get(class_name);
    }

    public void readClassList(String classlistFile) {
        // read through the classlist and populate the maps
        try(BufferedReader br = new BufferedReader(new FileReader(classlistFile))) {
            for(String line; (line = br.readLine()) != null;) {
                ClassNode cn = readClass(line);
                if (cn != null)
                    loaded_class_set.add(cn);
            }
        } catch (Exception e) {
            log_ln("[error] couldn't read classlist file (probably wrong file name): "+e);
        }
    }

    public void checkSafety() {
        final String clinitConst = "<clinit>()V";

        // first phase handle classes as individuals
        for (ClassNode cn : loaded_class_set) {
            class_count++;

            boolean safe_class = false;
            String clinitKey = cn.name + clinitConst;
            MethodNode clinit = method_map.get(clinitKey);
            if (clinit != null) {

                HashSet<MethodNode> mn_visited = new HashSet<MethodNode>();
                Stack<MethodNode> mn_stack = new Stack<MethodNode>();
                mn_stack.push(clinit);

                /* peek at the start and pop only if the method is safe this
                 * way if you leave the loop with a non-empty stack you know
                 * class is not safe
                 */
                while (!mn_stack.empty()) {
                    MethodNode mn = mn_stack.peek();
                    HashSet<MethodNode> callees = new HashSet<MethodNode>();
                    // aslong as everything else in method is safe continue checking callees
                    boolean safe_method = walkMethod(cn, mn, callees);
                    if (!safe_method) {
                        break;
                    } else {
                        mn_visited.add(mn_stack.pop());
                        for (MethodNode callee : callees) {
                            /* just need to check each method once, unless a
                             * recursive call does nothing it should be unsafe
                             * by referecing non-constant data)
                             */
                            if (!mn_visited.contains(callee))
                                mn_stack.push(callee);
                        }
                    }
                }
                // made it to end of clinit code trace
                if (mn_stack.empty()) {
                    safe_class = true;
                }

                clinit_count++;
            } else {
                // no clinit no problem
                safe_class = true;
            }

            safe_map.put(cn.name, safe_class);
        } // first phase

        // second phase handle not safe super case
        for (ClassNode cn : loaded_class_set) {
            if (safe_map.get(cn.name)) {
                boolean safe = true;
                /* recursively check super
                 * - could keep a stack to set all safe children of unsafe
                 * parent, but that is a small optimization
                 */
                ClassNode super_cn = class_map.get(cn.superName);
                while (safe && super_cn != null) {
                    if (safe_map.containsKey(super_cn.name)) {
                        if (!safe_map.get(super_cn.name)) {
                            safe = false;
                            super_unsafe++;
                            if (log_enable)
                                log_ln("[unsafe] class: " + cn.name + " [cause:super] unsafe super: " + super_cn.name);
                        } else {
                            // this should only happen for java/lang/Object
                            if (super_cn.superName == null) {
                                super_cn = null;
                            } else {
                                if (class_map.containsKey(super_cn.superName)) {
                                    super_cn = class_map.get(super_cn.superName);
                                } else {
                                    if (known_safe_list.contains(cn.superName)) {
                                        super_cn = readClass(cn.superName);
                                        if (cn == null) {
                                            // something really bad is going on if these can't be read
                                            safe = false;
                                            error_unsafe++;
                                            log_ln("[error][unsafe] class: " + cn.name
                                                    + " [cause:super] cannot readclass safe class: " + cn.superName);
                                        }
                                    } else {
                                        safe = false;
                                        error_unsafe++;
                                        log_ln("[error][unsafe] class: " + cn.name
                                                + " [cause:super] !class_map.contains(super): " + super_cn.name);
                                    }
                                }
                            }
                        }
                    } else {
                        safe = false;
                        error_unsafe++;
                        log_ln("[error][unsafe] class: " + cn.name
                                + " [cause:super] !safe_map.contains(super): " + super_cn.name);
                    }
                }
                // check interfaces
                // if any interface is not safe this is not safe
                // don't ask me why cn.interfaces is an object...
                @SuppressWarnings("unchecked")
                List<String> interface_names = (List<String>)cn.interfaces;
                for (int i = 0; safe && i < interface_names.size(); i++) {
                    String interface_name = interface_names.get(i);
                    if (safe_map.containsKey(interface_name) && !safe_map.get(interface_name)) {
                        safe = false;
                        interface_unsafe++;
                        if (log_enable)
                            log_ln("[unsafe] class: " + cn.name+" [cause:interface] unsafe interface: " + interface_name);
                    }
                    /* if the safe map doesn't contain the interface it means
                     * the interface was never initialized, so we're good
                     */
                }
                if (!safe)
                    safe_map.put(cn.name, false);
            }
        } // second phase

        // third phase handle not safe dependence case
        boolean changed = true;
        while (changed) {
            changed = false;
            for (ClassNode cn : loaded_class_set) {
                if (safe_map.get(cn.name)) {
                    HashSet<ClassNode> dep_set = class_dependence_map.get(cn);
                    if (dep_set != null) {
                        boolean safe = true;
                        Iterator<ClassNode> dep_iter = dep_set.iterator();
                        while (safe && dep_iter.hasNext()) {
                            ClassNode dep_cn = dep_iter.next();
                            if (safe_map.containsKey(dep_cn.name)) {
                                if (!safe_map.get(dep_cn.name)) {
                                    safe = false;
                                    dependence_unsafe++;
                                }
                            } else {
                                safe = false;
                                error_unsafe++;
                                log_ln("[error][unsafe] class: " + cn.name
                                        + " [cause:dependence] !safe_map.contains(dependence): " + dep_cn.name);
                            }
                        }
                        if (!safe) {
                            changed = true;
                            safe_map.put(cn.name, false);
                        }
                    }
                }
            }
        } // third phase
    }

    /*
     * remember:
     *   local variables are safe:
     *     - clinit doesn't have any parameters to pass in at the start
     *     - if you want to put non-constant data (time sensitive data) into a
     *       local variable you will need to reference something non-local, aka
     *       a field from some other class (with that class already existing)
     */
    public boolean walkMethod(ClassNode clinit_cn, MethodNode mn, HashSet<MethodNode> method_callees) {
        boolean safe = true;
        InsnList insns = mn.instructions;

        for (int i = 0; safe && i < insns.size(); i++) {
            AbstractInsnNode ain = insns.get(i);

            switch (ain.getType()) {
            case AbstractInsnNode.FIELD_INSN:
                FieldInsnNode fin = (FieldInsnNode) ain;
                safe = handleField(clinit_cn, fin);
                break;

            case AbstractInsnNode.METHOD_INSN:
                MethodInsnNode min = (MethodInsnNode) ain;
                HashSet<MethodNode> invoke_callees = handleInvoke(clinit_cn, min);
                if (invoke_callees == null) {
                    safe = false;
                } else {
                    for (MethodNode callee : invoke_callees)
                        method_callees.add(callee);
                }
                break;

            // deals with java.lang.invoke.MethodHandle
            // I think this is like function pointer aka not safe
            case AbstractInsnNode.INVOKE_DYNAMIC_INSN:
                safe = false;
                bad_invoke_dynamic++;
                if (log_enable)
                    log_ln("[unsafe] class: " + clinit_cn.name + " [cause:invoke_dynamic]");
                break;

            case AbstractInsnNode.INSN:
                // throwing things can hurt people so not safe
                // most likely throw logic depends on a reference, so we should
                // already handle this implicitly, but no harm in safety first
                if (ain.getOpcode() == ATHROW) {
                    safe = false;
                    bad_throw++;
                    if (log_enable)
                        log_ln("[unsafe] class: " + clinit_cn.name + " [cause:throw]");
                }
                break;

            default:
                break;
            }
        }
        return safe;
    }

    /*
     * --field cases--
     *   1. *field: non-static field is for sure non-constant data
     *   2. getstatic: must be final and non-reference
     *   3. putstatic: can only be static from this class
     *
     * --putstatic to a clinit staticfield is safe--
     *   - at this point we have deemed every insn up to this point safe
     *      - meaning no references to non-constant data
     *   - if no non-constant data has been accessed prior to the putstatic
     *     call, putstatic cannot write non-constant data
     *
     * --getstatic to a clinit staticfield is safe--
     *   - a staticfield from clinit's class could not have been altered prior to this check
     *   - as long as we don't write any non-constant data to it nothing we get could be non-constant
     *   - what if you get and try to write to it after the get:
     *       note: *astore is only store that isn't put* and isn't for local variable
     *       - if you getstatic and follow it with astore it is only a problem if
     *         you reference non-constant data to use in the astore
     *       - this requires an instruction to reference the non-constant data,
     *         which we will see later, so no need to worry
     */

    public boolean handleField(ClassNode clinit_cn, FieldInsnNode fin) {

        boolean safe = false;

        switch (fin.getOpcode()) {
        case GETFIELD:
        case PUTFIELD:
            bad_ref_field++;
            if (log_enable) {
                log_f("[unsafe] class: %s [cause:get/putfield] fin.owner: %s fin.name: %s\n",
                        clinit_cn.name, fin.owner, fin.name);
            }
            break;

        case GETSTATIC:
            // we have a reference, but if it is owned by clinit's class it is ok
            // this is very rare (makes sense, why would you read from something you will initialize)
            // this is not a class dependence (as it would depend on itself)
            // everything else depends on when the owning class was initialized
            // even known safe classes' fields are unsafe as they might be initialized by a user called method
            // note: really only final from a known safe class could be safe and unaltered by the user
            if (clinit_cn.name.equals(fin.owner)) {
                safe = true;
            } else {
                bad_ref_getstatic++;
                if (log_enable) {
                    log_f("[unsafe] class: %s [cause:getstatic] fin.owner: %s fin.name: %s\n",
                            clinit_cn.name, fin.owner, fin.name);
                }
            }
            break;

        // only touch static fields from clinit class
        // causes crashes when running clinit
        case PUTSTATIC:
            if (clinit_cn.name.equals(fin.owner)) {
                safe = true;
                addClassDependence(clinit_cn, fin.owner);
            } else {
                bad_ref_putstatic++;
                if (log_enable) {
                    log_f("[unsafe] class: %s [cause:putstatic] fin.owner: %s fin.name: %s\n",
                            clinit_cn.name, fin.owner, fin.name);
                }
            }
            break;

        default:
            error_unsafe++;
            log_ln("[error][unsafe] class: " + clinit_cn.name
                    + " [cause:field] unknown opcode: " + fin.getOpcode());
            break;
        }
        return safe;
    }

    // return null if invoke virtual or if something unexpected happen
    public HashSet<MethodNode> handleInvoke(ClassNode clinit_cn, MethodInsnNode min) {

        // special native library case
        if (min.name.equals("loadLibrary")) {
            native_lib++;
            if (log_enable)
                log_ln("[unsafe] class: " + clinit_cn.name + " [cause:native] native library call");
            return null;
        }

        HashSet<MethodNode> result = new HashSet<MethodNode>();

        switch (min.getOpcode()) {
        case INVOKEVIRTUAL:
            /* -- can't handle invoke virtual --
             * - the object used to invoke the message is determined at
             * runtime meaning it is very likely non-constant
             * - technically it is possible for only one object to be
             * possible (making it constant), but determinting that is
             * quite hard
             */
            result = null;
            bad_ref_invokevirtual++;
            if (log_enable) {
                String key = min.owner + min.name + min.desc;
                log_ln("[unsafe] class: " + clinit_cn.name + " [cause:invokevirtual] = " + key);
            }
            break;

        case INVOKESTATIC: {
            String key = min.owner + min.name + min.desc;
            MethodNode invoke_mn = method_map.get(key);
            if (invoke_mn != null) {
                result.add(invoke_mn);
                addClassDependence(clinit_cn, min.owner);
            } else {
                /*
                 * probably caused by path not travelled at runtime
                 *
                 * we need to check this new class because even if this code
                 * path doesn't access non-constant data, this class' clinit
                 * could access non-constant data making it possible for us to
                 * change the outcome
                 *
                 * new: caused by createvm initialized (system) class
                 */
                readClass(min.owner);
                invoke_mn = method_map.get(key);
                if (invoke_mn != null) {
                    result.add(invoke_mn);
                    addClassDependence(clinit_cn, min.owner);
                } else {
                    result = null;
                    error_unsafe++;
                    log_ln("[error][unsafe] class: " + clinit_cn.name
                            + " [cause:invokestatic] !method_map.get(key): " + key);
            break;
                }
            }
            break;
        }

        case INVOKEINTERFACE:
            // TODO: I'm not sure pretty sure this is similar to invoke virtual
            result = null;
            bad_ref_invokeinterface++;
            if (log_enable) {
                String key = min.owner + min.name + min.desc;
                log_ln("[unsafe] class: " + clinit_cn.name + " [cause:invokeinterface] = " + key);
            }
            break;

        case INVOKESPECIAL: {
            // if class doesn't have a declaration must check superclass
            // recursively until finding a definition

            ClassNode cn = class_map.get(min.owner);
            if (cn == null) {
                /*
                 * probably caused by path not travelled at runtime
                 *
                 * we need to check this new class because even if this code
                 * path doesn't access non-constant data, this class' clinit
                 * could access non-constant data making it possible for us to
                 * change the outcome
                 *
                 * new: caused by createvm initialized (system) class
                 */
                cn = readClass(min.owner);
                if (cn == null) {
                    result = null;
                    error_unsafe++;
                    log_ln("[error][unsafe] class: " + clinit_cn.name
                            + " [cause:invokespecial] !readClass: " + min.owner);
                }
            }
            while (cn != null) {

                String key = cn.name + min.name + min.desc;
                MethodNode invoke_mn = method_map.get(key);
                if (invoke_mn != null) {
                    result.add(invoke_mn);
                    addClassDependence(clinit_cn, cn);
                    break;
                } else {
                    if (cn.superName != null) {
                        cn = class_map.get(cn.superName);
                        if (cn == null) {
                            // - super should have been initialized for this class to be initialized
                            // - manually reading the class isn't enough to "undo" the problem as we
                            //   are missing an initialized class (should be in classlist)
                            // - if we aren't missing the initialized class then there is no reason for
                            //   it not to be in the map

                            // special case: guaranteed safe classes (createvm initialized classes)
                            if (known_safe_list.contains(cn.superName)) {
                                cn = readClass(cn.superName);
                                if (cn == null) {
                                    // something really bad is going on if these can't be read
                                    result = null;
                                    error_unsafe++;
                                    log_ln("[error][unsafe] class: " + clinit_cn.name
                                            + " [cause:invokespecial] cannot readclass safe class: " + cn.superName);
                                }
                            } else {
                                result = null;
                                error_unsafe++;
                                log_ln("[error][unsafe] class: " + clinit_cn.name
                                        + " [cause:invokespecial] !class_map.get(super): " + cn.superName);
                            }
                        }
                    } else {
                        result = null;
                        error_unsafe++;
                        log_ln("[error][unsafe] class: " + clinit_cn.name
                                + " [cause:invokespecial] super is null, method not found: " + key);
                    }
                }
            }
            break;
        }

        default:
            result = null;
            error_unsafe++;
            log_ln("[error][unsafe] class: " + clinit_cn.name
                    + " [cause:invoke] unknown opcode: "+min.getOpcode());
            break;
        }
        return result;
    }

    public void output() {
        for (ClassNode cn : loaded_class_set) {
            if (safe_map.containsKey(cn.name))
                System.out.println(cn.name + " " + (safe_map.get(cn.name) ? '1' : '0'));
            else
                log_ln("[error][fatal][output] !safe_map.contains(class): " + cn.name);
        }
    }

    public void printStats() {
        for (ClassNode cn : loaded_class_set) {
            if (safe_map.get(cn.name))
                safe_classes++;
        }
        log_ln("[stats]          --------------------summary--------------------");
        log_f("[stats]          %-25s %7d\n","total classes",class_count);
        if (class_count != 0) {
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "classes with clinit",
                    clinit_count, ((double)clinit_count/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "safe classes",
                    safe_classes, ((double)safe_classes/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "[ref] get/put field",
                    bad_ref_field, ((double)bad_ref_field/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "[ref] getstatic",
                    bad_ref_getstatic, ((double)bad_ref_getstatic/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "[ref] putstatic",
                    bad_ref_putstatic, ((double)bad_ref_putstatic/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "[ref] invokevirtual",
                    bad_ref_invokevirtual, ((double)bad_ref_invokevirtual/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "[ref] invokeinterface",
                    bad_ref_invokeinterface, ((double)bad_ref_invokeinterface/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "throw",
                    bad_throw, ((double)bad_throw/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "native lib",
                    native_lib, ((double)native_lib/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "invoke dynamic",
                    bad_invoke_dynamic, ((double)bad_invoke_dynamic/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "super unsafe",
                    super_unsafe, ((double)super_unsafe/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "interface unsafe",
                    interface_unsafe, ((double)interface_unsafe/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "class dependence unsafe",
                    dependence_unsafe, ((double)dependence_unsafe/class_count)*100);
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "error",
                    error_unsafe, ((double)error_unsafe/class_count)*100);
            int unsafe = bad_ref_field + bad_ref_getstatic + bad_ref_putstatic + bad_ref_invokevirtual
                + bad_ref_invokeinterface + bad_throw + native_lib + bad_invoke_dynamic + super_unsafe
                + interface_unsafe + dependence_unsafe + error_unsafe;
            log_f("[stats]          %-25s %7d    (%7.4f)\n", "unsafe",
                    unsafe, ((double)unsafe/class_count)*100);
            log_f("[stats]          %-25s %7d\n", "safe + unsafe", safe_classes + unsafe);
        }
    }
}
