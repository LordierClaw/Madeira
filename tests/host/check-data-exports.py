#!/usr/bin/env python3
"""Test the actual Wine loader helper with mocked PE/JIT mappings (Windows host)."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc',default=os.environ.get('CC') or shutil.which('x86_64-w64-mingw32-clang'))
args=parser.parse_args()
if os.name!='nt':
    parser.error('This harness executes a Windows binary; use a Windows host.')
if not args.cc:
    parser.error('Provide --cc /path/to/x86_64-w64-mingw32-clang.exe.')
root=Path(__file__).resolve().parents[2]
source=(root/'wine/dlls/ntdll/loader.c').read_text()
start=source.index('static FARPROC madeira_msvcp_data_export(')
end=source.index('\n}\n',start)+3
cases=['valid','other-dll','wrong-name-length','no-module','native-x64','code','readonly',
       'unreadable','outside-image','headers','section-end','last-byte','zero-virtual-size',
       'identity','null-map','wrong-reverse-map','disabled']
with tempfile.TemporaryDirectory(prefix='madeira-data-') as directory:
    temp=Path(directory)
    (temp/'data-export-under-test.inc').write_text(source[start:end])
    (temp/'test.c').write_bytes((root/'tests/host/data-export.c').read_bytes())
    executable=temp/'test.exe'
    subprocess.run([args.cc,'-Werror','-Wno-macro-redefined',str(temp/'test.c'),'-o',str(executable)],check=True)
    for case in cases:subprocess.run([str(executable),case],check=True)
print(f'PASS: {len(cases)} actual loader helper checks.')
