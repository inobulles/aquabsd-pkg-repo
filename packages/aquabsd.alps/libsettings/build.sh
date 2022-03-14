#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/lib/

# build the package files

cc -std=c99 -c src/libsettings.c -fPIC -o $BUILD_DIR/libsettings.o
ar rc $BUILD_DIR/package/lib/libsettings.a $BUILD_DIR/libsettings.o

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0