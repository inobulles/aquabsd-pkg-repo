#!/bin/sh
set -e

# build the package files

rm -rf .build/
mkdir .build/

# create the package tarball

pkg create -M manifest.json -p plist -r .build

exit 0