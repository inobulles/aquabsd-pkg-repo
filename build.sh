#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir $BUILD_DIR

BUILD_DIR=$(realpath $BUILD_DIR)

# build all packages

( cd packages/
	for category in $(find -L . -maxdepth 1 -type d -not -name ".*" | cut -c3-); do
		echo "Building packages from the '$category' category ..."

		( cd $category
			for package in $(find -L . -maxdepth 1 -type d -not -name ".*" | cut -c3-); do
				echo -e "\tBuilding '$category/$package' ..."
				
				( cd $package
					sh build.sh
					mv *.pkg $BUILD_DIR
				)
			done
		) 
	done
)