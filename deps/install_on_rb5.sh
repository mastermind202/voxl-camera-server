#!/bin/bash
################################################################################
# Copyright (c) 2019 ModalAI, Inc. All rights reserved.
#
# Installs the ipk package on target.
# Requires the ipk to be built and an adb connection.
#
# author: james@modalai.com
################################################################################
set -e

PACKAGE=$(cat DEBIAN/control | grep "Package" | cut -d' ' -f 2)

# count deb files in current directory
NUM_FILES=$(ls -1q $PACKAGE*.deb | wc -l)

if [ $NUM_FILES -eq "0" ]; then
	echo "ERROR: no ipk file found"
	echo "run make_package.sh first"
	exit 1
elif [ $NUM_FILES -gt "1" ]; then
	echo "ERROR: more than 1 deb file found"
	echo "make sure there is only one deb file in the current directory"
	exit 1
fi

# now we know only one deb file exists
FILE=$(ls -1q $PACKAGE*.deb)
echo "pushing $FILE to target"

echo "searching for ADB device"
adb wait-for-device
echo "adb device found"


adb push $FILE /tmp/$FILE
adb shell "dpkg -i --force-downgrade --force-depends /tmp/$FILE"
