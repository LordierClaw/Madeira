#!/bin/bash
# Native dependencies for a fresh, unsigned Debug app. No Microsoft runtime.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
export CMAKE_BUILD_PARALLEL_LEVEL=3
mkdir -p outputs toolchains
trap 'find build -type f \( -name "*.err" -o -name "err-*.txt" -o -name "*-configure.log" -o -name "*-make.log" \) -exec tail -n 35 {} \; >&2' ERR

case "${1:?stage required}" in
fex)
    git submodule update --init --depth 1 FEX
    # Linux conformance binary repositories are not needed for FEXCore on iOS.
    git -C FEX submodule update --init --recursive --depth 1 \
        $(git config -f FEX/.gitmodules --get-regexp path | awk '{print $2}' | grep -vE '(tests-bins|posixtest-bins)')
    cmake -S FEX -B FEX/build-ios -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_SYSTEM_PROCESSOR=arm64 \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=iphoneos \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF \
        -DBUILD_FEX_LINUX_TESTS=OFF -DENABLE_FEX_ALLOCATOR=OFF \
        -DENABLE_ASSERTIONS=OFF -DENABLE_CLANG_THUNKS=ON -DENABLE_CCACHE=ON
    bash build/fex-ios/build.sh
    ;;
media)
    bash build/gnutls-ios/build.sh
    cp toolchains/gnutls-ios/lib/lib{gmp,gnutls,hogweed,nettle}.a app/Madeira/
    bash build/ffmpeg/build.sh
    bash build/freetype-ios/build.sh
    rustup target add aarch64-apple-ios
    bash build/rppairing-ios/build.sh
    ;;
wine)
    git submodule update --init --depth 1 wine madeira-dock
    TC=toolchains/llvm-mingw-20260421-ucrt-macos-universal
    if [ ! -x "$TC/bin/arm64ec-w64-mingw32-clang" ]; then
        curl --fail --location --retry 3 "https://github.com/mstorsjo/llvm-mingw/releases/download/20260421/llvm-mingw-20260421-ucrt-macos-universal.tar.xz" -o toolchains/llvm-mingw.tar.xz
        echo 'bd85a3975723815cef28dbbd2ca2cb0c926f6b348a12a0453f39f7af273cb3f7  toolchains/llvm-mingw.tar.xz' | shasum -a 256 -c -
        tar -xf toolchains/llvm-mingw.tar.xz -C toolchains
    fi
    export PATH="$ROOT/$TC/bin:$(brew --prefix bison)/bin:$(brew --prefix llvm@18)/bin:$PATH"
    mkdir -p wine/build-macos
    if [ ! -f wine/build-macos/config.status ]; then
        (cd wine/build-macos && ../configure --enable-win64 --enable-archs=arm64ec --without-x --disable-tests --enable-winegstreamer)
    fi
    # Generate the IDL headers consumed by Wine's native media and graphics code.
    make -C wine/build-macos -j3 include/all
    bash build/ntdll-unix/build.sh
    bash build/freetype-ios/build.sh
    bash build/win32u-unix/build.sh
    bash build/wineserver/bootstrap-base.sh
    bash build/wineserver/build.sh
    # The PE loader includes our data-export correction. Use top-level targets:
    # Wine's current build tree no longer contains per-DLL Makefiles.
    make -C wine/build-macos -j3 dlls/ntdll/arm64ec-windows/ntdll.dll dlls/opengl32/arm64ec-windows/opengl32.dll
    "$ROOT/$TC/bin/arm64ec-w64-mingw32-strip" wine/build-macos/dlls/ntdll/arm64ec-windows/ntdll.dll
    python3 - <<'PY'
from pathlib import Path
import struct
src = Path('wine/build-macos/dlls/ntdll/arm64ec-windows/ntdll.dll').read_bytes()
pe = struct.unpack_from('<I', src, 0x3c)[0]
target = struct.unpack_from('<I', src, pe + 24 + 56)[0] + 0x50000
assert len(src) <= target
Path('app/Madeira/arm64ec-windows/ntdll.dll').write_bytes(src.ljust(target, b'\0'))
PY
    cp wine/build-macos/dlls/opengl32/arm64ec-windows/opengl32.dll app/Madeira/arm64ec-windows/
    bash build/madeira-dock/build.sh
    ;;
pack)
    # Preserve generated headers and library paths, not intermediate object files.
    find FEX/build-ios -type f \( -name '*.a' -o -name '*.h' -o -name '*.inc' \) > outputs/native-files.txt
    find app/Madeira -maxdepth 1 -name '*.a' >> outputs/native-files.txt
    printf '%s\n' app/Madeira/legal/LICENSES-rppairing-crates.txt \
        app/Madeira/arm64ec-windows/ntdll.dll app/Madeira/arm64ec-windows/opengl32.dll \
        app/Madeira/arm64ec-windows/dockhost.exe app/Madeira/arm64ec-windows/dock-notices.txt >> outputs/native-files.txt
    tar -czf outputs/native-ios.tar.gz -T outputs/native-files.txt
    ;;
*) exit 2 ;;
esac
