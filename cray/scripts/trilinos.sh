#!/bin/bash -x
# bash script to build Trilinos for nalu

pkg=publicTrilinos

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
cd $NALU_BUILD_DIR/packages

if [ -d $pkg ]; then
    rm -rf $pkg
fi

# Download and expand Trilinos to packages
git clone https://forge.us.cray.com/Applications/Trilinos $pkg

# Build Trilinos
cd $pkg
mkdir build
cd build
$NALU_ROOT/cray/scripts/do-configTrilinos
make
make install
