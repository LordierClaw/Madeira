#!/usr/bin/env python3
"""Package a freshly built native app with explicitly supplied local runtimes.

The compatibility IPA supplies only the OpenGL plugins, i386 PE farm, and the
previously verified Microsoft runtime. Its native app and loader are never used.
Neither input nor the resulting IPA belongs in Git.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import struct
import tarfile
import zipfile

RUNTIMES = {
    'concrt140.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vcamp140.dll',
    'vccorlib140.dll', 'vcomp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'vcruntime140_threads.dll',
}
KEEP_WINE_EC = {'msvcp140.dll', 'vcruntime140.dll'}
PREFIX = 'Payload/Madeira.app/'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--native-app', type=Path, required=True)
    ap.add_argument('--compat-ipa', type=Path, required=True)
    ap.add_argument('--compat-sha256', required=True)
    ap.add_argument('--source-commit', required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if file_digest(args.compat_ipa) != args.compat_sha256:
        raise SystemExit('Compatibility IPA checksum mismatch')
    if args.output.exists():
        raise SystemExit('Output already exists; choose a new filename')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.ipa.tmp')
    if temporary.exists():
        raise SystemExit('Temporary output already exists')
    overlay = {}
    with zipfile.ZipFile(args.compat_ipa) as compat:
        if len(compat.namelist()) != len(set(compat.namelist())):
            raise SystemExit('Duplicate compatibility ZIP members')
        for entry in compat.infolist():
            if entry.is_dir() or not entry.filename.startswith(PREFIX):
                continue
            rel = entry.filename[len(PREFIX):]
            path = PurePosixPath(rel)
            if '..' in path.parts or '\\' in rel:
                raise SystemExit('Invalid compatibility path')
            if (rel.startswith(('gl/', 'i386-windows/'))
                    or rel.startswith('x86_64-vcruntime/') and path.name in RUNTIMES
                    or rel.startswith('arm64ec-windows/') and path.name in RUNTIMES - KEEP_WINE_EC):
                overlay[rel] = (compat.read(entry), (entry.external_attr >> 16) & 0o777 or 0o644)
        for name in RUNTIMES:
            assert 'x86_64-vcruntime/' + name in overlay, name
        for name in ('libOSMesa.dylib', 'libMoltenVK.dylib'):
            assert struct.unpack_from('<II', overlay['gl/' + name][0]) == (0xfeedfacf, 0x100000c)
        for name in RUNTIMES - KEEP_WINE_EC:
            assert overlay['x86_64-vcruntime/' + name][0] == overlay['arm64ec-windows/' + name][0]

    expected = {}
    native_hashes = {}
    with tarfile.open(args.native_app, 'r:gz') as native, zipfile.ZipFile(
            temporary, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as result:
        seen = set()

        def write(rel, data, mode):
            if rel in expected:
                raise SystemExit('Duplicate output member: ' + rel)
            info = zipfile.ZipInfo(PREFIX + rel)
            info.create_system = 3
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            result.writestr(info, data)
            expected[rel] = digest(data)

        for member in native:
            path = PurePosixPath(member.name)
            if '..' in path.parts or path.is_absolute() or '\\' in member.name or path.parts[0] != 'Madeira.app':
                raise SystemExit('Invalid native app archive member: ' + member.name)
            if member.isdir():
                continue
            if not member.isfile():
                raise SystemExit('Unsupported native app archive link: ' + member.name)
            rel = str(PurePosixPath(*path.parts[1:]))
            if rel in seen:
                raise SystemExit('Duplicate native member: ' + rel)
            seen.add(rel)
            # Re-signing is required; old seals/profiles are not reusable.
            if '_CodeSignature' in path.parts or path.name == 'embedded.mobileprovision' or path.name.startswith('._'):
                continue
            data = native.extractfile(member).read()
            native_hashes[rel] = digest(data)
            if rel not in overlay:
                write(rel, data, member.mode & 0o777)
        for rel, (data, mode) in sorted(overlay.items()):
            write(rel, data, mode)

    with zipfile.ZipFile(temporary) as result:
        assert result.testzip() is None
        assert len(result.namelist()) == len(expected)
        for rel, sha in expected.items():
            assert digest(result.read(PREFIX + rel)) == sha, rel
        info = plistlib.loads(result.read(PREFIX + 'Info.plist'))
        assert info['CFBundleIdentifier'] == 'com.willfaust.madeora'
        assert expected['Madeira'] == native_hashes['Madeira']
        app_code = result.read(PREFIX + 'Madeira.debug.dylib')
        assert expected['Madeira.debug.dylib'] == native_hashes['Madeira.debug.dylib']
        for text in (b'Always keep local', b'Download all missing DLC', b'Always use landscape when playing',
                     b'MADEIRA_CLICK_HOLD_MS', b'MADEIRA_KEY_HOLD_MS'):
            assert text in app_code, 'New UI missing from native code: ' + repr(text)
        loader = result.read(PREFIX + 'arm64ec-windows/ntdll.dll')
        assert expected['arm64ec-windows/ntdll.dll'] == native_hashes['arm64ec-windows/ntdll.dll']
        pe = struct.unpack_from('<I', loader, 0x3c)[0]
        assert struct.unpack_from('<H', loader, pe + 4)[0] == 0x8664
        assert struct.unpack_from('<II', loader, pe + 24 + 32) == (0x10000, 0x10000)
        assert len(loader) == struct.unpack_from('<I', loader, pe + 24 + 56)[0] + 0x50000
        assert b'madeira-data-export' in loader
        assert 'MADEIRA_EC_DATA_EXPORTS'.encode('utf-16le') in loader
        for name in KEEP_WINE_EC:
            assert expected['arm64ec-windows/' + name] == native_hashes['arm64ec-windows/' + name]
        assert PREFIX + 'PlugIns/MadeiraJITHelper.appex/Info.plist' in result.namelist()
    temporary.replace(args.output)
    manifest = {
        'source_commit': args.source_commit,
        'native_app_sha256': file_digest(args.native_app),
        'compatibility_ipa_sha256': args.compat_sha256,
        'ipa_sha256': file_digest(args.output),
        'ipa_bytes': args.output.stat().st_size,
        'version': info['CFBundleShortVersionString'],
        'build': info['CFBundleVersion'],
        'configuration': 'Debug; fresh native app, libraries and JIT helper',
        'native_code_preserved': True,
        'rebuilt_loader_preserved': True,
        'local_compatibility_files': {rel: expected[rel] for rel in sorted(overlay)},
        'validation': 'ZIP integrity, all file hashes, new UI strings, loader architecture/alignment/data-export fix; device check pending',
        'signing': 'Unsigned app; re-sign through the sideload tool before installing',
    }
    args.output.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    args.output.with_suffix('.ipa.sha256').write_text(manifest['ipa_sha256'] + '  ' + args.output.name + '\n')
    print(json.dumps({k: v for k, v in manifest.items() if k != 'local_compatibility_files'}, indent=2))


if __name__ == '__main__':
    main()
