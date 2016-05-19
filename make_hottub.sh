#!/usr/bin/env bash

set -e

# You can set `JVMS_DIR`
# This script will try to copy the built JVM image here if the dir exists
# This script will also create a symlink `java_home` -> `$IMG` (see below)
JVMS_DIR="${JVMS_DIR:-"$HOME/jvms"}"

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$(pwd)"

if [ "$#" != "1" ] && [ "$#" != "2" ]; then
    echo
    echo "Usage: $0 <image name> [<jvm build type>]"
    echo '- <jvm build type> is: release, fastdebug, or slowdebug (see OpenJDK build notes)'
    echo '- Defaults to release build type and only copies debuginfo if not release'
    echo '- Remember to do `bash configure --with-debug-level=<jvm build type>`'
    echo
    exit 1
fi

IMG="j2sdk-image.$1"
TYPE="${2:-release}"
DIR="build/linux-x86_64-normal-server-$TYPE"
BUILD_HOME="$DIR/images/j2sdk-image"

echo "=== Building $IMG of type $TYPE"

echo "===== Building static analysis"
cd hottub/static_analysis
make
cd -
echo

echo "===== Building client"
cd hottub/client
make
cd -
echo

# force it to rebuild java binary
rm -f "$BUILD_HOME/bin/java"

echo "===== Building OpenJDK"

make images "CONF=linux-x86_64-normal-server-$TYPE"
echo

echo "===== Extracting debuginfo"
cp -r "$DIR"/jdk/objs/java_objs/java.debuginfo "$BUILD_HOME"/bin/
if [[ "$TYPE" == *"debug" ]]; then
    pushd "$BUILD_HOME"/jre/lib/amd64/server
    unzip -o libjvm.diz
    popd
fi
echo

echo "=== Finishing up"
rm -rf "$BUILD_HOME/hottub"
mkdir -p "$BUILD_HOME/hottub/data"

if [ -d "$IMG" ]; then
    echo "- Local image $IMG exists, replacing"
    rm -rf "$IMG"
fi

if [ -d "$JVMS_DIR" ]; then
    if [ -d "$JVMS_DIR/$IMG" ]; then
        echo "- Image $IMG in JVMs dir $JVMS_DIR exists, replacing"
        rm -rf "$JVMS_DIR/$IMG"
    fi
fi

# client
mv "$BUILD_HOME/bin/java" "$BUILD_HOME/bin/java_real"
cp hottub/client/java "$BUILD_HOME/bin"
cp hottub/client/spoonjvm "$BUILD_HOME/bin"

# static analysis
cp -r hottub/static_analysis/build $BUILD_HOME/hottub/static_analysis

cp -r "$BUILD_HOME" "$IMG"

if [ -d "$HOME/jvms" ]; then
    cp -r "$BUILD_HOME" "$JVMS_DIR/$IMG"

    if [ -e "$JVMS_DIR/java_home" ]; then
        rm $HOME/jvms/java_home
    fi
    ln -s  "$JVMS_DIR/$IMG" "$JVMS_DIR/java_home"
fi

echo "Done."
echo
