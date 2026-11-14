#!/usr/bin/env python3
"""Verify that GitHub release descriptions contain exactly one version."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT/'release/release_notes.py'
spec = importlib.util.spec_from_file_location('release_notes', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
extract = module.extract_release_notes


class ReleaseNotesTests(unittest.TestCase):
    def test_current_release_metadata(self):
        version = (ROOT/'release/VERSION').read_text().strip()
        markdown = (ROOT/'docs/RELEASE_NOTES.md').read_text(encoding='utf-8')
        self.assertTrue(markdown.startswith('## opengrads-hpc '+version+'\n'))
        notes = extract(markdown, version)
        self.assertEqual(notes.count('\n## opengrads-hpc '), 0)
        self.assertGreater(len(notes.splitlines()), 5)

    def test_only_requested_version(self):
        text = 'Intro\n## opengrads-hpc 1.0.16\nNew changes\n\n## opengrads-hpc 1.0.15\nOld changes\n'
        self.assertEqual(extract(text, '1.0.16'), '## opengrads-hpc 1.0.16\nNew changes\n')
        self.assertEqual(extract(text, '1.0.15'), '## opengrads-hpc 1.0.15\nOld changes\n')

    def test_subheadings_and_examples(self):
        text = ('## opengrads-hpc 1.0.16\n### Added\nDetails\n'
                '```text\n## opengrads-hpc 1.0.16\n```\n'
                '~~~\n## opengrads-hpc 1.0.10\n~~~\n'
                '### Fixed\nA fix\n## opengrads-hpc 1.0.15\nOld\n')
        result = extract(text, '1.0.16')
        self.assertIn('### Fixed\nA fix', result)
        self.assertNotIn('1.0.15', result)

    def test_exact_version_and_crlf(self):
        text = '## opengrads-hpc 1.0.10\r\nNew\r\n## opengrads-hpc 1.0.1\r\nOld\r\n'
        self.assertIn('Old', extract(text, '1.0.1'))
        self.assertNotIn('New', extract(text, '1.0.1'))

    def test_invalid_sections_fail(self):
        for text, version in [
            ('## opengrads-hpc 1.0.15\nOld\n', '1.0.16'),
            ('## opengrads-hpc 1.0.16\nA\n## opengrads-hpc 1.0.16\nB\n', '1.0.16'),
            ('## opengrads-hpc 1.0.16\n\n## opengrads-hpc 1.0.15\nOld\n', '1.0.16'),
            ('## opengrads-hpc 1.0.16\nNew\n', '1.0'),
            ('## opengrads-hpc 1.0.16-extra\nNew\n', '1.0.16')]:
            with self.subTest(text=text, version=version):
                with self.assertRaises(ValueError):
                    extract(text, version)

    def test_cli_default_from_another_directory(self):
        with tempfile.TemporaryDirectory(prefix='release-notes-') as directory:
            output = Path(directory)/'notes.md'
            process = subprocess.run([sys.executable, str(SCRIPT), '--output', str(output)],
                                     cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(process.returncode, 0, process.stderr)
            version = (ROOT/'release/VERSION').read_text().strip()
            expected = extract((ROOT/'docs/RELEASE_NOTES.md').read_text(encoding='utf-8'), version)
            self.assertEqual(output.read_text(encoding='utf-8'), expected)

    def test_cli_failure_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(prefix='release-notes-') as directory:
            output = Path(directory)/'notes.md'
            output.write_text('previous output')
            process = subprocess.run([sys.executable, str(SCRIPT), '--version', '999.0.0', '--output', str(output)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn(b'exactly one release section', process.stderr)
            self.assertEqual(output.read_text(), 'previous output')


if __name__=='__main__':
    unittest.main()
