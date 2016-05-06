#!/usr/bin/env bash

if [[ $# < 2 ]]; then
    echo "run.sh <classpath (file or string)> <classlist file> [log_enable]"
    exit 1
fi

if [[ -f $1 ]]; then
    classpath="$(cat $1)"
else
    classpath="$1"
fi

classlist="$2"
log="$3"

# this will work as long as you don't symlink directly to this file
# needed for the classpath + java call
dir="$(dirname "${BASH_SOURCE[0]}")"

# add our own jars
classpath="$dir/static_analysis.jar:$dir/asm-5.1.jar:$dir/asm-tree-5.1.jar:$classpath"

"$dir"/../../bin/java -cp $classpath StaticAnalysis $classlist $log
