#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR/package/usr/local/bin/

( cd $BUILD_DIR
	# build the package files

	git clone https://github.com/obiwac/x-compositing-wm --branch main --depth 1

	( cd x-compositing-wm/
		cc src/main.c -Isrc -I/usr/local/include -L/usr/local/lib -lX11 -lGL -lGLEW -lXcomposite -lXfixes -lXinerama -lm -o x-compositing-wm
	)

	# move built files to their appropriate place in the package

	mv x-compositing-wm/x-compositing-wm package/usr/local/bin/x-compositing-wm
)

# create the package tarball

pkg create -M manifest.json -p plist -r $BUILD_DIR/package

exit 0