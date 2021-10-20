#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/lib/

( cd $BUILD_DIR
	# build the package files

	git clone https://github.com/inobulles/iar --branch main --depth 1

	( cd iar/
		# we actually need to install this completely on our system in order to link the 'libiar' shared library
		sh build.sh
	)

	# move built files to their appropriate place in the package

	mv iar/bin/libiar.a package/usr/lib/libiar.a
	mv iar/bin/libiar.so package/usr/lib/libiar.so
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0