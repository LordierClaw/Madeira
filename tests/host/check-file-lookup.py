#!/usr/bin/env python3
"""Compare Wine's real lookup functions with/without the iOS parent shortcut.

The filesystem calls are mocked; this checks paths, statuses, reparse offsets
and syscall counts, not iOS latency or Darkest Dungeon loading time.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang'))
args = parser.parse_args()
if not args.cc:
    parser.error('Provide --cc or set CC.')
root = Path(__file__).resolve().parents[2]
source = (root / 'wine/dlls/ntdll/unix/file.c').read_text(encoding='utf-8')

def function(name):
    start = source.index('static NTSTATUS ' + name + '(')
    return source[start:source.index('\n}\n', start) + 3]

lookup = function('lookup_unix_name')
reference = lookup.replace('lookup_unix_name(', 'reference_lookup_unix_name(', 1)
with tempfile.TemporaryDirectory(prefix='madeira-file-lookup-') as directory:
    temp = Path(directory)
    (temp / 'lookup.inc').write_text(
        function('find_file_in_dir') + '\n#undef WINE_IOS\n' + reference
        + '\n#define WINE_IOS 1\n' + lookup, encoding='utf-8')
    (temp / 'test.c').write_bytes((root / 'tests/host/file-lookup.c').read_bytes())
    exe = temp / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(temp / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
