#!/bin/bash
################################################################################
# Copyright (c) 2020 ModalAI, Inc. All rights reserved.
################################################################################

## voxl-cross contains the following toolchains
## first two for apq8096, last for qrb5165
TOOLCHAIN_APQ8096_32="/opt/cross_toolchain/arm-gnueabi-4.9.toolchain.cmake"
TOOLCHAIN_APQ8096_64="/opt/cross_toolchain/aarch64-gnu-4.9.toolchain.cmake"
TOOLCHAIN_QRB5165="/opt/cross_toolchain/aarch64-gnu-7.toolchain.cmake"


mkdir -p build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_QRB5165} ${EXTRA_OPTS} ../
make -j$(nproc)
cd ../
