#!/usr/bin/env python3
"""Verify unsigned graphics before use, then record final signed plugin hashes."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
if not __debug__:
    raise SystemExit('Do not run provenance checks with Python optimization')
mode, directory = sys.argv[1:]
app = Path(directory)
graphics = json.loads((app / 'gl/build-info.json').read_text())
state = json.loads((ROOT / 'docs/upstream-state.json').read_text())
for key, value in state['graphics'].items():
    assert graphics[key] == value, 'Unexpected graphics source: ' + key
assert graphics['patches'] == {p.name: sha(p) for p in (ROOT / 'build/mesa-ios/patches').glob('*.patch')}
if mode == 'unsigned':
    for name in ('libOSMesa.dylib', 'libMoltenVK.dylib'):
        assert sha(app / 'gl' / name) == graphics['unsigned_plugins'][name], name
elif mode == 'signed':
    runtime = json.loads((app / 'build-info/runtime.json').read_text())
    pins = {repo: subprocess.check_output(['git', 'rev-parse', 'HEAD:' + repo], text=True).strip()
            for repo in ('wine', 'FEX', 'dxmt', 'madeira-dock')}
    assert all(runtime['pins'][repo] == pin for repo, pin in pins.items()), 'Runtime source differs'
    assert runtime['pins'] == state['runtime_pins'], 'Update the recorded runtime pins'
    assert set(state['required_rebuilt_files']) <= runtime['rebuilt_files'].keys()
    for path, digest in runtime['rebuilt_files'].items():
        assert sha(app / path) == digest, path
    info = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
            'runtime_receipt_sha256': sha(app / 'build-info/runtime.json'),
            'graphics_receipt_sha256': sha(app / 'gl/build-info.json'),
            'signed_plugins': {name: sha(app / 'gl' / name) for name in ('libOSMesa.dylib', 'libMoltenVK.dylib')}}
    (app / 'build-info/app.json').write_text(json.dumps(info, indent=2) + '\n')
else:
    raise SystemExit('Expected unsigned or signed mode')
