#!/usr/bin/env python3
"""Build diagnostic EXEs from source with llvm-mingw; vendors no MSVC binaries."""
from pathlib import Path
import argparse
import os
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--toolchain',type=Path,required=True,help='llvm-mingw bin directory')
parser.add_argument('--out',type=Path,default=Path(__file__).resolve().parent/'out')
args=parser.parse_args()
here=Path(__file__).resolve().parent
args.out.mkdir(parents=True,exist_ok=True)
suffix='.exe' if os.name=='nt' else ''
cc=args.toolchain/('x86_64-w64-mingw32-clang'+suffix)
dlltool=args.toolchain/('llvm-dlltool'+suffix)
common=[str(cc),'-Os','-fno-builtin','-fno-stack-protector','-nostdlib','-Wl,--entry,mainCRTStartup','-Wall','-Wextra','-Werror']
subprocess.run(common+['-Wl,--subsystem,windows',str(here/'MadeiraInputProbe.c'),'-lkernel32','-luser32','-lgdi32','-lwinmm','-o',str(args.out/'MadeiraInputProbe.exe')],check=True)
library=args.out/'probe-msvcp.a'
subprocess.run([str(dlltool),'-m','i386:x86-64','-d',str(here/'probe-msvcp.def'),'-l',str(library)],check=True)
for static in [False,True]:
    output=args.out/('MadeiraDataProbeStatic.exe' if static else 'MadeiraDataProbe.exe')
    extra=['-DSTATIC_PROBE',str(library)] if static else []
    subprocess.run(common+['-Wl,--subsystem,console',str(here/'MadeiraDataProbe.c')]+extra+['-lkernel32','-o',str(output)],check=True)
print(f'Built three diagnostic executables in {args.out}. Do not commit these outputs.')
