#!/bin/bash -x
# bash script to build libxml2 for nalu

pkg=libxml2-2.9.2

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d $pkg ]; then
    rm -rf $pkg
fi

# Download and expand libxml2 to packages
curl -o $pkg.tar.gz http://www.xmlsoft.org/sources/$pkg.tar.gz
tar -xzf $pkg.tar.gz

# Build libxml2
cd $pkg
CC=cc CXX=CC ./configure --prefix=$NALU_BUILD_DIR/install --disable-shared --without-python
make
make -k install
