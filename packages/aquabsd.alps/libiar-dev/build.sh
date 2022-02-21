#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

( cd $BUILD_DIR
	# get the package files

	git clone https://github.com/inobulles/iar --branch main --depth 1

	# create package directories

	mkdir package/usr/include/

	# move built files to their appropriate place in the package

	mv iar/src/iar.h package/usr/include/iar.h
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
