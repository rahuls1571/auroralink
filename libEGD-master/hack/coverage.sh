#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [ -z "${ARCH}" ]; then
    echo "ARCH must be set"
    exit 1
fi
if [ -z "${VERSION}" ]; then
    echo "VERSION must be set"
    exit 1
fi
if [ -z "${NAME}" ]; then
    echo "NAME must be set"
    exit 1
fi
if [ -z "${SRC_DIRS}" ]; then
    echo "SRC_DIRS must be set"
    exit 1
fi

for SRC_DIR in ${SRC_DIRS}; do
    make -C ${SRC_DIR} coverage
done
