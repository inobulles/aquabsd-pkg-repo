#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

# create package directories

mkdir $BUILD_DIR/package/usr/lib/
mkdir $BUILD_DIR/package/usr/include/

cp src/mkfs_ufs.h $BUILD_DIR/package/usr/include/

# build the package files

cc -std=c99 -fPIC -c src/mkfs_ufs.c -o $BUILD_DIR/mkfs_ufs.o
ld -shared $BUILD_DIR/mkfs_ufs.o -o $BUILD_DIR/package/usr/lib/libmkfs_ufs.so

ar rc $BUILD_DIR/package/usr/lib/libmkfs_ufs.a $BUILD_DIR/mkfs_ufs.o
ranlib $BUILD_DIR/package/usr/lib/libmkfs_ufs.a

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
