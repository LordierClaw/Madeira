#!/bin/bash
# Reproducible OpenGL plugins for the app; no compatibility-IPA binaries.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
mkdir -p research outputs app/Madeira/licenses
MESA_VERSION=25.0.7
MESA_SHA256=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
MOLTENVK_COMMIT=db66022459ffb663aa2b50f6b018bc2e124f5edf # v1.4.2
curl --fail --location --retry 3 "https://archive.mesa3d.org/mesa-$MESA_VERSION.tar.xz" -o outputs/mesa.tar.xz
echo "$MESA_SHA256  outputs/mesa.tar.xz" | shasum -a 256 -c -
tar -xf outputs/mesa.tar.xz -C research
git clone --depth 1 --branch v1.4.2 https://github.com/KhronosGroup/MoltenVK.git research/MoltenVK
test "$(git -C research/MoltenVK rev-parse HEAD)" = "$MOLTENVK_COMMIT"
python3 -m venv research/mesa-venv
research/mesa-venv/bin/python -m pip install meson==1.9.1 ninja==1.13.0 mako==1.3.10 pyyaml==6.0.3 packaging==25.0
xcodebuild -downloadComponent MetalToolchain
bash build/moltenvk-ios/build.sh
bash build/mesa-ios/build.sh
export MESA_VERSION MESA_SHA256 MOLTENVK_COMMIT
python3 - <<'PY'
from pathlib import Path
import hashlib, json, os, subprocess
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
info = {
    'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'mesa_version': os.environ['MESA_VERSION'], 'mesa_source_sha256': os.environ['MESA_SHA256'],
    'moltenvk_commit': os.environ['MOLTENVK_COMMIT'],
    'patches': {p.name: digest(p) for p in sorted(Path('build/mesa-ios/patches').glob('*.patch'))},
    'unsigned_plugins': {p.name: digest(p) for p in sorted(Path('app/Madeira/gl').glob('*.dylib'))},
}
Path('app/Madeira/gl/build-info.json').write_text(json.dumps(info, indent=2) + '\n')
PY
tar -czf outputs/graphics-ios.tar.gz app/Madeira/gl app/Madeira/licenses
