"""Allowlisted source inventory shared by scanner and archive creator."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORIES = ('AppScope', 'core', 'renderer', 'platform', 'reference', 'tests',
               'entry/src', 'hvigor', 'scripts', 'docs', 'prompts', '.github')
FILES = ('README.md', 'ARCHITECTURE.md', 'CONTRIBUTING.md', 'THIRD_PARTY_NOTICES.md',
         'LICENSE', 'NOTICE', 'LICENSE_PENDING.md', '.gitignore', '.gitattributes', '.clang-tidy',
         'CMakeLists.txt', 'hvigorfile.ts', 'oh-package.json5', 'entry/oh-package.json5',
         'entry/hvigorfile.ts', 'entry/build-profile.json5', 'build-profile.example.json5',
         'release-readiness.json')


def source_files(root=ROOT):
    paths = set(root / name for name in FILES if (root / name).is_file())
    for directory in DIRECTORIES:
        for path in (root / directory).rglob('*'):
            if '__pycache__' in path.parts or path.name == '.DS_Store' or path.suffix == '.pyc':
                continue
            if path.is_symlink():
                raise ValueError('Publication does not allow symlinks: ' + str(path.relative_to(root)))
            if path.is_file():
                paths.add(path)
    for path in sorted(paths):
        if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError('Source path escapes repository: ' + str(path))
    return sorted(paths)
