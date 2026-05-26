#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [ -z "${ROOT_DIR}" ]; then
    echo "ROOT_DIR must be set"
    exit 1
fi
if [ -z "${NAME}" ]; then
    echo "NAME must be set"
    exit 1
fi
if [ -z "${VERSION}" ]; then
    echo "VERSION must be set"
    exit 1
fi
if [ -z "${ARCH}" ]; then
    echo "ARCH must be set"
    exit 1
fi
if [ -z "${REVISION}" ]; then
    echo "REVISION must be set"
    exit 1
fi
if [ -z "${DEB_FILE}" ]; then
    echo "DEB_FILE must be set"
    exit 1
fi
if [ -z "${DEPENDENCIES}" ]; then
    echo "DEPENDENCIES must be set"
    exit 1
fi

# Form the package name and set some directory names
package_full_name="${NAME}_${VERSION}-${REVISION}"
package_dir="/tmp/${package_full_name}"
rm -rf ${package_dir}
mkdir -p "${package_dir}"
build_dir="/tmp/build-${package_full_name}"
rm -rf ${build_dir}
mkdir -p "${build_dir}"

# Set up a CMake build in the build dir
pushd "${build_dir}"
cmake "${ROOT_DIR}" -DCMAKE_BUILD_TYPE="RELEASE" -DCMAKE_INSTALL_PREFIX="${package_dir}/usr"
make --no-print-directory -C "${build_dir}" -j $(($(nproc) + 1))
make install
popd

# Create the debian control file which will dictate the build
mkdir -p "${package_dir}/DEBIAN"
cat > "${package_dir}/DEBIAN/control" << EOF
Package: ${NAME}
Version: ${VERSION}-${REVISION}
Section: base
Priority: optional
Architecture: ${ARCH}
Depends: ${DEPENDENCIES}
Maintainer: Rob Fisher <robert.p.fisher@ge.com>
Description:
 Python binding for DG framework
EOF

# Create the deb file from the package
dpkg-deb --build "${package_dir}" "${DEB_FILE}"
