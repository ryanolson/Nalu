#!/bin/bash -x
# bash script to build Cmake for nalu

pkgdir=cmake-3.1.0-rc2

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d $pkgdir ]; then
    rm -rf $pkgdir
fi

# Download and expand Cmake to packages
curl -o cmake-3.1.0-rc2.tar.gz http://www.cmake.org/files/v3.1/cmake-3.1.0-rc2.tar.gz
tar -xzf cmake-3.1.0-rc2.tar.gz

# Build Cmake
cd $pkgdir
./configure --prefix=$NALU_BUILD_DIR/install
gmake
gmake install
