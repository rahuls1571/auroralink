# Copyright © 2017 GE Global Research. All rights reserved.
#
# The copyright to the computer software herein is the property
# of GE Global Research. The software may be used and/or copied
# only with the written permission of GE Global Research or in
# accordance with the terms and conditions stipulated in the
# agreement/contract under which the software has been supplied.
ARG ARCH

FROM debian:bookworm-slim

# Add the GE cert to the list of trusted certs to make the APK happy
ENV GE_CRT_NAME "GE_External_Root_CA_2_1.crt"
COPY hack/docker/${GE_CRT_NAME} /etc/ssl/certs/${GE_CRT_NAME}
RUN set -ex \
    && cat /etc/ssl/certs/${GE_CRT_NAME} >> /etc/ssl/cert.pem

# Install generic build and test dependencies
ENV DEBIAN_FRONTEND noninteractive
RUN set -ex \
    && apt-get update && apt-get install -y \
        gdb \
        tcl \
        tar \
        git \
        curl \
        lcov \
        cmake \
        netcat-traditional \
        python3 \
        libtool \
        doxygen \
        autoconf \
        automake \
        cppcheck \
        libz-dev \
        zlib1g-dev \
        libssl-dev \
        build-essential \
        ca-certificates \
    && ln -s /usr/bin/python3 /usr/bin/python

# Download and set up cpplint
RUN set -ex \
    && git clone https://github.com/google/styleguide.git /edge-test/styleguide

# Download and set up google test
ENV GOOGLETEST_VERSION 1.8.1
RUN set -ex \
    && curl -fsSL https://github.com/google/googletest/archive/release-${GOOGLETEST_VERSION}.tar.gz -o /edge-test/googletest-release-${GOOGLETEST_VERSION}.tar.gz \
    && mkdir /edge-test/googletest \
	&& tar -xzf /edge-test/googletest-release-${GOOGLETEST_VERSION}.tar.gz -C /edge-test/googletest --strip-components 1 \
	&& rm /edge-test/googletest-release-${GOOGLETEST_VERSION}.tar.gz

# Symlink /usr/lib64 to /usr/lib to standardize the installation on all architectures
RUN set -ex \
    && ln -s /usr/lib /usr/lib64

# Install the libssh2 static library since the one that alpine has needs libressl
ENV LIB_SSH_NAME libssh2
ENV LIB_SSH_VERSION 1.9.0
RUN set -ex \
    && curl -fsSLo /tmp/${LIB_SSH_NAME}-${LIB_SSH_VERSION}.tar.gz https://github.com/libssh2/${LIB_SSH_NAME}/archive/${LIB_SSH_NAME}-${LIB_SSH_VERSION}.tar.gz \
    && tar -xzf /tmp/${LIB_SSH_NAME}-${LIB_SSH_VERSION}.tar.gz -C /tmp/ \
    && mkdir /tmp/${LIB_SSH_NAME}-${LIB_SSH_NAME}-${LIB_SSH_VERSION}/build \
    && cd /tmp/${LIB_SSH_NAME}-${LIB_SSH_NAME}-${LIB_SSH_VERSION}/build \
    && cmake \
        -DCMAKE_INSTALL_PREFIX="/usr" \
        -DCMAKE_BUILD_TYPE="Release" \
        -DENABLE_DEBUG_LOGGING="OFF" \
        /tmp/${LIB_SSH_NAME}-${LIB_SSH_NAME}-${LIB_SSH_VERSION} \
    && make \
    && make install

# Install a version of libpsl and libnghttp2 that compiles using fPIC so we can include it in our shared object
ENV PSL_VERSION 0.21.0
ENV NGHTTP2_VERSION 1.40.0
RUN set -ex \
    && curl -fsSLo /tmp/libpsl-${PSL_VERSION}.tar.gz https://github.com/rockdaboot/libpsl/releases/download/libpsl-${PSL_VERSION}/libpsl-${PSL_VERSION}.tar.gz \
    && tar -xzf /tmp/libpsl-${PSL_VERSION}.tar.gz -C /tmp/ \
    && cd /tmp/libpsl-${PSL_VERSION} \
    && ./configure \
       	--prefix=/usr/local/ \
       	--with-pic \
    && make -j $(($(nproc) + 1)) \
    && make install \
    \
    && curl -fsSLo /tmp/nghttp2-${NGHTTP2_VERSION}.tar.gz https://github.com/nghttp2/nghttp2/releases/download/v${NGHTTP2_VERSION}/nghttp2-${NGHTTP2_VERSION}.tar.gz \
    && tar -xzf /tmp/nghttp2-${NGHTTP2_VERSION}.tar.gz -C /tmp/ \
    && cd /tmp/nghttp2-${NGHTTP2_VERSION} \
    && ./configure \
       	--prefix=/usr/local/ \
       	--with-pic \
    && make -j $(($(nproc) + 1)) \
    && make install

# Install a slimmed down version of curl that will only use HTTP so our required libraries are much smaller
ENV CURL_VERSION 7.67.0
RUN set -ex \
    && curl -fsSLo /tmp/curl-${CURL_VERSION}.tar.gz https://github.com/curl/curl/releases/download/curl-7_67_0/curl-${CURL_VERSION}.tar.gz \
    && tar -xzf /tmp/curl-${CURL_VERSION}.tar.gz -C /tmp/ \
    && cd /tmp/curl-${CURL_VERSION} \
    && ./configure \
       	--prefix=/usr/local/ \
       	--enable-ipv6 \
       	--enable-unix-sockets \
       	--without-libidn \
       	--without-libidn2 \
       	--without-librtmp \
       	--without-brotli \
       	--disable-ldap \
       	--with-pic \
    && make -j $(($(nproc) + 1)) \
    && make install