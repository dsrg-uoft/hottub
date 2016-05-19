#!/bin/bash

set -e

# use a stable jvm
#java_home="/usr/lib/jvm/java-7-openjdk-amd64"
#javac="$java_home/bin/javac"
#jar="$java_home/bin/jar"


cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$(pwd)"

echo "Compiling..."
cd src
# For some reason, `javac` and `jar` returns exit code 1 even when there are no warnings
javac -Xlint -cp asm-5.1.jar:asm-tree-5.1.jar StaticAnalysis.java || [ $? -eq 1 ]

echo "Building jar..."
jar -cf static_analysis.jar StaticAnalysis.class || [ $? -eq 1 ]

echo "Finishing up..."
rm StaticAnalysis.class

rm -rf "$ROOT/build"
mkdir "$ROOT/build"

cp asm-5.1.jar "$ROOT/build"
cp asm-tree-5.1.jar "$ROOT/build"
mv static_analysis.jar "$ROOT/build"
cp run.sh "$ROOT/build"
