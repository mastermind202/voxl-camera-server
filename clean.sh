#!/bin/bash
################################################################################
# Copyright (c) 2020 ModalAI, Inc. All rights reserved.
################################################################################

PACKAGE=$(cat ./package/control | grep "Package" | cut -d' ' -f 2)

sudo rm -rf build*/ 2>/dev/null
rm -rf *.deb 2>/dev/null
rm -rf ${PACKAGE}* 2>/dev/null
rm -rf .bash_history 2>/dev/null

echo ""
echo "Done cleaning"
echo ""