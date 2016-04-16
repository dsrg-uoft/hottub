#!/bin/bash

# use a stable jvm
#java_home="/usr/lib/jvm/java-7-openjdk-amd64"
#javac="$java_home/bin/javac"
#jar="$java_home/bin/jar"

home=$PWD

cd $home/src
javac -cp asm-5.1.jar:asm-tree-5.1.jar StaticAnalysis.java
if [ $? -ne 0 ]; then
    echo "javac failed"
fi
jar -cf static_analysis.jar StaticAnalysis.class
if [ $? -ne 0 ]; then
    echo "jar failed"
fi
rm StaticAnalysis.class

if [[ ! -d $home/build ]]; then
    mkdir $home/build
fi

cp asm-5.1.jar $home/build
cp asm-tree-5.1.jar $home/build
mv static_analysis.jar $home/build
cp run.sh $home/build
