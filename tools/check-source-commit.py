#!/usr/bin/env python3
"""Reject new binary/private payloads in staged changes or a commit range.

This intentionally audits the change, not artifacts inherited from upstream.
"""
from pathlib import PurePosixPath
import argparse
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
group = parser.add_mutually_exclusive_group(required=True)
group.add_argument('--staged', action='store_true')
group.add_argument('--range', dest='revision_range')
args = parser.parse_args()
root = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip()

def git(*arguments):
    return subprocess.check_output(['git', '-C', root, *arguments])

if args.staged:
    entries = [('index', p) for p in git('diff','--cached','--name-only','--diff-filter=ACMRT','-z').split(b'\0') if p]
else:
    entries = []
    for commit in git('rev-list', args.revision_range).decode().splitlines():
        entries.extend((commit,p) for p in git('diff-tree','--no-commit-id','--root','-r','--name-only','--diff-filter=ACMRT','-z',commit).split(b'\0') if p)

binary_suffixes = {'.dll','.exe','.dylib','.ipa','.a','.lib','.o','.obj','.pdb','.zip','.7z','.rar','.cab','.msi','.pkg','.p12','.pfx','.mobileprovision','.key','.pem'}
private_names = {'madeira.cfg','madeira-library.json','madeira-controls.json','madeira-control-presets.json'}
secret = re.compile(rb'-----BEGIN (?:[A-Z ]+ )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{50,}')
errors = []
checked = 0
for revision, raw in entries:
    path = raw.decode('utf-8')
    p = PurePosixPath(path)
    listing = git('ls-files','--stage','--',path) if revision == 'index' else git('ls-tree',revision,'--',path)
    if listing.startswith(b'160000 '):
        continue  # a submodule commit, not an embedded payload
    content = git('show', (':' if revision == 'index' else revision+':') + path)
    name = p.name.lower()
    parts = {part.lower() for part in p.parts}
    if p.suffix.lower() in binary_suffixes or b'\0' in content:
        errors.append(f'{revision[:12]} {path}: binary/vendor/signing payload')
    elif name in private_names or (name.startswith(('madeira-log','madeira-input-probe','madeira-data-probe')) and name.endswith('.txt')) or name.startswith('pairing') and p.suffix == '.plist':
        errors.append(f'{revision[:12]} {path}: private configuration/device log')
    elif parts & {'outputs','toolchains','x86_64-vcruntime'}:
        errors.append(f'{revision[:12]} {path}: local build/vendor directory')
    elif secret.search(content):
        errors.append(f'{revision[:12]} {path}: possible credential/private key')
    checked += 1
if errors:
    raise SystemExit('\n'.join(errors))
print(f'Source-only audit PASS: {checked} changed file versions; no binary/vendor/private payloads.')
