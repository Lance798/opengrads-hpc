#!/usr/bin/env python3
"""Check startup argument forwarding, including macOS Bash 3.2 with color off."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
BASH = os.environ.get('OPENGRADS_TEST_BASH', 'bash')


class LauncherTests(unittest.TestCase):
    def check_launcher(self, kind):
        with tempfile.TemporaryDirectory(prefix='opengrads-launcher-') as directory:
            root = Path(directory)
            local = kind.startswith('local-')
            macos = kind == 'macos-archive'
            launcher = root/('release/launch-local.sh' if local else 'opengrads')
            launcher.parent.mkdir(parents=True, exist_ok=True)
            source = 'release/launch-local.sh' if local else 'release/opengrads-macos' if macos else 'opengrads'
            shutil.copy2(ROOT/source, launcher)
            (root/'etc').mkdir()
            (root/'etc/udpt-local').write_text('gxdummy gxdisplay libgxdummy.so\n')
            if kind.endswith('archive'):
                (root/'VERSION').write_text('opengrads-hpc 1.0.16\nGrADS base 2.2.1.oga.1\n')
            build = root/'build'
            plugins = build/'src/.libs'
            plugins.mkdir(parents=True)
            suffix = 'dylib' if kind == 'local-macos' else 'so'
            (plugins/('libgxdummy.'+suffix)).touch()
            binary = root/'bin/grads' if macos else build/'src/grads'
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_text('#!/usr/bin/env python3\nimport json, sys\nprint(json.dumps(sys.argv[1:]))\n')
            binary.chmod(0o755)
            env = dict(os.environ, HOME=str(root), OPENGRADS_BUILD_ROOT=str(build),
                       OPENGRADS_RELEASE_PLATFORM='Darwin' if kind == 'local-macos' else 'Linux',
                       OPENGRADS_UPDATE_CHECK='0', OPENGRADS_TERM='0', DISPLAY='', TERM='dumb')
            env.pop('OPENGRADS_BUNDLE_ROOT', None)
            supplied = ['-bl', '-d', 'gxdummy', '-h', 'gxdummy', '-c', 'say hello', '', '*']
            for color in (None, '0', '1', '2'):
                for args in ([], supplied):
                    with self.subTest(kind=kind, color=color, args=args):
                        settings = dict(env, OPENGRADS_COLOR_SCHEME='2')
                        settings.pop('OPENGRADS_COLOR', None)
                        if color is not None:
                            settings['OPENGRADS_COLOR'] = color
                        expected = list(args)
                        if not args and not local:
                            expected = ['-bl', '-d', 'gxdummy', '-h', 'Cairo' if macos else 'gxdummy']
                        if not macos and color != '0':
                            expected = ['-C', '2']+expected
                        result = subprocess.run([BASH, str(launcher), *args], env=settings,
                                                text=True, capture_output=True, timeout=20)
                        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        self.assertEqual(json.loads(result.stdout), expected)

    def test_source(self):
        self.check_launcher('source')

    def test_linux_archive(self):
        self.check_launcher('linux-archive')

    def test_macos_archive(self):
        self.check_launcher('macos-archive')

    def test_local_linux(self):
        self.check_launcher('local-linux')

    def test_local_macos(self):
        self.check_launcher('local-macos')


if __name__ == '__main__':
    print(subprocess.check_output([BASH, '--version'], text=True).splitlines()[0], flush=True)
    unittest.main()
