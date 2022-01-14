#!/bin/bash
################################################################################
# Copyright (c) 2022 ModalAI, Inc. All rights reserved.
################################################################################

# Script to install build dependencies in voxl-cross docker image

# list all your dependencies here. Note for packages that have AMD64 equivalents
# in the ubuntu repositories you should specify the arm64 architecture to make
# sure the correct one is installed in voxl-cross.
DEPS="
libmodal-pipe
libmodal-json
libmodal-exposure
libvoxl-cutils"


# repo options.Use deb-deb for everything right now since that's all that's up
# until the first stable SDK 1.0 release is finished
STABLE="deb [trusted=yes] http://voxl-packages.modalai.com/dev-deb/ ./"
DEV="deb [trusted=yes] http://voxl-packages.modalai.com/dev-deb/ ./"


# parse dev or stable option
if [ "$1" == "stable" ]; then
	echo "using stable repository"
	LINE="$STABLE"

elif [ "$1" == "dev" ]; then
	echo "using development repository"
	LINE="$DEV"

else
	echo ""
	echo "Please specify if the build dependencies should be pulled from"
	echo "the stable or development modalai opkg package repos."
	echo "If building the master branch you should specify stable."
	echo "For development branches please specify dev."
	echo ""
	echo "./install_build_deps.sh stable"
	echo "./install_build_deps.sh dev"
	echo ""
	exit 1
fi

# write in the new entry
DPKG_FILE="/etc/apt/sources.list.d/modalai.list"
sudo echo "${LINE}" > ${DPKG_FILE}

## make sure we have the latest package index
## only pull from voxl-packages to save time
sudo apt-get update -o Dir::Etc::sourcelist="sources.list.d/modalai.list" -o Dir::Etc::sourceparts="-" -o APT::Get::List-Cleanup="0"

## install the user's list of dependencies
if [ -n "$DEPS" ]; then
	echo "installing: "
	echo $DEPS
	sudo apt-get install -y $DEPS
fi

echo ""
echo "Done installing dependencies"
echo ""
exit 0
