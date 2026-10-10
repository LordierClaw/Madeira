#!/bin/bash
# Refresh PE components changed by the integrated upstream revisions.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
TC="$ROOT/toolchains/llvm-mingw-20260421-ucrt-macos-universal/bin"
export PATH="$TC:$(brew --prefix bison)/bin:$PATH"
export CMAKE_BUILD_PARALLEL_LEVEL=3
git submodule update --init --recursive --depth 1 dxmt
python3 -m venv research/pe-venv
research/pe-venv/bin/python -m pip install meson==1.9.1 ninja==1.13.0
export PATH="$ROOT/research/pe-venv/bin:$PATH"
xcodebuild -downloadComponent MetalToolchain

# Wine builtins newly supplied upstream, plus the changed win32u entry points.
# The msvc-named modules here are Wine source implementations, not Microsoft.
modules=(win32u cabinet cryptsp d3dx11_43 gdiplus hnetcfg mlang msasn1 msctf
    msftedit msvcp110 msvcr110 msxml3 msxml6 riched20 riched32 sspicli usp10
    wbemdisp wbemprox wldp wmiutils xaudio2_7 xmllite dbghelp)
WINE_BUILD_DIR="$ROOT/wine/build-macos" JOBS=3 bash build/wine-pe/build-modules.sh "${modules[@]}"
printf 'app/Madeira/arm64ec-windows/%s.dll\n' "${modules[@]}" >> outputs/rebuilt-wine-pe.txt

# FEX has separate PE backends in addition to the native FEXCore archive.
bash build/fex-arm64ec/build.sh
bash build/fex-wow64/build.sh
printf '%s\n' app/Madeira/arm64ec-windows/xtajit64.dll app/Madeira/aarch64-windows/xtajit.dll >> outputs/rebuilt-wine-pe.txt

# Rebuild the changed native WoW64 side against the same Wine pin.
mkdir -p wine/build-aarch64
(cd wine/build-aarch64 && ../configure --enable-archs=aarch64 --without-x --disable-tests)
targets=()
for module in ntdll wow64 wow64win; do targets+=("dlls/$module/aarch64-windows/$module.dll"); done
make -C wine/build-aarch64 -j3 "${targets[@]}"
for module in ntdll wow64 wow64win; do
    "$TC/aarch64-w64-mingw32-strip" --strip-debug -o "app/Madeira/aarch64-windows/$module.dll" "wine/build-aarch64/dlls/$module/aarch64-windows/$module.dll"
    echo "app/Madeira/aarch64-windows/$module.dll" >> outputs/rebuilt-wine-pe.txt
done

# Update affected 32-bit modules; unchanged farm members come from the local
# compatibility input, and packaging must let these fresh members win.
JOBS=3 SKIP_DXMT=1 bash build/wine-i386/build.sh ntdll kernelbase mscoree win32u opengl32 \
    xinput1_1 xinput1_2 xinput1_3 xinput1_4 xinput9_1_0 xinputuap
find app/Madeira/i386-windows -type f ! -name .gitkeep >> outputs/rebuilt-wine-pe.txt

SDKROOT="$(xcrun --sdk macosx --show-sdk-path)" meson setup dxmt/build-arm64ec dxmt \
    --cross-file=dxmt/build-arm64ec-win.txt --native-file=dxmt/build-osx.txt \
    --buildtype=release -Dwine_build_path="$ROOT/wine/build-macos" -Dwine_builtin_dll=true -Denable_nvapi=true
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)" meson compile -C dxmt/build-arm64ec -j3
for module in d3d11/d3d11.dll dxgi/dxgi.dll d3d10/d3d10core.dll winemetal/winemetal.dll nvapi/nvapi64.dll; do
    dest="app/Madeira/arm64ec-windows/$(basename "$module")"
    "$TC/arm64ec-w64-mingw32-strip" --strip-debug -o "$dest" "dxmt/build-arm64ec/src/$module"
    echo "$dest" >> outputs/rebuilt-wine-pe.txt
done
bash build/madeira-d3d12/build-pe.sh
for module in d3d12 madeira_d3d12 d3d12core; do
    cp "build/madeira-d3d12/out-pe/$module.dll" "app/Madeira/arm64ec-windows/$module.dll"
    echo "app/Madeira/arm64ec-windows/$module.dll" >> outputs/rebuilt-wine-pe.txt
done

# The optional LOVE runtime is open-source LuaJIT GC64, built at a fixed pin.
git clone https://github.com/LuaJIT/LuaJIT.git research/LuaJIT
git -C research/LuaJIT checkout c6ffc141a8762b41703f9287d63d93622a13dd8f
bash build/luajit-x64/build.sh
find app/Madeira/compat/love -type f >> outputs/rebuilt-wine-pe.txt
echo app/Madeira/licenses/LuaJIT-MIT.txt >> outputs/rebuilt-wine-pe.txt

python3 - <<'PY'
from pathlib import Path
import hashlib, json, struct, subprocess
paths = sorted(set(Path('outputs/rebuilt-wine-pe.txt').read_text().splitlines()) |
               {'app/Madeira/arm64ec-windows/ntdll.dll', 'app/Madeira/arm64ec-windows/opengl32.dll',
                'app/Madeira/arm64ec-windows/dockhost.exe'})
for path in paths:
    p = Path(path)
    if p.name == 'ntdll.dll':
        data = p.read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        size = struct.unpack_from('<I', data, pe + 24 + 56)[0] + 0x50000
        assert len(data) <= size, path
        p.write_bytes(data.ljust(size, b'\0'))
info = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'pins': {repo: subprocess.check_output(['git', '-C', repo, 'rev-parse', 'HEAD'], text=True).strip()
                 for repo in ['wine', 'FEX', 'dxmt', 'madeira-dock', 'research/LuaJIT']},
        'rebuilt_files': {str(Path(p).relative_to('app/Madeira')).replace('\\', '/'):
                          hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in paths}}
Path('app/Madeira/build-info').mkdir(exist_ok=True)
Path('app/Madeira/build-info/runtime.json').write_text(json.dumps(info, indent=2) + '\n')
Path('outputs/rebuilt-wine-pe.txt').write_text('\n'.join(paths + ['app/Madeira/build-info/runtime.json']) + '\n')
PY
