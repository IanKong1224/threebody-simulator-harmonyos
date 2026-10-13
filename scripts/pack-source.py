#!/usr/bin/env python3
"""Create a deterministic, scanned source ZIP; never include build/signing history."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import sys
import zipfile
from publication_files import ROOT

spec = importlib.util.spec_from_file_location('release_check', ROOT / 'scripts/check-release.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/threebody-simulator-source-candidate.zip')
    args = parser.parse_args()
    files, errors = checker.check(public=args.public)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    output = args.output.resolve()
    if output.is_relative_to(ROOT) and output.relative_to(ROOT).parts[0] not in ('dist', 'build'):
        raise SystemExit('Choose an output outside source directories (for example dist/).')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            info = zipfile.ZipInfo('threebody-simulator-harmonyos/' + path.relative_to(ROOT).as_posix(), (2000, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(output.suffix + '.sha256').write_text(digest + '  ' + output.name + '\n')
    print('Scanned source archive:', output)
    print('SHA256:', digest)
    if not args.public:
        print('Local candidate only. Public release still requires --public checks and owner approval.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
