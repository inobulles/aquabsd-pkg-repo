#!/bin/sh
set -e

BUILD_DIR=".build"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/bin/

# build the package files

# TODO remove debugging flag
# TODO remove second library path

cc -std=c99 -g src/settings.c -o $BUILD_DIR/package/bin/settings -L/lib -L../libsettings/.build/package/lib -lsettings

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
