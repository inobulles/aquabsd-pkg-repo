#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

# create package directories

mkdir $BUILD_DIR/package/usr/lib/
mkdir $BUILD_DIR/package/usr/include/

cp src/mkfs_msdos.h $BUILD_DIR/package/usr/include/

# build the package files

cc -std=c99 -fPIC -c src/mkfs_msdos.c -o $BUILD_DIR/mkfs_msdos.o
cc -shared $BUILD_DIR/mkfs_msdos.o -o $BUILD_DIR/package/usr/lib/libmkfs_msdos.so

ar rc $BUILD_DIR/package/usr/lib/libmkfs_msdos.a $BUILD_DIR/mkfs_msdos.o
ranlib $BUILD_DIR/package/usr/lib/libmkfs_msdos.a

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
