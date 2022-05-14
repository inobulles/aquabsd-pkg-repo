#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

# create package directories

mkdir $BUILD_DIR/package/usr/lib/
mkdir $BUILD_DIR/package/usr/include/

cp src/copyfile.h $BUILD_DIR/package/usr/include/

# build the package files

cc -Wall -std=c99 -fPIC -c src/copyfile.c -o $BUILD_DIR/copyfile.o
cc -shared $BUILD_DIR/copyfile.o -o $BUILD_DIR/package/usr/lib/libcopyfile.so

ar rc $BUILD_DIR/package/usr/lib/libcopyfile.a $BUILD_DIR/copyfile.o
ranlib $BUILD_DIR/package/usr/lib/libcopyfile.a

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
