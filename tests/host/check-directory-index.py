#!/usr/bin/env python3
"""Compile the actual Wine directory-index header against fault-injectable I/O."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
args = parser.parse_args()
if not args.cc:
    parser.error('Provide --cc or set CC.')
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='madeira-directory-index-') as temp:
    exe = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'wine/dlls/ntdll/unix'),
                    str(root / 'tests/host/directory-index.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    if sys.platform == 'darwin':
        source = (root / 'wine/dlls/ntdll/unix/file.c').read_text()
        start = source.index('struct file_lookup_reparse\n')
        end = source.index('\n}\n', source.index('static NTSTATUS find_file_in_dir(', start)) + 3
        (Path(temp) / 'find.inc').write_text(source[start:end])
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-fsanitize=address,undefined', '-I', str(root / 'wine/dlls/ntdll/unix'),
                        '-I', temp, str(root / 'tests/host/directory-index-darwin.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
