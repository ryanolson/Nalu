#!/bin/bash -x
# bash script to build boost for nalu

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d boost_1_55_0 ]; then
    rm -rf boost_1_55_0
fi

# Boost requires a patch to work on the XC
# Copy a Cray supplied patched tarball
# TODO: download source and patch it
# curl -o boost_1_55_0.tar.gz http://iweb.dl.sourceforge.net/project/boost/boost/1.55.0/boost_1_55_0.tar.gz
tar -xjf $NALU_ROOT/cray/tarballs/boost_1_55_0_cray.tar.bz2

# Compile and 
cd boost_1_55_0
CC=cc CXX=CC ./bootstrap.sh --prefix=$NALU_BUILD_DIR/install --with-libraries=signals,regex,filesystem,system,mpi,serialization,thread,program_options,exception 
./b2 -j 4
./b2 -j 4 install
