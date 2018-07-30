#!/bin/bash

BDIR=build
GENERATOR=
COMPILER=
BUILD_TYPE=-DCMAKE_BUILD_TYPE=Release
OPTIONS=

function base() {
    #echo "Setup:"
    #echo "COMPILER=$COMPILER"
    #echo "GENERATOR=$GENERATOR"
    #echo "BDIR=$BDIR"

    cmake -H. -B$BDIR \
        -DBISON_EXECUTABLE=/usr/local/Cellar/bison/3.0.4_1/bin/bison \
        -DFLEX_EXECUTABLE=/usr/local/Cellar/flex/2.6.4/bin/flex \
        $COMPILER \
        $GENERATOR \
        $BUILD_TYPE \
        $OPTIONS \
        $*
}

function xcode() {
    BDIR="${BDIR}_xcode"
    GENERATOR="-GXcode"
    OPTIONS="-DCPPAN_USE_CACHE=0"
}

function gcc() {
    BDIR="${BDIR}_gcc8"
    COMPILER="-DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8"
}

function clang() {
    BDIR="${BDIR}_clang"
    CLANG=/usr/local/Cellar/llvm/6.0.0/bin/clang-6.0
    COMPILER="-DCMAKE_C_COMPILER=$CLANG -DCMAKE_CXX_COMPILER=$CLANG"
}

function ninja() {
    BDIR="${BDIR}_ninja"
    GENERATOR="-GNinja"
}

function debug() {
    BDIR="${BDIR}_debug"
    BUILD_TYPE=-DCMAKE_BUILD_TYPE=Debug
}

function release() {
    #BDIR="${BDIR}_release"
    BUILD_TYPE=-DCMAKE_BUILD_TYPE=Release
}

for i in "$@"; do
    eval $i
done

base

echo $BDIR
