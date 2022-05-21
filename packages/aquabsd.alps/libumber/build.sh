#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

( cd $BUILD_DIR
	# build the package files

	git clone https://github.com/inobulles/umber --branch main --depth 1

	( cd umber/
		sh build.sh _
	)

	# create package directories

	mkdir package/usr/lib/

	# move built files to their appropriate place in the package

	mv umber/bin/libumber.a  package/usr/lib/libumber.a
	mv umber/bin/libumber.so package/usr/lib/libumber.so
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
