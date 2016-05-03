#!/usr/bin/env bash

set -e
#set -x

if [ "$#" != "1" ] && [ "$#" != "2" ]; then
    echo "usage: ./make_forkjvm.sh <image name> [<jvm build type>]"
    echo "must be run from root folder"
    echo "defaults to release build type and only copies debug if not release"
    exit 1
fi

IMG="j2sdk-image.java8.$1"
if [ -z "$2" ]; then
    TYPE=${TYPE:-'release'}
else
    TYPE=${TYPE:-"$2"}
fi
DIR="build/linux-x86_64-normal-server-$TYPE"

echo "building static analysis"

cd forkjvm/static_analysis
./make.sh
cd -

echo "building client"

cd forkjvm/client
make
cd -

# force it to rebuild java binary
if [ -e "$DIR/images/j2sdk-image/bin/java" ]; then
    rm "$DIR/images/j2sdk-image/bin/java"
fi

echo "building jvm"

make images CONF=linux-x86_64-normal-server-$TYPE

if [ -d "$IMG" ]; then
    echo "Local image $IMG exists"
    rm -rf "$IMG"
fi

if [ -d "$HOME/jvms/$IMG" ]; then
    echo "Image $IMG in ~/jvms/ exists"
    rm -rf "$HOME/jvms/$IMG"
fi

cp -r "$DIR"/jdk/objs/java_objs/java.debuginfo "$DIR"/images/j2sdk-image/bin/

if [[ "$TYPE" == *"debug" ]]; then
    pushd "$DIR"/images/j2sdk-image/jre/lib/amd64/server
    unzip -o libjvm.diz
    popd
fi

JAVA_HOME="$DIR/images/j2sdk-image"

if [ ! -d "$JAVA_HOME/forkjvm" ]; then
    mkdir $JAVA_HOME/forkjvm
    mkdir $JAVA_HOME/forkjvm/data
    mkdir $JAVA_HOME/forkjvm/static_analysis
fi

# client
mv $JAVA_HOME/bin/java $JAVA_HOME/bin/java_real
cp ./forkjvm/client/java $JAVA_HOME/bin
cp ./forkjvm/client/spoonjvm $JAVA_HOME/bin

# sa
cp ./forkjvm/static_analysis/build/* $JAVA_HOME/forkjvm/static_analysis

cp -r "$DIR"/images/j2sdk-image "$IMG"

if [ -d "$HOME/jvms" ]; then
    cp -r "$DIR"/images/j2sdk-image "$HOME/jvms/$IMG"

    if [ -e "$HOME/jvms/java_home" ]; then
        rm $HOME/jvms/java_home
    fi
    ln -s  "$HOME/jvms/$IMG" "$HOME/jvms/java_home"
fi
