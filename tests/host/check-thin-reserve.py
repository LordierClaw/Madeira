#!/usr/bin/env python3
"""Exercise production thin-reserve opt-in and initialization under mocked VM calls.

This does not validate the experimental allocator's overlapping tails on iOS.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('cc') or shutil.which('clang'))
args = parser.parse_args()
if not args.cc:
    parser.error('Provide --cc /path/to/clang or set CC.')
root = Path(__file__).resolve().parents[2]
source = (root / 'build/ntdll-unix/virtual_ios.c').read_text(encoding='utf-8')
config = source[source.index('#define IOS_THIN_ARENAS'):source.index('/* Like mmap_add_fex_reserved_area:')]
start = source.index('static BOOL ios_thin_reserve(')
reserve = source[start:source.index('\n}\n', start) + 3]
start = source.index('static int ios_thin_find(')
find = source[start:source.index('\n}\n', start) + 3]
start = source.index('static BOOL ios_thin_decommit_fixup(')
decommit = source[start:source.index('\n}\n', start) + 3]
cases = ['default', 'empty', 'off', 'invalid', 'opt-in', 'locking', 'threshold', 'minimum', 'maximum']
cases += ['tail-valid', 'tail-end', 'tail-overrun', 'tail-wrap', 'tail-zero']
with tempfile.TemporaryDirectory(prefix='madeira-thin-') as directory:
    temp = Path(directory)
    (temp / 'thin-config.inc').write_text(config, encoding='utf-8')
    # The harness models Wine's enter/leave calls, not host signal masks.
    (temp / 'thin-reserve.inc').write_text(reserve.replace('sigset_t', 'int'), encoding='utf-8')
    (temp / 'thin-decommit.inc').write_text(find + decommit, encoding='utf-8')
    (temp / 'test.c').write_bytes((root / 'tests/host/thin-reserve.c').read_bytes())
    exe = temp / ('thin-test.exe' if os.name == 'nt' else 'thin-test')
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(temp / 'test.c'), '-o', str(exe)], check=True)
    failed = [case for case in cases if subprocess.run([str(exe), case]).returncode]
if failed:
    raise SystemExit('FAIL: ' + ', '.join(failed))
print(f'PASS: {len(cases)} thin-reserve opt-in/initialization/decommit checks (mock VM, not iOS)')
