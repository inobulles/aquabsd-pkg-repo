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
		# compile packages in the right order (some depend on others, and a proper dependcy system would be a tad too complicated)

		for package in $(cat order); do
			echo -e "\tBuilding '$category/$package' ..."

			( cd $package
				if [ -f build.sh ]; then
					sh build.sh

					if [ $(id -u) = 0 ]; then
						pkg install -y *.pkg
					fi

					mv *.pkg $BUILD_DIR
				elif [ -f Makefile ]; then
					make clean

					if [ $(id -u) = 0 ]; then
						make build-depends-list | cut -c12- | xargs pkg install -y
					fi

					make -j$(sysctl -n hw.ncpu) package BATCH=

					if [ $(id -u) = 0 ]; then
						pkg install -y work/pkg/*.pkg
					fi

					mv work/pkg/*.pkg $BUILD_DIR
				else
					echo -e "\tDon't know how to build $package 😢"
				fi
			)
		done
	)

	shift
done
