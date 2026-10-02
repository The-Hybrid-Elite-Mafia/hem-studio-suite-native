#!/bin/sh
# Source from a shell; the project itself has no runtime dependency on these tools.
HEM_ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE:-$0}")/../.." && pwd)
HEM_SYSROOT="$HEM_ROOT/native-build-tools/sysroot"
export PATH="$HEM_ROOT/native-build-tools/python/cmake/data/bin:$PATH"
export CMAKE_PREFIX_PATH="$HEM_SYSROOT/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$HEM_SYSROOT"
export PKG_CONFIG_LIBDIR="$HEM_SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$HEM_SYSROOT/usr/share/pkgconfig"
export CPLUS_INCLUDE_PATH="$HEM_SYSROOT/usr/include:$HEM_SYSROOT/usr/include/x86_64-linux-gnu${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
export LIBRARY_PATH="$HEM_SYSROOT/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
# Keep host runtime libraries unchanged. CMake's generated RPATH may refer to
# explicitly linked local development libraries, but do not globally override libc.
