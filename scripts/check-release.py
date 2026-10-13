#!/usr/bin/env python3
"""Check candidate contents; --public also requires explicit publication readiness."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from publication_files import ROOT, source_files

FORBIDDEN_EXTENSIONS = {'.p12', '.pfx', '.p7b', '.cer', '.pem', '.key', '.keystore',
                        '.hap', '.app', '.so', '.dylib', '.dll', '.a', '.jar', '.har', '.log'}
PATTERNS = {
    'private key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    'personal absolute path': re.compile('/' + r'(?:Users|home)/[A-Za-z0-9_.-]+/'),
    'Windows personal path': re.compile(r'[A-Za-z]:\\Users\\[^\\\s]+\\'),
    'GitHub token': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b'),
    'signing configuration': re.compile(r'"signingConfigs"\s*:\s*\[\s*\{'),
    'signing secret': re.compile(r'"(?:storePassword|keyPassword|storeFile|certpath)"\s*:\s*"[^"\s]+"'),
}


def check(root=ROOT, public=False):
    errors = []
    files = source_files(root)
    for path in files:
        name = path.relative_to(root).as_posix()
        if path.suffix.lower() in FORBIDDEN_EXTENSIONS:
            errors.append(name + ': forbidden binary/signing/log file')
            continue
        raw = path.read_bytes()
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            errors.append(name + ': unexpected binary; review provenance before allowing')
            continue
        for label, pattern in PATTERNS.items():
            if pattern.search(text):
                # Never echo a matched credential, path or source line.
                errors.append(name + ': ' + label)
    if (root / '.git').exists():
        result = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True, check=True)
        allowed = {p.relative_to(root).as_posix() for p in files}
        for item in result.stdout.decode().split('\0'):
            if item and item not in allowed:
                errors.append(item + ': tracked file outside publication allowlist')
    if public:
        readiness = json.loads((root / 'release-readiness.json').read_text(encoding='utf-8'))
        for key in ('copyright_confirmed', 'license_confirmed', 'third_party_review_complete'):
            if readiness.get(key) is not True:
                errors.append('publication blocked: ' + key)
        if not (root / 'LICENSE').is_file() or (root / 'LICENSE_PENDING.md').exists():
            errors.append('publication blocked: finalize LICENSE and remove LICENSE_PENDING.md')
    return files, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public', action='store_true')
    args = parser.parse_args()
    try:
        files, errors = check(public=args.public)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Source check failed:', type(error).__name__, file=sys.stderr)
        return 1
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print('PASS:', len(files), 'allowlisted text source files; no matching secret/binary patterns.')
    print('Pattern scanning is not a complete credential or license audit.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
