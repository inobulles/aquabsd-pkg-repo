#!/bin/sh
set -e

BUILD_DIR=".build"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/bin/

# build the package files

cc -std=c99 src/kld.c -o $BUILD_DIR/package/bin/kld -lutil

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0