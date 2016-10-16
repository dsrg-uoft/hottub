HotTub
======
HotTub is built on top of OpenJDK 8.
OpenJDK 8 uses Mercurial and the "[trees][1]" extension for version control.
We tried using their setup, but ultimately switched to git as it fit our work-flow better (whether this was the best choice is another discussion).

## Building
First configure OpenJDK normally - see `README.openjdk.txt` (renamed from `README`) and `README-builds.html`.
You can see options with `bash ./configure --help`.

### OpenJDK
Compiling OpenJDK requires a "bootstrap" Java installation.
When you run `bash ./configure`, if you have set and exported the `JAVA_HOME` environment variable (where the executable `java` is `$JAVA_HOME/bin/java`), that will take precedence over `java` on your `PATH`.
You can also use the `--with-boot-jdk=` flag to point to a Java installation (same path as what you would said `JAVA_HOME` to).

If you get errors about `freetype` missing, but it is installed in your system, use the `--with-freetype-lib=` and `--with-freetype-include=` flags.
You can use `freetype-config --libs` which should give linker flags - e.g. `-L/usr/lib/x86_64-linux-gnu -lfreetype -lz -lpng12`,
and `freetype-config --cflags` which should give compiler flags - e.g. `-I/usr/include/freetype2`.
With these two paths from the `-L` and `-I` flags, you would configure like `bash ./configure --with-freetype-lib=/usr/lib/x86_64-linux-gnu --with-freetype-include=/usr/include/freetype2`.

Examples:
* bash ./configure --with-boot-jdk=/usr/lib/jvm/java-7-openjdk-amd64
* bash ./configure --with-boot-jdk=/usr/lib/jvm/java-7-openjdk-amd64 --with-debug-level=slowdebug

### HotTub
To build HotTub, run `./make_hottub.sh <image name> [<jvm build type>]`, where `<jvm build type>` is `release`, `fastdebug`, or `slowdebug`.
See the OpenJDK readmes for more information.

An image `j2sdk-image.java8.<image name>` will be copied to this project's root dir (this file's dir).
This image dir is `JAVA_HOME`, e.g. for Hadoop configuration, and `$JAVA_HOME/bin` should be added to the `PATH`.
If the shell variable `JVMS_DIR` is set (as an environment variable or by modifying the script),
it will also copy the image there, and create a symlink `$JVMS_DIR/java_home`.

The script `make_hottub.sh` builds the static analysis files, "client" files, and OpenJDK (the "server" logic is inside OpenJDK code).

## Diffs
Run `git diff vanilla master` to get a patch.
If you pipe it to a file, you can apply them to an updated OpenJDK project source with `patch -p1 < hottub.patch`.

## Flags
* -hottub                    : enable hottub
* -XX:+HotTubReinit          : enable class re-initialization
* -XX:+HotTubLog             : enable logging
* -XX:+HotTubTmp             : enable temporary (debugging) features
* -XX:+ProfileIntComp        : enable profiling of interpretered, and compiled (jitted + native) code
* -XX:+ProfileIntCompJitOnly : only profile jitted code; native time included in interpreted time, rather than compiled time

## Interpreter, Compiled/Jit, Native code profiling
With the exception of blocking compiled time, you must first enable profiling with the `-XX:+ProfileIntComp` flag to use the counters, or you will get that the interpreter is executing 100% of the time.
The following instance methods are added to the `java.lang.Thread` class, as the counters are per-thread.

```
/**
 * Reset the counters.
 */
void Thread#resetIntCompTimes();
/**
 * Get blocking compile time in nanoseconds - e.g. if Java is run with the `-comp` or `-XX:+CompileTheWorld` flag
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
System.out.print(String.format("Thread has spent %.6f seconds running compiled code\n", th.getCompTime() / 1e9));

Thread th2 = new Thread(new MyRunnable());
th2.start();
th2.join();
System.out.print(String.format("Thread 2 has spent %.6f seconds running interpreted code\n", th2.getIntTime() / 1e9));
```

## Implementation notes
Notes will be added soon.

[1]: http://openjdk.java.net/projects/code-tools/trees/
