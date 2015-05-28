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

echo "using intel" >> ./tools/build/v2/user-config.jam
echo "  : " >> ./tools/build/v2/user-config.jam
echo "  : CC" >> ./tools/build/v2/user-config.jam
echo "  : <cxxflags>"-ip -fp-model precise -fp-model source -restrict -DMPICH_SKIP_MPICXX"" >> ./tools/build/v2/user-config.jam
echo "  ;" >> ./tools/build/v2/user-config.jam

pushd tools/build/v2/engine
  ./build.sh gcc
  arch=`./bootstrap/jam0 -d0 -f build.jam --toolset=gcc --toolset-root= --show-locate-target`
popd
cp tools/build/v2/engine/$arch/b2 .
cp tools/build/v2/engine/$arch/bjam .

CC=cc CXX=CC ./bootstrap.sh --prefix=$NALU_BUILD_DIR/install --with-libraries=signals,regex,filesystem,system,mpi,serialization,thread,program_options,exception --with-toolset=intel --with-bjam=`pwd`/bjam
./b2 link=static
./b2 link=static install
