bdel
====
Instrumenting the JVM to keep track of time spent in the interpreter, and time spent in jit-compiled code.

## Usage
- add flag `-XX:+WildTurtle` to count
- add flag `-XX:+Dyrus` for additional debugging logs

## TODO
- keep track of time spent in native (JNI) calls
- keep track of time spent sleeping (and other?)?
- optimize compiled entries (osr and standard) to directly update counter?
- use `rdtsc`?
- improve tracking

## Notes
- hmmm
