HotTub
======
HotTub is built on top of OpenJDK 8.
OpenJDK 8 uses mercurial for version control - for reasons best not discussed, we used git.

## Building
To build HotTub, run `./make_hottub.sh <image name> [<jvm build type>]`.
An image `j2sdk-image.java8.<image name>` will be copied to this project's root dir (this file's dir).
This image dir is `JAVA_HOME`, e.g. for Hadoop configuration, and `JAVA_HOME/bin` should be added to the `PATH`.
If the shell variable `JVMS_DIR` is set (as an environment variable or by modifying the script),
it will also copy the image there, and create a symlink `$JVMS_DIR/java_home`.

The script `make_hottub.sh` builds the static analysis files, "client" files, and OpenJDK (the "server" logic is inside OpenJDK code).

## Diffs
Notes will be added soon.

## Notes
Notes will be added soon.

## jtrace
Notes will be added soon.
