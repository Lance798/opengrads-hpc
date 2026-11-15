#!/usr/bin/env python3
"""Exercise packaging timeouts with real children and captured shell output."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parent.parent / 'release/run-checked.py'


class PackageRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='opengrads-package-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_child(self, source, *, options=(), data=b'', timeout='5'):
        return subprocess.run(
            [sys.executable, str(RUNNER), '--label', 'fixture', '--log-dir', str(self.root),
             '--timeout', timeout, *options, '--', sys.executable, '-c', source],
            input=data, capture_output=True, timeout=45)

    def test_stdin_binary_output_and_visible_progress(self):
        data = b'commands\n\x1b]1337;image\x00\xff'
        result = self.run_child(
            'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read()); '
            'sys.stdout.flush(); sys.stderr.write("stderr\\n")', data=data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, data + b'stderr\n')
        self.assertEqual((self.root/'fixture.log').read_bytes(), result.stdout)
        self.assertIn(b'fixture: start', result.stderr)
        self.assertIn(b'fixture: exit 0', result.stderr)

    def test_nonzero_is_reported_when_stdout_is_captured(self):
        result = self.run_child('print("load failed"); raise SystemExit(7)')
        self.assertEqual(result.returncode, 7)
        self.assertIn(b'load failed', result.stderr)

    def test_expected_nonzero(self):
        result = self.run_child('print("no X server"); raise SystemExit(1)',
                                options=('--allow-failure',))
        self.assertEqual(result.returncode, 0)
        self.assertIn(b'no X server', result.stdout)

    def test_timeout_is_never_allowed_and_kills_process_group(self):
        pidfile = self.root/'child.pid'
        child = 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(1000)'
        source = (
            'import subprocess,sys,signal,time; from pathlib import Path; '
            'signal.signal(signal.SIGTERM,signal.SIG_IGN); '
            f'p=subprocess.Popen([sys.executable,"-c",{child!r}]); '
            f'Path({str(pidfile)!r}).write_text(str(p.pid)); '
            'print("blocked",flush=True); time.sleep(1000)')
        result = self.run_child(source, timeout='2', options=('--allow-failure',))
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertIn(b'TIMEOUT', result.stderr)
        self.assertIn(b'blocked', result.stderr)
        self.assertTrue((self.root/'fixture.processes.txt').is_file())
        pid = int(pidfile.read_text())
        state = subprocess.run(['/bin/ps', '-o', 'state=', '-p', str(pid)],
                               capture_output=True, text=True, timeout=5).stdout.strip()
        self.assertTrue(not state or state.startswith('Z'), state)

    def test_signals_are_not_allowed_failures(self):
        result = self.run_child('import os,signal; os.kill(os.getpid(),signal.SIGTERM)',
                                options=('--allow-failure',))
        self.assertEqual(result.returncode, 143)

    def test_inherited_output_descriptor_does_not_block_runner(self):
        pidfile = self.root/'detached.pid'
        result = self.run_child(
            'import subprocess,sys; from pathlib import Path; '
            'p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(30)"]); '
            f'Path({str(pidfile)!r}).write_text(str(p.pid)); '
            'print("finished")')
        pid = int(pidfile.read_text())
        try:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b'finished', result.stdout)
            # It completed while a child still holds the inherited descriptor.
            os.kill(pid, 0)
        finally:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def test_invalid_timeout(self):
        for timeout in ('0', '-1', 'nan', 'inf'):
            with self.subTest(timeout=timeout):
                result = self.run_child('raise AssertionError("must not run")', timeout=timeout)
                self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
