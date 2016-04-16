#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "usage: ./make_forkjvm.sh <jvm build type> <image name>"
    echo "must be run from jvmperf_openjdk8u"
    exit 1
fi


build_type="$1"
image_name="$2"

echo "building static analysis"

cd forkjvm/static_analysis
./make.sh
if [ $? -ne 0 ]; then
    echo "static analysis make fail"
fi
cd -

echo "building client"

cd forkjvm/client
make
if [ $? -ne 0 ]; then
    echo "client make fail"
fi
cd -

rm build/linux-x86_64-normal-server-$build_type/images/j2sdk-image/bin/java

echo "building jvm"

make images CONF=linux-x86_64-normal-server-$build_type && ./ty_dfg image_name $build_type debuginfo forkjvm
