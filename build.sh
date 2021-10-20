#!/bin/sh
set -e

# build all packages

( cd packages/
	for category in $(find -L . -maxdepth 1 -type d -not -name ".*" | cut -c3-); do
		echo "Building packages from the '$category' category ..."

		( cd $category
			for package in $(find -L . -maxdepth 1 -type d -not -name ".*" | cut -c3-); do
				echo -e "\tBuilding '$category/$package' ..."
				
				( cd $package
					sh build.sh
				)
			done
		) 
	done
)