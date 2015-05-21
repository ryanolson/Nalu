#!/bin/bash -x
# bash script to build libxml2 for nalu

pkg=yaml-cpp-0.3.0

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d $pkg ]; then
    rm -rf $pkg
fi

# Download and expand libxml2 to packages
curl -o $pkg.tar.gz https://yaml-cpp.googlecode.com/files/$pkg.tar.gz
tar -xzf $pkg.tar.gz
mv yaml-cpp $pkg

# Build libxml2
cd $pkg
mkdir build
cd build
cmake -DCMAKE_CC_COMPILER=cc -DCMAKE_CC_COMPILER=CC -DCMAKE_INSTALL_PREFIX=$NALU_BUILD_DIR/install ..
make
make install
