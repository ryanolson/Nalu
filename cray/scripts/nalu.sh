#!/bin/bash -x
# bash script to build Trilinos for nalu

cp cray/scripts/do-configNalu ./build
cd build
./do-configNalu
make
