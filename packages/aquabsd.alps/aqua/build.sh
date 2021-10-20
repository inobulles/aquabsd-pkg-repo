#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

( cd $BUILD_DIR
	# build the package files

	git clone https://github.com/inobulles/aqua-unix --branch main --depth 1
	
	( cd aqua-unix/
		sh build.sh --auto-iar --kos --devices --devbranch aquabsd.alps
	)
	
	# create package directories

	mkdir package/usr/bin/
	mkdir -p package/usr/share/aqua/devices/

	# move built files to their appropriate place in the package

	mv aqua-unix/bin/kos package/usr/bin/aqua
	mv aqua-unix/bin/devices/* package/usr/share/aqua/devices/
	
	# the 'aquabsd.alps.ui' device is not (yet) open source, so create a stub for it

	touch package/usr/share/aqua/devices/aquabsd.alps.ui.device
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0