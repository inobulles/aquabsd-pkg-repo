#!/bin/sh
set -e

BUILD_DIR=".build/"

rm -rf $BUILD_DIR
mkdir $BUILD_DIR

BUILD_DIR=$(realpath $BUILD_DIR)

# go through each category (passed as arguments)

if [ $# = 0 ]; then
	echo "You must specify one or more categories to build"
	exit 1
fi

while test $# -gt 0; do
	category="packages/$1"

	if [ ! -d $category ]; then
		echo "Can't find the $1 category 😢"
		exit 1
	fi

	echo "Building packages from the '$1' category ..."

	( cd $category
		for package in $(find -L . -maxdepth 1 -type d -not -name ".*" | cut -c3-); do
			echo -e "\tBuilding '$category/$package' ..."

			( cd $package
				sh build.sh
				mv *.pkg $BUILD_DIR
			)
		done
	)

	shift
done