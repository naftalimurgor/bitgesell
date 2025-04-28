#!/bin/bash

set -e

# Log file for capturing output
LOG_FILE=boost_build.log
exec > >(tee -a "$LOG_FILE") 2>&1

BOOST_VERSION=1.69.0
BOOST_VERSION_UNDERSCORE=1_69_0
BOOST_SRC=boost_${BOOST_VERSION_UNDERSCORE}
BOOST_DIR=/tmp/boost
INSTALL_PREFIX=/work/depends/${HOST}
THREAD_COUNT=$(nproc)

# Define environment variables for the Android NDK if not already set
if [ -z "$ANDROID_TOOLCHAIN_BIN" ]; then
    echo "ERROR: ANDROID_TOOLCHAIN_BIN is not set. Please set it to the path of your Android NDK toolchain binaries."
    exit 1
fi

if [ -z "$ANDROID_API_LEVEL" ]; then
    echo "ERROR: ANDROID_API_LEVEL is not set. Please set it to the desired Android API level."
    exit 1
fi

echo "Starting Boost ${BOOST_VERSION} build for Android..."

# Ensure boost directory exists
mkdir -p "$BOOST_DIR" && cd "$BOOST_DIR"

BOOST_TAR=${BOOST_SRC}.tar.bz2
BOOST_URL=https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_TAR}

# Download Boost source if not already present
if [ ! -f ${BOOST_TAR} ]; then
    echo "Downloading Boost from ${BOOST_URL}..."
    wget -O ${BOOST_TAR} ${BOOST_URL}
fi

# Sanity check: Ensure downloaded file is a valid bzip2 archive
if ! file ${BOOST_TAR} | grep -q 'bzip2 compressed data'; then
    echo "❌ Error: Downloaded file is not a valid bzip2 archive."
    echo "💡 Check the URL or your internet connection."
    exit 1
fi

# Extract the Boost source code
rm -rf ${BOOST_SRC}
tar xf ${BOOST_TAR}
cd ${BOOST_SRC}

# Bootstrap the build system
echo "Running bootstrap..."
./bootstrap.sh

# Ensure the ANDROID_TOOLCHAIN_BIN and ANDROID_API_LEVEL variables are passed correctly
echo "Generating user-config.jam..."

cat <<EOF | tee user-config.jam
using clang : android : \\
  ${ANDROID_TOOLCHAIN_BIN}/clang++ \\
  : <archiver>${ANDROID_TOOLCHAIN_BIN}/llvm-ar \\
    <compileflags>--target=${HOST}${ANDROID_API_LEVEL} \\
    <compileflags>-fPIC -std=c++11 -frtti -fexceptions \\
    <linkflags>--target=${HOST}${ANDROID_API_LEVEL} \\
    <linkflags>-static-libstdc++ ;
EOF

# Start Boost build with b2 and enable verbose output for debugging
echo "Starting Boost build with b2..."
./b2 \
  toolset=clang-android \
  target-os=android \
  threadapi=pthread \
  threading=multi \
  runtime-link=static \
  link=static \
  --with-system \
  --with-thread \
  --with-chrono \
  --with-filesystem \
  --with-date_time \
  --prefix="${INSTALL_PREFIX}" \
  --user-config=./user-config.jam \
  install -j${THREAD_COUNT} -v

echo "✅ Boost built and installed to ${INSTALL_PREFIX}"
