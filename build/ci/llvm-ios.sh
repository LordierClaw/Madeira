#!/bin/bash
# Build the LLVM revision documented in BUILDING.md for DXMT's AIR compiler.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
REV=8dfdcc7b7bf66834a761bd8de445840ef68e4d1a
mkdir -p toolchains outputs
if [ ! -f toolchains/llvm-project/llvm/CMakeLists.txt ]; then
    curl --fail --location --retry 3 "https://github.com/llvm/llvm-project/archive/$REV.tar.gz" -o toolchains/llvm-source.tar.gz
    mkdir -p toolchains/llvm-project
    tar -xzf toolchains/llvm-source.tar.gz --strip-components=1 -C toolchains/llvm-project
fi
# LLVM 15 predates its current iOS CMake support; use Apple's linker flags.
python3 - <<'PY'
from pathlib import Path
p = Path('toolchains/llvm-project/llvm/cmake/modules/AddLLVM.cmake')
s = p.read_text()
old = 'if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")'
new = 'if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin|iOS")'
assert s.count(old) == 2 or s.count(new) == 2
p.write_text(s.replace(old, new))
PY
cmake -G Ninja -S toolchains/llvm-project/llvm -B toolchains/llvm-host-build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DLLVM_TARGETS_TO_BUILD= -DLLVM_INCLUDE_TESTS=OFF -DLLVM_ENABLE_ZLIB=OFF
cmake --build toolchains/llvm-host-build --target llvm-tblgen --parallel 3
cmake -G Ninja -S toolchains/llvm-project/llvm -B toolchains/llvm-ios-build \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DLLVM_HOST_TRIPLE=arm64-apple-ios17.0 \
    -DLLVM_DEFAULT_TARGET_TRIPLE=arm64-apple-ios17.0 -DLLVM_TARGET_ARCH=host \
    -DLLVM_TARGETS_TO_BUILD= -DLLVM_ENABLE_PROJECTS= -DLLVM_BUILD_TOOLS=OFF \
    -DLLVM_BUILD_UTILS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_ENABLE_ZLIB=OFF \
    -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_TABLEGEN="$ROOT/toolchains/llvm-host-build/bin/llvm-tblgen"
cmake --build toolchains/llvm-ios-build --parallel 3
tar -czf outputs/llvm-ios.tar.gz toolchains/llvm-project/llvm/include \
    toolchains/llvm-ios-build/include toolchains/llvm-ios-build/lib
