HotTub
======
HotTub is built on top of OpenJDK 8.
OpenJDK 8 uses Mercurial and the "[trees][1]" extension for version control.
We tried using their setup, but ultimately switched to git as it fit our work-flow better (whether this was the best choice is another discussion).

## Building
First configure OpenJDK normally - see `README.openjdk.txt` (renamed from `README`) and `README-builds.html`.
You can see options with `bash ./configure --help`.
Below is a quick overview which should be able to get you started, but you should still read OpenJDK's instructions.

### OpenJDK
Compiling OpenJDK requires a "bootstrap" Java installation.
When you run `bash ./configure`, it chooses a Java installation with the following preference:
	- `--with-boot-jdk=<path/to/java/home>` flag: path to a Java installation, where the `java` executable is `<path/to/java/home>/bin/java`
	- `JAVA_HOME` environment variable: path to a Java installation, as above, e.g. what you use when configuring Hadoop
	- `java` on the path

You can set the debug level with `--with-debug-level=`.
The options are `release` (default), `fastdebug`, and `slowdebug` (no optimizations).

OpenJDK source includes an empty CA certificates file in `./jdk/src/share/lib/security/cacerts`.
From `README-builds.html`: "Failure to provide a populated cacerts file will result in verification errors of a certificate chain during runtime."
You can specify a cacerts file, e.g. from a Java installation from your distro's repo, with `--with-cacerts-file=`.

Example:
```
bash ./configure --with-boot-jdk=/usr/lib/jvm/java-8-oracle \
	--with-debug-level=fastdebug \
	--with-cacerts-file=/usr/lib/jvm/java-8-oracle/jre/lib/security/cacerts
```

#### Additional notes
If you get errors about `freetype` missing, but it is installed in your system, use the `--with-freetype-lib=` and `--with-freetype-include=` flags.
You can use `freetype-config --libs` which should give linker flags - e.g. `-L/usr/lib/x86_64-linux-gnu -lfreetype -lz -lpng12`,
and `freetype-config --cflags` which should give compiler flags - e.g. `-I/usr/include/freetype2`.
With these two paths from the `-L` and `-I` flags, you would configure like `bash ./configure --with-freetype-lib=/usr/lib/x86_64-linux-gnu --with-freetype-include=/usr/include/freetype2`.

### HotTub
To build HotTub, run `./make_hottub.sh <image name> [<jvm build type>]`, where `<jvm build type>` is `release`, `fastdebug`, or `slowdebug`.
See the OpenJDK readmes for more information.

An image `j2sdk-image.java8.<image name>` will be copied to this project's root dir (this file's dir).
This image dir is `JAVA_HOME`, as explained above in the **OpenJDK** section, and you may want to add `$JAVA_HOME/bin` to your `PATH`.
If the shell variable `JVMS_DIR` is set (as an environment variable or by modifying the script),
the script will also copy the image there, and create a symlink `$JVMS_DIR/java_home`.

The script `make_hottub.sh` builds the static analysis files, "client" files, and OpenJDK (the "server" logic is inside OpenJDK code).

## Diffs
Run `git diff vanilla master` to get a patch.
To apply HotTub changes to another OpenJDK project (e.g. updated source), you can run `patch -p1 < hottub.patch` in the root of the other project.

To apply OpenJDK changes to HotTub, clone a new OpenJDK project, and recursively copy the updated OpenJDK source to the `vanilla` branch of HotTub.
Git will see all the changes (you should double check that things look right), and you can commit to the `vanilla` branch.
You can then merge `vanilla` into `master`.

## Flags
* -hottub                    : enable hottub
* -XX:+HotTubReinit          : enable class re-initialization
* -XX:+HotTubLog             : enable logging
* -XX:+HotTubTmp             : enable temporary (debugging) features
* -XX:HotTubReinitSkip       : TODO add me
* -XX:+ProfileIntComp        : enable profiling of interpretered, and compiled (jitted + native) code
* -XX:+ProfileIntCompJitOnly : only profile jitted code; native time included in interpreted time, rather than compiled time

## Interpreter, Compiled/Jit, Native code profiling
With the exception of blocking compiled time (e.g. if Java is run with the `-comp` or `-XX:+CompileTheWorld` flag),
you must first enable profiling with the `-XX:+ProfileIntComp` flag, or you will get that the interpreter is executing 100% of the time.
The following instance methods are added to the `java.lang.Thread` class, as the counters are per-thread.

```
/**
 * Reset the counters
 */
void Thread#resetIntCompTimes();
/**
 * Get blocking compile time in nanoseconds
 */
long Thread#getBlockingCompileTime();
/**
 * Get interpreter time in nanoseconds
 */
long Thread#getIntTime();
/**
 * Get compiled/jit time in nanoseconds
 */
long Thread#getCompTime();
```

Examples:
```
Thread th = Thread.currentThread();
System.out.format("Thread has spent %.6f seconds running compiled code\n", th.getCompTime() / 1e9);

Thread th2 = new Thread(new MyRunnable());
th2.start();
th2.join();
System.out.format("Thread 2 has spent %.6f seconds running interpreted code\n", th2.getIntTime() / 1e9);
```

## Implementation notes
Notes will be added soon.

## Known issues and bugs
- HotTub sends a `ThreadDeath` error (extends `java.lang.Throwable`) to "cleanly" kill threads when an application finishes via `System.exit` or a signal
	- this causes a lot of backtraces to be printed, including locks and synchronization complaining about an `InterruptedException`
		- this is actually fine and these messages can be safely ignored; all threads are being killed and all data is being reset
	- TODO: implement another way of killing threads, or silence these backtraces if during a "HotTub shutdown"
- Segfault in jitted code, related to `sun.misc.Unsafe`
	- TODO: debug...
	- Workaround solution: disable jitting of methods with the [`-XX:CompileCommand`][2] flag (you can specify multiple)
		- Example: `-XX:CompileCommand=exclude,akka/actor/RepointableActorRef.underlying`
- Segfault in jitted `java.util.concurrent.LinkedBlockingQueue#take`, see trace below
	- Workaround solution: `-XX:CompileCommand=exclude,java/util/concurrent/LinkedBlockingQueue.take`

```
Stack: [0x00007f325cee2000,0x00007f325cfe3000],  sp=0x00007f325cfe0aa0,  free space=1018k
Native frames: (J=compiled Java code, j=interpreted, Vv=VM code, C=native code)
V  [libjvm.so+0x812b27]  nmethod::handler_for_exception_and_pc(Handle, unsigned char*)+0x17
V  [libjvm.so+0x8d2065]  OptoRuntime::handle_exception_C_helper(JavaThread*, nmethod*&)+0x435
V  [libjvm.so+0x8d21b8]  OptoRuntime::handle_exception_C(JavaThread*)+0x28
v  ~ExceptionBlob
C  0x00000005f8000d34

Java frames: (J=compiled Java code, j=interpreted, Vv=VM code)
v  ~ExceptionBlob
J 22047 C2 java.util.concurrent.LinkedBlockingQueue.take()Ljava/lang/Object; (93 bytes) @ 0x00007f3440873cac [0x00007f34408737e0+0x4cc]
J 27765 C2 java.util.concurrent.ThreadPoolExecutor.getTask()Ljava/lang/Runnable; (179 bytes) @ 0x00007f3441e03fdc [0x00007f3441e03f20+0xbc]
J 24018 C1 java.util.concurrent.ThreadPoolExecutor.runWorker(Ljava/util/concurrent/ThreadPoolExecutor$Worker;)V (225 bytes) @ 0x00007f343dc82554 [0x00007f343dc822c0+0x294]
J 28618 C1 java.util.concurrent.ThreadPoolExecutor$Worker.run()V (9 bytes) @ 0x00007f343fe56204 [0x00007f343fe56100+0x104]
J 24045 C1 java.lang.Thread.run()V (17 bytes) @ 0x00007f343d1f6544 [0x00007f343d1f6400+0x144]
v  ~StubRoutines::call_stub
```

[1]: http://openjdk.java.net/projects/code-tools/trees/
[2]: http://docs.oracle.com/javase/8/docs/technotes/tools/unix/java.html
