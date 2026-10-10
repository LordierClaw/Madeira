#!/usr/bin/env python3
"""Synthetic archive tests: fresh native products must win over compatibility files."""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import plistlib
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/package-local-ipa.py'
spec = importlib.util.spec_from_file_location('packager', SCRIPT)
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
sha = lambda data: hashlib.sha256(data).hexdigest()
STATE = json.loads((ROOT / 'docs/upstream-state.json').read_text())
SOURCE = 'a' * 40


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='madeira-package-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / 'result.ipa'
        macho = struct.pack('<II', 0xfeedfacf, 0x100000c)
        loader = bytearray(0x51000)
        struct.pack_into('<I', loader, 0x3c, 0x80)
        struct.pack_into('<H', loader, 0x84, 0x8664)
        struct.pack_into('<II', loader, 0x80 + 24 + 32, 0x10000, 0x10000)
        struct.pack_into('<I', loader, 0x80 + 24 + 56, 0x1000)
        markers = b'madeira-data-export\0' + 'MADEIRA_EC_DATA_EXPORTS'.encode('utf-16le')
        loader[0x200:0x200 + len(markers)] = markers
        self.native = {
            'Madeira': b'fresh app stub',
            'Madeira.debug.dylib': b'\0'.join([b'Always keep local', b'Download all missing DLC',
                b'Always use landscape when playing', b'Checking existing files', b'Waiting for data',
                b'MADEIRA_CLICK_HOLD_MS', b'MADEIRA_KEY_HOLD_MS']),
            'Info.plist': plistlib.dumps({'CFBundleIdentifier': 'com.willfaust.madeora',
                'CFBundleShortVersionString': '0.1.3', 'CFBundleVersion': '99'}),
            'PlugIns/MadeiraJITHelper.appex/Info.plist': b'fixture',
            'arm64ec-windows/ntdll.dll': bytes(loader),
            'i386-windows/kernelbase.dll': b'fresh kernelbase',
            'gl/libOSMesa.dylib': macho + b'fresh Mesa',
            'gl/libMoltenVK.dylib': macho + b'fresh MoltenVK',
        }
        for name in pkg.KEEP_WINE_EC:
            self.native['arm64ec-windows/' + name] = b'Wine source fixture ' + name.encode()
        for name in STATE['required_rebuilt_files']:
            self.native.setdefault(name, b'fresh fixture: ' + name.encode())
        graphics = {**STATE['graphics'], 'source_commit': SOURCE,
            'patches': {p.name: sha(p.read_bytes()) for p in (ROOT / 'build/mesa-ios/patches').glob('*.patch')}}
        self.native['gl/build-info.json'] = json.dumps(graphics).encode()
        self.native['build-info/runtime.json'] = json.dumps({'source_commit': SOURCE,
            'pins': STATE['runtime_pins'],
            'rebuilt_files': {name: sha(self.native[name]) for name in STATE['required_rebuilt_files']}}).encode()
        self.update_app_receipt()
        self.compat = {'gl/libOSMesa.dylib': macho + b'old Mesa',
                       'gl/libMoltenVK.dylib': macho + b'old MoltenVK',
                       'i386-windows/kernelbase.dll': b'old kernelbase',
                       'i386-windows/unchanged.dll': b'compat unchanged module'}
        for name in pkg.RUNTIMES:
            data = b'synthetic runtime, no vendor payload: ' + name.encode()
            self.compat['x86_64-vcruntime/' + name] = data
            self.compat['arm64ec-windows/' + name] = data

    def update_app_receipt(self):
        self.native['build-info/app.json'] = json.dumps({'source_commit': SOURCE,
            'runtime_receipt_sha256': sha(self.native['build-info/runtime.json']),
            'graphics_receipt_sha256': sha(self.native['gl/build-info.json']),
            'signed_plugins': {name: sha(self.native['gl/' + name]) for name in ('libOSMesa.dylib', 'libMoltenVK.dylib')}}).encode()

    def run_package(self):
        archive = self.root / 'native.tar.gz'
        with tarfile.open(archive, 'w:gz') as tar:
            for name, data in self.native.items():
                info = tarfile.TarInfo('Madeira.app/' + name)
                info.size = len(data)
                info.mode = 0o755
                tar.addfile(info, io.BytesIO(data))
        compat = self.root / 'compat.ipa'
        with zipfile.ZipFile(compat, 'w') as zip:
            for name, data in self.compat.items(): zip.writestr(pkg.PREFIX + name, data)
        return subprocess.run([sys.executable, str(SCRIPT), '--native-app', str(archive),
            '--compat-ipa', str(compat), '--compat-sha256', sha(compat.read_bytes()),
            '--source-commit', SOURCE, '--output', str(self.output)], capture_output=True, text=True)

    def test_fresh_plugins_and_pe_win(self):
        result = self.run_package()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.output) as zip:
            for name in ['gl/libOSMesa.dylib', 'gl/libMoltenVK.dylib', 'i386-windows/kernelbase.dll']:
                self.assertEqual(zip.read(pkg.PREFIX + name), self.native[name], name)
            self.assertEqual(zip.read(pkg.PREFIX + 'i386-windows/unchanged.dll'), self.compat['i386-windows/unchanged.dll'])
        manifest = json.loads(self.output.with_suffix('.manifest.json').read_text())
        self.assertEqual(manifest['graphics_build']['mesa_version'], '25.0.7')
        self.assertNotIn('i386-windows/kernelbase.dll', manifest['local_compatibility_files'])

    def test_missing_fresh_plugin_fails(self):
        del self.native['gl/libOSMesa.dylib']
        self.assertNotEqual(self.run_package().returncode, 0)
        self.assertFalse(self.output.exists())

    def test_missing_graphics_receipt_fails(self):
        del self.native['gl/build-info.json']
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_wrong_patch_receipt_fails(self):
        info = json.loads(self.native['gl/build-info.json'])
        info['patches'] = {}
        self.native['gl/build-info.json'] = json.dumps(info).encode()
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_tampered_rebuilt_module_fails(self):
        self.native['i386-windows/kernelbase.dll'] = b'unexpected rebuilt file'
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_wrong_graphics_pin_fails(self):
        info = json.loads(self.native['gl/build-info.json'])
        info['moltenvk_commit'] = 'b' * 40
        self.native['gl/build-info.json'] = json.dumps(info).encode()
        self.update_app_receipt()
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_wrong_runtime_pin_fails(self):
        info = json.loads(self.native['build-info/runtime.json'])
        info['pins']['wine'] = 'b' * 40
        self.native['build-info/runtime.json'] = json.dumps(info).encode()
        self.update_app_receipt()
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_incomplete_rebuild_fails(self):
        info = json.loads(self.native['build-info/runtime.json'])
        del info['rebuilt_files']['arm64ec-windows/xtajit64.dll']
        self.native['build-info/runtime.json'] = json.dumps(info).encode()
        self.update_app_receipt()
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_wrong_app_source_fails(self):
        info = json.loads(self.native['build-info/app.json'])
        info['source_commit'] = 'b' * 40
        self.native['build-info/app.json'] = json.dumps(info).encode()
        self.assertNotEqual(self.run_package().returncode, 0)

    def test_tampered_signed_plugin_fails(self):
        self.native['gl/libOSMesa.dylib'] += b'tampered after signing'
        self.assertNotEqual(self.run_package().returncode, 0)


if __name__ == '__main__':
    unittest.main()
