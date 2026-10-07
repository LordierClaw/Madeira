#!/usr/bin/env python3
"""Compile and run the production queue helper with deterministic host mocks."""
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
with tempfile.TemporaryDirectory(prefix='madeira-input-') as directory:
    out = Path(directory) / ('queue-test.exe' if os.name == 'nt' else 'queue-test')
    subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Werror',
                    '-I',str(root/'app/Madeira/Winios'),
                    str(root/'tests/host/input-queue.c'),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
