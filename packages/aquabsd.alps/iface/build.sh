#!/bin/sh
set -e

BUILD_DIR=".build"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/bin/

# build the package files
# TODO remove debugging flag

cc -std=c99 -g src/iface.c -o $BUILD_DIR/package/bin/iface -L/lib -liface

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
