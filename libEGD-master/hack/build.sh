#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [ -z "${ROOT_DIR}" ]; then
    echo "ROOT_DIR must be set"
    exit 1
fi
if [ -z "${BIN_DIR}" ]; then
    echo "BIN_DIR must be set"
    exit 1
fi
if [ ! -d "${BIN_DIR}" ]; then
    echo "${BIN_DIR} must be created before running this script"
    exit 1
fi

# Make a build directory and run cmake if we have not already run it
if [ ! -f "${BIN_DIR}/Makefile" ]; then
    pushd ${BIN_DIR}
    cmake ${ROOT_DIR} -DCMAKE_BUILD_TYPE="RelWithDebInfo"
    popd
fi

# Whether or not we had to rerun cmake, build the project
make --no-print-directory -C ${BIN_DIR} -j $(($(nproc) + 1))
