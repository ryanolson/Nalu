#!/bin/bash -x
# bash script to build SuperLU for nalu

pkgdir=SuperLU_4.3

# Work in the packages directory
mkdir -p $NALU_BUILD_DIR/packages
mkdir -p $NALU_BUILD_DIR/install/SuperLU_4.3/lib
mkdir -p $NALU_BUILD_DIR/install/SuperLU_4.3/include
cd $NALU_BUILD_DIR/packages

if [ -d $pkgdir ]; then
    rm -rf $pkgdir
fi

# Download and expand SuperLU to packages
curl -o superlu_4.3.tar.gz http://crd-legacy.lbl.gov/~xiaoye/SuperLU/superlu_4.3.tar.gz
tar -xzf superlu_4.3.tar.gz

# Build SuperLU
cd $pkgdir
mkdir -p install/SuperLU_4.3/lib
mkdir -p install/SuperLU_4.3/include
cp $NALU_ROOT/cray/patches/make.superlu.cray make.inc
make
cp SRC/*.h $NALU_BUILD_DIR/install/SuperLU_4.3/include
