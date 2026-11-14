#!/usr/bin/env python3
"""Extract one version's GitHub release description from the changelog."""
import argparse
from pathlib import Path
import re
import sys


def extract_release_notes(markdown, version):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?', version):
        raise ValueError('Invalid release version: '+version)
    lines = markdown.splitlines(keepends=True)
    sections = []
    fence = None
    for index, line in enumerate(lines):
        marker = re.match(r'^ {0,3}(`{3,}|~{3,})', line)
        if fence:
            if marker and marker[1][0]==fence[0] and len(marker[1])>=len(fence) and not line[marker.end():].strip():
                fence = None
            continue
        if marker:
            fence = marker[1]
            continue
        heading = re.match(r'^ {0,3}##[ \t]+opengrads-hpc[ \t]+(\S+)[ \t]*$', line.rstrip('\r\n'))
        if heading:
            sections.append((heading[1], index))
    matches = [i for i, section in enumerate(sections) if section[0]==version]
    if len(matches)!=1:
        raise ValueError('Expected exactly one release section for '+version+', found '+str(len(matches)))
    selected = matches[0]
    start = sections[selected][1]
    end = sections[selected+1][1] if selected+1<len(sections) else len(lines)
    if not ''.join(lines[start+1:end]).strip():
        raise ValueError('Release section is empty: '+version)
    return ''.join(lines[start:end]).strip()+'\n'


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', help='distribution version (defaults to release/VERSION)')
    parser.add_argument('--notes-file', type=Path, default=root/'docs/RELEASE_NOTES.md')
    parser.add_argument('--output', type=Path, help='write to this file instead of stdout')
    args = parser.parse_args()
    try:
        version = args.version if args.version is not None else (root/'release/VERSION').read_text(encoding='utf-8').strip()
        notes = extract_release_notes(args.notes_file.read_text(encoding='utf-8'), version)
        if args.output:
            args.output.write_text(notes, encoding='utf-8')
        else:
            sys.stdout.write(notes)
    except (OSError, ValueError) as error:
        parser.exit(1, 'Release notes error: '+str(error)+'\n')
    return 0


if __name__=='__main__':
    sys.exit(main())
