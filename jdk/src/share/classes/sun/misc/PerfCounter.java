/*
 * Copyright (c) 2009, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package sun.misc;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.LongBuffer;
import java.security.AccessController;

/**
 * Performance counter support for internal JRE classes.
 * This class defines a fixed list of counters for the platform
 * to use as an interim solution until RFE# 6209222 is implemented.
 * The perf counters will be created in the jvmstat perf buffer
 * that the HotSpot VM creates. The default size is 32K and thus
 * the number of counters is bounded.  You can alter the size
 * with -XX:PerfDataMemorySize=<bytes> option. If there is
 * insufficient memory in the jvmstat perf buffer, the C heap memory
 * will be used and thus the application will continue to run if
 * the counters added exceeds the buffer size but the counters
 * will be missing.
 *
 * See HotSpot jvmstat implementation for certain circumstances
 * that the jvmstat perf buffer is not supported.
 *
 */
public class PerfCounter {
    private static final Perf perf =
        AccessController.doPrivileged(new Perf.GetPerfAction());

    // Must match values defined in hotspot/src/share/vm/runtime/perfdata.hpp
    private final static int V_Constant  = 1;
    private final static int V_Monotonic = 2;
    private final static int V_Variable  = 3;
    private final static int U_None      = 1;

    private final String name;
    private final LongBuffer lb;

    private PerfCounter(String name, int type) {
        this.name = name;
        ByteBuffer bb = perf.createLong(name, type, U_None, 0L);
        bb.order(ByteOrder.nativeOrder());
        this.lb = bb.asLongBuffer();
    }

    static PerfCounter newPerfCounter(String name) {
        return new PerfCounter(name, V_Variable);
    }

    static PerfCounter newConstantPerfCounter(String name) {
        PerfCounter c = new PerfCounter(name, V_Constant);
        return c;
    }

    /**
     * Returns the current value of the perf counter.
     */
    public synchronized long get() {
        return lb.get(0);
    }

    /**
     * Sets the value of the perf counter to the given newValue.
     */
    public synchronized void set(long newValue) {
        lb.put(0, newValue);
    }

    /**
     * Adds the given value to the perf counter.
     */
    public synchronized void add(long value) {
        long res = get() + value;
        lb.put(0, res);
    }

    /**
     * Increments the perf counter with 1.
     */
    public void increment() {
        add(1);
    }

    /**
     * Adds the given interval to the perf counter.
     */
    public void addTime(long interval) {
        add(interval);
    }

    /**
     * Adds the elapsed time from the given start time (ns) to the perf counter.
     */
    public void addElapsedTimeFrom(long startTime) {
        add(System.nanoTime() - startTime);
    }

    @Override
    public String toString() {
        return name + " = " + get();
    }

    static class CoreCounters {
        static final PerfCounter pdt   = newPerfCounter("sun.classloader.parentDelegationTime");
        static final PerfCounter lc    = newPerfCounter("sun.classloader.findClasses");
        static final PerfCounter lct   = newPerfCounter("sun.classloader.findClassTime");
        static final PerfCounter rcbt  = newPerfCounter("sun.urlClassLoader.readClassBytesTime");
        static final PerfCounter zfc   = newPerfCounter("sun.zip.zipFiles");
        static final PerfCounter zfot  = newPerfCounter("sun.zip.zipFile.openTime");

        static final PerfCounter flct = newPerfCounter("sun.classloader.findLoadedClassTime");
        static final PerfCounter cnlst = newPerfCounter("sun.classloader.classNameLockSyncTime");
        static final PerfCounter null_lc = newPerfCounter("sun.classloader.nullFindClasses");
        static final PerfCounter null_lct = newPerfCounter("sun.classloader.nullFindClassTime");
        static final PerfCounter ojt  = newPerfCounter("sun.urlClassLoader.openJarTime");
        static final PerfCounter oj  = newPerfCounter("sun.urlClassLoader.openJars");
        static final PerfCounter dct  = newPerfCounter("sun.urlClassLoader.defineClassTime");
        static final PerfCounter clct = newPerfCounter("sun.classloader.classLoadingCompTime");
        static final PerfCounter rct = newPerfCounter("sun.classloader.resolveClassTime");

        static final PerfCounter rcb = newPerfCounter("sun.urlClassLoader.readClassBytes");
        static final PerfCounter rcb_u1kb = newPerfCounter("sun.urlClassLoader.readClassBytes_u1kb");
        static final PerfCounter rcb_u5kb = newPerfCounter("sun.urlClassLoader.readClassBytes_u4kb");
        static final PerfCounter rcbt_u1kb = newPerfCounter("sun.urlClassLoader.readClassBytesTime_u1kb");
        static final PerfCounter rcbt_u5kb = newPerfCounter("sun.urlClassLoader.readClassBytesTime_u4kb");

        static final ThreadLocal<PerfCounter> tl_lct = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_findClassTime");
            }
        };
        static final ThreadLocal<PerfCounter> tl_null_lc = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_null_findClasses");
            }
        };
        static final ThreadLocal<PerfCounter> tl_null_lct = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_null_findClassTime");
            }
        };
        static final ThreadLocal<PerfCounter> tl_flct = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_findLoadedCLassTime");
            }
        };
        static final ThreadLocal<PerfCounter> tl_pdt = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_parentDelegationTime");
            }
        };
        static final ThreadLocal<PerfCounter> tl_rct = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_resolveClassTime");
            }
        };
        static final ThreadLocal<PerfCounter> tl_cnlst = new ThreadLocal<PerfCounter>() {
            @Override protected PerfCounter initialValue() {
                return PerfCounter.newPerfCounter("t"+Thread.currentThread().getId()+"_classNameLockSyncTime");
            }
        };
    }

    static class WindowsClientCounters {
        static final PerfCounter d3dAvailable = newConstantPerfCounter("sun.java2d.d3d.available");
    }

    /**
     * Number of findClass calls
     */
    public static PerfCounter getFindClasses() {
        return CoreCounters.lc;
    }

    /**
     * Time (ns) spent in finding classes that includes
     * lookup and read class bytes and defineClass
     */
    public static PerfCounter getFindClassTime() {
        return CoreCounters.lct;
    }

    /**
     * Time (ns) spent in finding classes
     */
    public static PerfCounter getReadClassBytesTime() {
        return CoreCounters.rcbt;
    }

    /**
     * Time (ns) spent in the parent delegation to
     * the parent of the defining class loader
     */
    public static PerfCounter getParentDelegationTime() {
        return CoreCounters.pdt;
    }

    /**
     * Number of zip files opened.
     */
    public static PerfCounter getZipFileCount() {
        return CoreCounters.zfc;
    }

    /**
     * Time (ns) spent in opening the zip files that
     * includes building the entries hash table
     */
    public static PerfCounter getZipFileOpenTime() {
        return CoreCounters.zfot;
    }

    /**
     * D3D graphic pipeline available
     */
    public static PerfCounter getD3DAvailable() {
        return WindowsClientCounters.d3dAvailable;
    }

    // JVMPERF
    /**
     * Time (ns) spend trying to synchronize on class name lock
     */
    public static PerfCounter getClassNameLockSyncTime() {
        return CoreCounters.cnlst;
    }
    /**
     * Number of null class loader findClass calls
     */
    public static PerfCounter getNullFindClasses() {
        return CoreCounters.null_lc;
    }
    /**
     * Time (ns) spent in null finding classes that includes
     * lookup and read class bytes and defineClass
     */
    public static PerfCounter getNullFindClassTime() {
        return CoreCounters.null_lct;
    }
    public static PerfCounter getOpenJars() {
        return CoreCounters.oj;
    }
    public static PerfCounter getOpenJarTime() {
        return CoreCounters.ojt;
    }
    public static PerfCounter getDefineClassTime() {
        return CoreCounters.dct;
    }
    public static PerfCounter getClassLoadingCompTime() {
        return CoreCounters.clct;
    }
    public static PerfCounter getFindLoadedClassTime() {
        return CoreCounters.flct;
    }
    public static PerfCounter getResolveClassTime() {
        return CoreCounters.rct;
    }
    public static PerfCounter getReadClassBytes() {
        return CoreCounters.rcb;
    }
    public static PerfCounter getReadClassBytes_u1kb() {
        return CoreCounters.rcb_u1kb;
    }
    public static PerfCounter getReadClassBytes_u5kb() {
        return CoreCounters.rcb_u5kb;
    }
    public static PerfCounter getReadClassBytesTime_u1kb() {
        return CoreCounters.rcbt_u1kb;
    }
    public static PerfCounter getReadClassBytesTime_u5kb() {
        return CoreCounters.rcbt_u5kb;
    }
    // JVMPERF thread local
    public static PerfCounter tl_FindClassTime() {
        return CoreCounters.tl_lct.get();
    }
    public static PerfCounter tl_ClassNameLockSyncTime() {
        return CoreCounters.tl_cnlst.get();
    }
    public static PerfCounter tl_FindLoadedClassTime() {
        return CoreCounters.tl_flct.get();
    }
    public static PerfCounter tl_ParentDelegationTime() {
        return CoreCounters.tl_pdt.get();
    }
    public static PerfCounter tl_ResolveClassTime() {
        return CoreCounters.tl_rct.get();
    }
    public static PerfCounter tl_NullFindClasses() {
        return CoreCounters.tl_null_lc.get();
    }
    public static PerfCounter tl_NullFindClassTime() {
        return CoreCounters.tl_null_lct.get();
    }
}
