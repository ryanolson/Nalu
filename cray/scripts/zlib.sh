#!/bin/bash -x
# bash script to build zlib for nalu

pkg=zlib-1.2.8

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d $pkg ]; then
    rm -rf $pkg
fi

# Download and expand zlib to packages
curl -o $pkg.tar.gz http://zlib.net/$pkg.tar.gz
tar -xzf $pkg.tar.gz

# Build zlib
cd $pkg
CC=gcc CXX=g++ CFLAGS=-O3 CXXFLAGES=-O3 ./configure --prefix=$NALU_BUILD_DIR/install/
make
make install
