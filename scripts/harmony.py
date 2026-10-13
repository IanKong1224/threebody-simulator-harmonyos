#!/usr/bin/env python3
"""Prepare isolated DevEco projects and build unsigned/signed HAPs explicitly."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EDITIONS = {
    'basic': ('三体模拟器', 'com.threebody.simulator.basic'),
    'particles': ('三体模拟器Pro', 'com.threebody.simulator.particles'),
    'accelerated': ('三体模拟器Pro Max', 'com.threebody.simulator.accelerated'),
}
SOURCE_DIRS = ('AppScope', 'entry/src', 'core', 'renderer', 'platform', 'hvigor')
SOURCE_FILES = ('hvigorfile.ts', 'oh-package.json5', 'entry/hvigorfile.ts',
                'entry/oh-package.json5', 'entry/build-profile.json5')


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


@contextlib.contextmanager
def build_lock():
    lock = ROOT / 'build' / '.harmony.lock'
    lock.parent.mkdir(exist_ok=True)
    try:
        lock.mkdir()
    except FileExistsError:
        raise RuntimeError('Another prepare/build is active. If a process was killed, verify it has stopped before removing build/.harmony.lock.')
    try:
        yield
    finally:
        lock.rmdir()


def prepare(edition, vendor_kits):
    stage = ROOT / 'build' / 'harmony' / edition
    stage.mkdir(parents=True, exist_ok=True)
    for directory in SOURCE_DIRS:
        shutil.copytree(ROOT / directory, stage / directory, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('__pycache__', '.DS_Store'))
    for name in SOURCE_FILES:
        dest = stage / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, dest)
    # Each edition keeps its own IDE-generated private signing configuration.
    profile = stage / 'build-profile.json5'
    if not profile.exists():
        shutil.copyfile(ROOT / 'build-profile.example.json5', profile)
    title, bundle = EDITIONS[edition]
    app_path = stage / 'AppScope/app.json5'
    app = json.loads(app_path.read_text(encoding='utf-8'))
    app['app'].update(bundleName=bundle, vendor='ThreeBodySimulator', versionCode=1000100, versionName='1.0.0')
    write_json(app_path, app)
    for name in ('AppScope/resources/base/element/string.json', 'entry/src/main/resources/base/element/string.json'):
        obj = json.loads((stage / name).read_text(encoding='utf-8'))
        for item in obj['string']:
            if item['name'] in ('app_name', 'EntryAbility_label'):
                item['value'] = title
        write_json(stage / name, obj)
    module_path = stage / 'entry/build-profile.json5'
    module = json.loads(module_path.read_text(encoding='utf-8'))
    flags = dict(BASIC=edition == 'basic', PARTICLES=edition == 'particles',
                 DETAILED=edition == 'accelerated', ACCELERATED=edition == 'accelerated', PLANET=False)
    native = module['buildOption']['externalNativeOptions']
    native['arguments'] = ' '.join('-DTHREEBODY_' + key + '=' + ('ON' if value else 'OFF') for key, value in flags.items())
    native['arguments'] += ' -DTHREEBODY_VENDOR_KITS=' + vendor_kits.upper()
    write_json(module_path, module)
    return stage


def tool_environment(deveco_home):
    home = Path(deveco_home).expanduser()
    if not home.is_dir():
        raise RuntimeError('Set --deveco-home / DEVECO_HOME to the DevEco installation directory (macOS: app Contents).')
    windows = os.name == 'nt'
    runner = home / 'tools/hvigor/bin' / ('hvigorw.bat' if windows else 'hvigorw')
    node = home / 'tools/node' / ('node.exe' if windows else 'bin/node')
    if not runner.is_file() or not node.is_file():
        raise RuntimeError('DevEco tools not found under the selected installation directory.')
    env = dict(os.environ)
    env['PATH'] = str(node.parent) + os.pathsep + env.get('PATH', '')
    env.setdefault('DEVECO_SDK_HOME', str(home / 'sdk'))
    java = home / ('jbr/Contents/Home' if sys.platform == 'darwin' else 'jbr')
    env.setdefault('JAVA_HOME', str(java))
    env.setdefault('HVIGOR_USER_HOME', str(ROOT / 'build/.hvigor-user'))
    return runner, env


def build(stage, edition, vendor_kits, deveco_home, signed):
    runner, env = tool_environment(deveco_home)
    profile = stage / 'build-profile.json5'
    private_profile = profile.read_bytes()
    outputs = stage / 'entry/build/default/outputs/default'
    try:
        if not signed:
            # Do not parse or log an IDE signing config, which may be JSON5 or private.
            shutil.copyfile(ROOT / 'build-profile.example.json5', profile)
        # A stale HAP must never be reported as this build's output.
        for path in outputs.glob('*.hap'):
            path.unlink()
        ohpm = Path(deveco_home).expanduser() / 'tools/ohpm/bin' / ('ohpm.bat' if os.name == 'nt' else 'ohpm')
        if not ohpm.is_file():
            raise RuntimeError('DevEco ohpm tool not found. Install the complete development tools.')
        # Resolve the local libentry.so declaration package in a fresh checkout.
        subprocess.run([str(ohpm), 'install'], cwd=stage, env=env, check=True)
        command = [str(runner), '--mode', 'module', '-p', 'product=default', '-p', 'module=entry@default',
                   '-p', 'buildMode=release', 'assembleHap', '--no-daemon']
        subprocess.run(command, cwd=stage, env=env, check=True)
    finally:
        if not signed:
            profile.write_bytes(private_profile)
    kind = 'signed' if signed else 'unsigned'
    hap = outputs / ('entry-default-' + kind + '.hap')
    if not hap.is_file():
        raise RuntimeError('No fresh ' + kind + ' HAP was produced. For --signed, configure this edition in DevEco and authorize the connected device first.')
    variant = edition + ('-native-only' if edition == 'accelerated' and vendor_kits == 'off' else '')
    dest = ROOT / 'dist' / ('threebody-' + variant + '-release-' + kind + '.hap')
    dest.parent.mkdir(exist_ok=True)
    shutil.copyfile(hap, dest)
    print('HAP:', dest)
    if not signed:
        print('Unsigned HAP: sign for your own device before installing.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'build'))
    parser.add_argument('edition', choices=EDITIONS)
    parser.add_argument('--vendor-kits', choices=('auto', 'on', 'off'), default='auto')
    parser.add_argument('--signed', action='store_true', help='Use this edition\'s local IDE signing config; never infer signing from another edition')
    parser.add_argument('--deveco-home', default=os.environ.get('DEVECO_HOME', '/Applications/DevEco-Studio.app/Contents' if sys.platform == 'darwin' else ''))
    args = parser.parse_args()
    try:
        with build_lock():
            stage = prepare(args.edition, args.vendor_kits)
            print('DevEco project:', stage, flush=True)
            print('Bundle:', EDITIONS[args.edition][1], flush=True)
            if args.action == 'build':
                build(stage, args.edition, args.vendor_kits, args.deveco_home, args.signed)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        # Signing fields and passwords are deliberately not included in diagnostics.
        print('Build failed:', error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
