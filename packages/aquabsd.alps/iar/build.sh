#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/bin/

( cd $BUILD_DIR
	# build the package files

	git clone https://github.com/inobulles/iar --branch main --depth 1

	( cd iar/
		# we actually need to install this completely on our system in order to link the 'libiar' shared library
		sh build.sh _
	)

	# move built files to their appropriate place in the package

	mv iar/bin/iar package/usr/bin/iar
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
