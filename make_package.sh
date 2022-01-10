#!/bin/bash
################################################################################
# Copyright (c) 2022 ModalAI, Inc. All rights reserved.
#
# Semi-universal script for making a deb and ipk package. This is shared
# between the vast majority of VOXL-SDK packages
#
# Add the 'timestamp' argument to add a date-timestamp suffix to the deb package
# version. This is used by CI for making nightly and development builds.
#
# author: james@modalai.com
################################################################################

set -e # exit on error to prevent bad ipk from being generated

################################################################################
# Check arguments
################################################################################

USETIMESTAMP=false

## convert argument to lower case for robustness
arg=$(echo "$1" | tr '[:upper:]' '[:lower:]')
case ${arg} in
	"")
		#echo "Making Normal Package"
		;;
	"-t"|"timestamp"|"--timestamp")
		echo "using timestamp suffix"
		USETIMESTAMP=true
		;;
	*)
		echo "invalid option"
		exit 1
esac


################################################################################
# variables
################################################################################
VERSION=$(cat pkg/control/control | grep "Version" | cut -d' ' -f 2)
PACKAGE=$(cat pkg/control/control | grep "Package" | cut -d' ' -f 2)
IPK_NAME=${PACKAGE}_${VERSION}.ipk
DEB_NAME=${PACKAGE}_${VERSION}.deb

DATA_DIR=pkg/data
CONTROL_DIR=pkg/control
IPK_DIR=pkg/IPK
DEB_DIR=pkg/DEB

echo "Package Name: " $PACKAGE
echo "version Number: " $VERSION

################################################################################
# start with a little cleanup to remove old files
################################################################################
# remove data directory where 'make install' installed to
sudo rm -rf $DATA_DIR
mkdir $DATA_DIR

# remove ipk and deb packaging folders
rm -rf $IPK_DIR
rm -rf $DEB_DIR

# remove old ipk and deb packages
rm -f *.ipk
rm -f *.deb


################################################################################
## install compiled stuff into data directory with 'make install'
## try this for all 3 possible build folders, some packages are multi-arch
## so both 32 and 64 need installing to pkg directory.
################################################################################

DID_BUILD=false

if [[ -d "build" ]]; then
	cd build && sudo make DESTDIR=../${DATA_DIR} PREFIX=/usr install && cd -
	DID_BUILD=true
fi
if [[ -d "build32" ]]; then
	cd build32 && sudo make DESTDIR=../${DATA_DIR} PREFIX=/usr install && cd -
	DID_BUILD=true
fi
if [[ -d "build64" ]]; then
	cd build64 && sudo make DESTDIR=../${DATA_DIR} PREFIX=/usr install && cd -
	DID_BUILD=true
fi

# make sure at least one directory worked
if [ "$DID_BUILD" = false ] && [ -f "build.sh" ]; then
	echo "neither build/ build32/ or build64/ were found"
	exit 1
fi


################################################################################
## install standard stuff common across ModalAI projects if they exist
################################################################################

if [ -d "services" ]; then
	sudo mkdir -p $DATA_DIR/etc/systemd/system/
	sudo cp services/* $DATA_DIR/etc/systemd/system/
fi

if [ -d "scripts" ]; then
	sudo mkdir -p $DATA_DIR/usr/bin/
	sudo chmod +x scripts/*
	sudo cp scripts/* $DATA_DIR/usr/bin/
fi

if [ -d "bash_completions" ]; then
	sudo mkdir -p $DATA_DIR/usr/share/bash-completion/completions
	sudo cp bash_completions/* $DATA_DIR/usr/share/bash-completion/completions
fi

if [ -d "misc_files" ]; then
	sudo cp -R misc_files/* $DATA_DIR/
fi

if [ -d "bash_profile" ]; then
	sudo mkdir -p ${DATA_DIR}/home/root/.profile.d/
	sudo cp -R bash_profile/* ${DATA_DIR}/home/root/.profile.d/
fi

################################################################################
# make an IPK
################################################################################

echo "starting building $IPK_NAME"

## make a folder dedicated to IPK building and make the required version file
mkdir $IPK_DIR
echo "2.0" > $IPK_DIR/debian-binary

## add tar archives of data and control for the IPK package
cd $CONTROL_DIR/
tar --create --gzip -f ../../$IPK_DIR/control.tar.gz *
cd ../../
cd $DATA_DIR/
tar --create --gzip -f ../../$IPK_DIR/data.tar.gz *
cd ../../

## use ar to make the final .ipk and place it in the repository root
ar -r $IPK_NAME $IPK_DIR/control.tar.gz $IPK_DIR/data.tar.gz $IPK_DIR/debian-binary


################################################################################
# make a DEB package
################################################################################

echo "starting building Debian Package"

## make a folder dedicated to IPK building and copy the requires debian-binary file in
mkdir $DEB_DIR

## copy the control stuff in
cp -rf $CONTROL_DIR/ $DEB_DIR/DEBIAN
cp -rf $DATA_DIR/*   $DEB_DIR

## update version with timestamp if enabled
if $USETIMESTAMP; then
	dts=$(date +"%Y%m%d%H%M")
	sed -E -i "s/Version.*/&-$dts/" $DEB_DIR/DEBIAN/control
	VERSION="${VERSION}-${dts}"
	DEB_NAME=${PACKAGE}_${VERSION}.deb
	echo "new version with timestamp: $VERSION"
fi


dpkg-deb --build ${DEB_DIR} ${DEB_NAME}

echo "DONE"