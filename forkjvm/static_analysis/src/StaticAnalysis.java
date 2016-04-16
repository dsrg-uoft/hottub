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

    public static void LOGLN (String s) {
        if (log_enable) {
            System.out.println("[StaticAnalysis]" + s);
        }
    }
    public static void LOGF (String format, Object... args) {
        if (log_enable) {
            System.out.printf(format, args);
        }
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

    public void addClassDependence(ClassNode cn, ClassNode dep_cn) {
        if (!class_dependence_map.containsKey(cn)){
            class_dependence_map.put(cn, new HashSet<ClassNode>());
        }
        class_dependence_map.get(cn).add(dep_cn);
    }

    public void addClassDependence(ClassNode cn, String class_name) {
        ClassNode dep_cn = class_map.get(class_name);
        if (dep_cn != null) {
            addClassDependence(cn, dep_cn);
        } else {
            System.out.println("[error][fatal][cause:class_map] !class_name.get(class) class: " + class_name);
        }
    }

    public static void main(String[] args) {

        if (args.length < 1) {
            System.out.println("requires classlist file");
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
        if (log_enable) {
            sa.printStats();
        }
    }

    public StaticAnalysis() {
        loaded_class_set = new HashSet<ClassNode>();
        class_map = new HashMap<String, ClassNode>();
        method_map = new HashMap<String, MethodNode>();
        safe_map = new HashMap<String, Boolean>();
        class_dependence_map = new HashMap<ClassNode, HashSet<ClassNode>>();
    }

    public ClassNode readClass(String class_name) {
        try {
            ClassReader cr = new ClassReader(class_name);
            ClassNode cn = new ClassNode();
            cr.accept(cn, ClassReader.SKIP_DEBUG);

            if (!class_map.containsKey(cn.name)) {
                class_map.put(cn.name, cn);
            } else {
                System.out.println("[error][fatal][cause:class_map] name not unique: " + cn.name);
            }

            for (int i = 0; i < cn.methods.size(); i++) {
                MethodNode mn = (MethodNode) cn.methods.get(i);
                String key = cn.name + mn.name + mn.desc;

                if (!method_map.containsKey(key)) {
                    method_map.put(key, mn);
                } else {
                    System.out.println("[error][fatal][cause:method_map] name not unique: " + key);
                }
            }
        } catch (Exception e) {
            System.out.println("[error][fatal][cause:asm] class: " + class_name + " " + e);
        }
        return class_map.get(class_name);
    }

    public void readClassList(String classlistFile) {
        // read through the classlist and populate the maps
        try(BufferedReader br = new BufferedReader(new FileReader(classlistFile))) {
            for(String line; (line = br.readLine()) != null;) {
                ClassNode cn = readClass(line);
                if (cn != null) {
                    loaded_class_set.add(cn);
                }
            }
        } catch (Exception e) {
            LOGLN("wups: "+e);
        }
    }

    public void checkSafety() {
        final String clinitConst = "<clinit>()V";

        // first phase handle classes as individuals
        for (ClassNode cn : loaded_class_set) {
            class_count++; // stats

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
                            if (!mn_visited.contains(callee)) {
                                mn_stack.push(callee);
                            }
                        }
                    }
                }
                // made it to end of clinit code trace
                if (mn_stack.empty()) {
                    safe_class = true;
                }

                clinit_count++; // stats
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
                            super_unsafe++; //stats
                            LOGLN("[unsafe] class: " + cn.name
                                    + " [cause:super] unsafe super: " + super_cn.name);
                        } else {
                            // this should only happen for java/lang/Object
                            if (super_cn.superName == null) {
                                super_cn = null;
                            } else {
                                if (!class_map.containsKey(super_cn.superName)) {
                                    safe = false;
                                    error_unsafe++; //stats
                                    System.out.println("[error][unsafe] class: " + cn.name
                                            + " [cause:super] !class_map.contains(super): " + super_cn.name);
                                }
                                super_cn = class_map.get(super_cn.superName);
                            }
                        }
                    } else {
                        safe = false;
                        error_unsafe++; //stats
                        System.out.println("[error][unsafe] class: " + cn.name
                                + " [cause:super] !safe_map.contains(super): " + super_cn.name);
                    }
                }
                // check interfaces
                // if any interface is not safe this is not safe
                // don't ask me why cn.interfaces is an object...
                List<String> interface_names = (List<String>)cn.interfaces;
                for (int i = 0; safe && i < interface_names.size(); i++) {
                    String interface_name = interface_names.get(i);
                    if (safe_map.containsKey(interface_name) && !safe_map.get(interface_name)) {
                        safe = false;
                        interface_unsafe++; //stats
                        LOGLN("[unsafe] class: " + cn.name+" [cause:interface] unsafe interface: " + interface_name);
                    }
                    /* if the safe map doesn't contain the interface it means
                     * the interface was never initialized, so we're good
                     *
                     * } else {
                     *     safe = false;
                     *     error_unsafe++; //stats
                     *     System.out.println("[error][unsafe] class: " + cn.name
                     *           + " [cause:interface] !safe_map.contains(interface): " + interface_name);
                     * }
                     */
                }
                if (!safe) {
                    safe_map.put(cn.name, false);
                }
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
                                    dependence_unsafe++; //stats
                                }
                            } else {
                                safe = false;
                                error_unsafe++; //stats
                                System.out.println("[error][unsafe] class: " + cn.name
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

        //LOGLN("[TRACE] walkmethod clinit.name: "+clinit_cn.name);
        for (int i = 0; safe && i < insns.size(); i++) {
            AbstractInsnNode ain = insns.get(i);
            //LOGLN("[TRACE] insn["+i+"] opcode: "+Integer.toHexString(ain.getOpcode()));

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
                    for (MethodNode callee : invoke_callees) {
                        method_callees.add(callee);
                    }
                }
                break;

            // deals with java.lang.invoke.MethodHandle
            // I think this is like function pointer aka not safe
            case AbstractInsnNode.INVOKE_DYNAMIC_INSN:
                safe = false;
                bad_invoke_dynamic++; // stats
                LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:invoke_dynamic]");
                break;

            case AbstractInsnNode.INSN:
                // throwing things can hurt people so not safe
                // most likely throw logic depends on a reference, so we should
                // already handle this implicitly, but no harm in safety first
                if (ain.getOpcode() == ATHROW) {
                    safe = false;
                    bad_throw++; // stats
                    LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:throw]");
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
                bad_ref_field++; // stats
                LOGLN("[unsafe] class: " + clinit_cn.name
                        + " [cause:field] putfield/getfield (non-static reference)");
                break;

            case GETSTATIC:
                // we have a reference, but if it is owned by clinit's class it is ok
                // this is very rare (makes sense, why would you read from something you will initialize)
                // this is not a class dependence (as it would depend on itself)
                if (clinit_cn.name.equals(fin.owner)) {
                    safe = true;
                } else {
                    // check if reference
                    if (!fin.desc.startsWith("L") && !fin.desc.startsWith("[")) {
                        // if not reference check if final
                        ClassNode field_cn = class_map.get(fin.owner);
                        if (field_cn == null) {
                            error_unsafe++; // stats
                            System.out.println("[error][unsafe] class: " + clinit_cn.name 
                                    + " [cause:field] !class_map.get(field): " + fin.owner);
                            break;
                        }
                        // get field node from class (could make map at start, but not used enough to bother)
                        FieldNode ffn = null;
                        // don't ask me why classnode.fields is an object...
                        for (FieldNode fn : (List<FieldNode>)field_cn.fields) {
                            if (fn.name.equals(fin.name)) {
                                ffn = fn;
                                break;
                            }
                        }
                        if (ffn != null) {
                            // not a reference and also final
                            // this is very rare
                            if ((ffn.access & ACC_FINAL) == 1) {
                                safe = true; // not reference and final
                                addClassDependence(clinit_cn, field_cn);
                            } else {
                                bad_ref_getstatic++; // stats
                            }
                        } else {
                            error_unsafe++; // stats
                            System.out.println("[error][unsafe] class: " + clinit_cn.name
                                    + " [cause:field] !fieldnode for: " + fin.owner);
                        }
                    } else {
                        bad_ref_getstatic++; // stats
                        LOGLN("[unsafe] class: " + clinit_cn.name
                                + " [cause:field] static field reference outside of class");
                    }
                }
                break;

            // only touch static fields from clinit class
            // causes crashes when running clinit
            case PUTSTATIC:
                //TODO: this causes null ptr exception on 2nd run of hdfs
                bad_ref_putstatic++; // stats
                LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:field] putstatic");
                //LOGF("[info][putstatic] clinit.name: %s fin.owner: %s fin.name: %s\n", clinit_cn.name,fin.owner,fin.name);
                //if (clinit_cn.name.equals(fin.owner)) {
                //    //safe = true;
                //    //addClassDependence(clinit_cn, fin.owner);
                //    bad_ref_putstatic++; // stats
                //} else {
                //    bad_ref_putstatic++; // stats
                //}
                break;

            default:
                error_unsafe++; // stats
                System.out.println("[error][unsafe] class: " + clinit_cn.name 
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
            LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:invoke] native library call");
            return null;
        }

        HashSet<MethodNode> result = new HashSet<MethodNode>();

        switch (min.getOpcode()) {

        case INVOKEVIRTUAL: // invoke virtual
            /* -- can't handle invoke virtual --
             * - the object used to invoke the message is determined at
             * runtime meaning it is very likely non-constant
             * - technically it is possible for only one object to be
             * possible (making it constant), but determinting that is
             * quite hard
             */
            result = null;
            bad_ref_invokevirtual++; // stats
            LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:invoke] invokevirtual");
            break;

        case INVOKESTATIC: { // invoke static
            // directly call
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
                 */
                readClass(min.owner);
                invoke_mn = method_map.get(key);
                if (invoke_mn != null) {
                    result.add(invoke_mn);
                    addClassDependence(clinit_cn, min.owner);
                } else {
                    result = null;
                    error_unsafe++;
                    System.out.println("[error][unsafe] class: " + clinit_cn.name
                            + " [cause:invoke] invokestatic !method_map.get(key): " + key);
            break;
                }
            }
            break;
        }

        case INVOKEINTERFACE: // invoke interface
            // TODO: I'm not sure any more COME BACK TO THIS?
            result = null;
            bad_ref_invokeinterface++; // stats
            LOGLN("[unsafe] class: " + clinit_cn.name + " [cause:invoke] invokeinterface");
            break;

        case INVOKESPECIAL: { // invoke special
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
                 */
                cn = readClass(min.owner);
                if (cn == null) {
                    result = null;
                    error_unsafe++;
                    System.out.println("[error][unsafe] class: " + clinit_cn.name
                            + " [cause:invoke] invokespecial !readClass: " + min.owner);
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
                            result = null;
                            error_unsafe++;
                            System.out.println("[error][unsafe] class: " + clinit_cn.name
                                    + " [cause:invoke] invokespecial !class_map.get(super): " + cn.superName);
                        }
                    } else {
                        result = null;
                        error_unsafe++;
                        System.out.println("[error][unsafe] class: " + clinit_cn.name
                                + " [cause:invoke] invokespecial super is null, method not found: " + key);
                    }
                }
            }
            break;
        }

        default:
            result = null;
            error_unsafe++;
            System.out.println("[error][unsafe] class: " + clinit_cn.name
                    + " [cause:invoke] unknown opcode: "+min.getOpcode());
            break;
        }
        return result;
    }

    public void output() {
        for (ClassNode cn : loaded_class_set) {
            if (safe_map.containsKey(cn.name)) {
                boolean safe = safe_map.get(cn.name);
                System.out.println(cn.name + " " + (safe ? '1' : '0'));
            } else {
                System.out.println("[error][fatal][output] !safe_map.contains(class): " + cn.name);
            }
        }
    }

    public void printStats() {
        for (ClassNode cn : loaded_class_set) {
            if (safe_map.get(cn.name)) {
                safe_classes++; // stats
            }
        }
        LOGLN("[stats]          --------------------summary--------------------");
        LOGF("[StaticAnaylsis][stats]          %-25s %7d\n","total classes",class_count);
        if (class_count != 0) {
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "classes with clinit",
                    clinit_count, ((double)clinit_count/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "safe classes",
                    safe_classes, ((double)safe_classes/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "[ref] get/put field",
                    bad_ref_field, ((double)bad_ref_field/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "[ref] getstatic",
                    bad_ref_getstatic, ((double)bad_ref_getstatic/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "[ref] putstatic",
                    bad_ref_putstatic, ((double)bad_ref_putstatic/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "[ref] invokevirtual",
                    bad_ref_invokevirtual, ((double)bad_ref_invokevirtual/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "[ref] invokeinterface",
                    bad_ref_invokeinterface, ((double)bad_ref_invokeinterface/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "throw",
                    bad_throw, ((double)bad_throw/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "native lib",
                    native_lib, ((double)native_lib/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "invoke dynamic",
                    bad_invoke_dynamic, ((double)bad_invoke_dynamic/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "super unsafe",
                    super_unsafe, ((double)super_unsafe/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "interface unsafe",
                    interface_unsafe, ((double)interface_unsafe/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "class dependence unsafe",
                    dependence_unsafe, ((double)dependence_unsafe/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "error",
                    error_unsafe, ((double)error_unsafe/class_count)*100);

            int unsafe = bad_ref_field + bad_ref_getstatic + bad_ref_putstatic + bad_ref_invokevirtual
                + bad_ref_invokeinterface + bad_throw + native_lib + bad_invoke_dynamic + super_unsafe
                + interface_unsafe + dependence_unsafe + error_unsafe;
            LOGF("[StaticAnaylsis][stats]          %-25s %7d    (%7.4f)\n", "unsafe",
                    unsafe, ((double)unsafe/class_count)*100);
            LOGF("[StaticAnaylsis][stats]          %-25s %7d\n", "safe + unsafe", safe_classes + unsafe);
        }
    }
}
