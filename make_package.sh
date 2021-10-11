#!/bin/bash
################################################################################
# Copyright (c) 2020 ModalAI, Inc. All rights reserved.
#
# creates an ipk package from compiled ros nodes.
# be sure to build everything first with build.sh in docker
# run this on host pc
# UPDATE VERSION IN CONTROL FILE, NOT HERE!!!
#
################################################################################

set -e # exit on error to prevent bad ipk from being generated

################################################################################
# variables
################################################################################
VERSION=$(cat ./package/control | grep "Version" | cut -d' ' -f 2)
PACKAGE=$(cat ./package/control | grep "Package" | cut -d' ' -f 2)
DEB_NAME=${PACKAGE}_${VERSION}.deb
PACKAGE_BUILD_DIR=${PACKAGE}_${VERSION}

echo ""
echo "Package Name   : " $PACKAGE
echo "Version Number : " $VERSION
echo "Deb     Name   : " $DEB_NAME

################################################################################
# start with a little cleanup to remove old files
################################################################################
sudo rm -rf ${PACKAGE_BUILD_DIR} 2>/dev/null > /dev/null

################################################################################
# Create the package build directory
################################################################################
mkdir -p ${PACKAGE_BUILD_DIR}/DEBIAN

# Copy the debian stuff over
cp -rp package/* ${PACKAGE_BUILD_DIR}/DEBIAN

################################################################################
## copy useful files into data directory
################################################################################

# install as root so the package files will have the right permissions
cd build && sudo make DESTDIR=../${PACKAGE_BUILD_DIR} PREFIX=/usr install   && cd ../

mkdir -p ${PACKAGE_BUILD_DIR}/etc/systemd/system/
sudo cp service/*.service ${PACKAGE_BUILD_DIR}/etc/systemd/system/ 2>/dev/null > /dev/null

################################################################################
# pack the control, data, and final ipk archives
################################################################################

dpkg-deb --build ${PACKAGE_BUILD_DIR}

echo ""
echo DONE