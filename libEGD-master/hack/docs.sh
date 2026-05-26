#!/bin/sh

set -o errexit
set -o nounset

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
if [ -z "${DOCS_DIR}" ]; then
    echo "DOCS_DIR must be set"
    exit 1
fi

doxygen doc/Doxyfile
