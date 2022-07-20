#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/

( cd $BUILD_DIR
	# set some environment variables for building AQUA

	export AQUA_ROOT_PATH=/root/.aqua-root/
	export AQUA_DATA_PATH=/usr/share/aqua/
	export AQUA_BIN_PATH=/usr/bin/

	# build the package files

	git clone https://github.com/inobulles/aqua-unix --branch main --depth 1
	
	( cd aqua-unix/
		sh build.sh --auto-iar --auto-umber --kos --devices --devset aquabsd.alps
	)

	# create package directories

	mkdir package/$AQUA_BIN_PATH
	mkdir -p package/$AQUA_DATA_PATH/devices/

	# move built files to their appropriate place in the package

	mv aqua-unix/bin/kos package/$AQUA_BIN_PATH/aqua
	mv aqua-unix/bin/devices/* package/$AQUA_DATA_PATH/devices/

	# the 'aquabsd.alps.ui' device is not (yet) open source, so create a stub for it

	touch package/$AQUA_DATA_PATH/devices/aquabsd.alps.ui.device
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0
