"""Regression checks for isolated variants, secret exclusion and failed builds."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


harmony = module('harmony', 'harmony.py')
scanner = module('check_release', 'check-release.py')
from publication_files import source_files


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        for path in source_files(ROOT):
            target = self.root / path.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
        self.override = patch.object(harmony, 'ROOT', self.root)
        self.override.start()

    def tearDown(self):
        self.override.stop()
        self.directory.cleanup()

    def test_editions_are_isolated_and_keep_signatures(self):
        source = (self.root / 'AppScope/app.json5').read_bytes()
        basic = harmony.prepare('basic', 'auto')
        private = json.dumps({'app': {'signingConfigs': [{'name': 'local-test-only'}]}}).encode()
        (basic / 'build-profile.json5').write_bytes(private)
        particles = harmony.prepare('particles', 'auto')
        harmony.prepare('basic', 'off')
        self.assertEqual((basic / 'build-profile.json5').read_bytes(), private)
        self.assertNotIn(b'local-test-only', (particles / 'build-profile.json5').read_bytes())
        self.assertEqual((self.root / 'AppScope/app.json5').read_bytes(), source)
        self.assertEqual(json.loads((particles / 'AppScope/app.json5').read_text())['app']['bundleName'], harmony.EDITIONS['particles'][1])

    def test_one_lock_for_all_editions(self):
        with harmony.build_lock():
            with self.assertRaises(RuntimeError):
                with harmony.build_lock():
                    pass
        self.assertFalse((self.root / 'build/.harmony.lock').exists())

    def test_failed_unsigned_build_restores_private_config(self):
        stage = harmony.prepare('basic', 'auto')
        profile = stage / 'build-profile.json5'
        private = json.dumps({'app': {'signingConfigs': [{'name': 'local-test-only'}]}}).encode()
        profile.write_bytes(private)
        home = self.root / 'toolchain'
        ohpm = home / 'tools/ohpm/bin/ohpm'
        ohpm.parent.mkdir(parents=True); ohpm.touch()
        stale = stage / 'entry/build/default/outputs/default/entry-default-unsigned.hap'
        stale.parent.mkdir(parents=True); stale.write_bytes(b'stale')
        with patch.object(harmony, 'tool_environment', return_value=(Path('missing-hvigor'), {})):
            with patch.object(harmony.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'test')):
                with self.assertRaises(subprocess.CalledProcessError):
                    harmony.build(stage, 'basic', 'auto', str(home), False)
        self.assertEqual(profile.read_bytes(), private)
        self.assertFalse(stale.exists())
        self.assertFalse((self.root / 'dist').exists())

    def test_archive_inventory_excludes_local_material(self):
        harmony.prepare('accelerated', 'auto')
        private = self.root / '.local-signing/local.p12'
        private.parent.mkdir(); private.write_bytes(b'private')
        files, errors = scanner.check(self.root)
        self.assertEqual(errors, [])
        self.assertTrue(all(not p.is_relative_to(self.root / 'build') for p in files))
        self.assertNotIn(private, files)

    def test_secret_in_allowed_source_is_rejected(self):
        path = self.root / 'docs/accidental-config.json'
        path.write_text(json.dumps({'storePassword': 'fake-test-value'}))
        _, errors = scanner.check(self.root)
        self.assertTrue(any('signing secret' in e for e in errors))
        self.assertTrue(all('fake-test-value' not in e for e in errors))

    def test_missing_public_authorization_fails_closed(self):
        (self.root / 'release-readiness.json').write_text('{}')
        _, errors = scanner.check(self.root, public=True)
        self.assertTrue(any('publication blocked' in e for e in errors))

    def test_symlinks_are_rejected(self):
        (self.root / 'docs/outside.txt').symlink_to(self.root.parent / 'outside.txt')
        with self.assertRaises(ValueError):
            source_files(self.root)


if __name__ == '__main__':
    unittest.main()
