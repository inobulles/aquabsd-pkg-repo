#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/include/

cp src/iface.h $BUILD_DIR/package/usr/include/

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0