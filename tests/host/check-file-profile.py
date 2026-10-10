#!/usr/bin/env python3
"""Exercise the production bounded file profiler with a deterministic clock."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
args = p.parse_args()
if not args.cc:
    p.error('Provide --cc or set CC.')
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='madeira-file-profile-') as directory:
    exe = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'wine/dlls/ntdll/unix'),
                    str(root / 'tests/host/file-profile.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
