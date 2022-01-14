#!/bin/bash
################################################################################
# Copyright (c) 2020 ModalAI, Inc. All rights reserved.
################################################################################

TOOLCHAIN32="/opt/cross_toolchain/arm-gnueabi-4.9.toolchain.cmake"
TOOLCHAIN64="/opt/cross_toolchain/aarch64-gnu-4.9.toolchain.cmake"
TOOLCHAIN64_865="/opt/cross_toolchain/aarch64-gnu-8.toolchain.cmake"

mkdir -p build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN64_865} ../
make -j$(nproc)
cd ../
