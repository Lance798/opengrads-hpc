#!/usr/bin/env python3
"""Check built-in names against the prompt catalog without building GrADS."""
import argparse
from collections import Counter
from pathlib import Path
import re
import sys


def without_comments(text):
    # Preserve quoted strings/characters while ignoring disabled registrations
    # in C comments. The check deliberately reads source, not build artifacts.
    tokens = re.compile(r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|/\*.*?\*/|//[^\n]*''', re.DOTALL)
    return tokens.sub(lambda match: '' if match.group().startswith('/') else match.group(), text)


def check_function_catalog(repo):
    source = (repo / 'cola/src/gafunc.c').read_text(encoding='utf-8')
    help_source = (repo / 'cola/src/gahelp.c').read_text(encoding='utf-8')
    try:
        dispatch = source.split('/* start with the list of built-in functions */', 1)[1]
        dispatch = dispatch.split('#if OPENGRADS', 1)[0]
        catalog = help_source.split('static const struct gahelp funcs[] = {', 1)[1]
        catalog = catalog.split('\n};', 1)[0]
    except IndexError:
        raise ValueError('Cannot locate the built-in dispatcher or funcs[] catalog')

    builtins = set(re.findall(r'cmpwrd\s*\(\s*"([^"]+)"\s*,\s*name\s*\)', without_comments(dispatch)))
    names = re.findall(r'\{\s*"([^"]+)"', without_comments(catalog))
    if not builtins or not names:
        raise ValueError('No names parsed from the built-in dispatcher or funcs[] catalog')
    shown = set(names)
    errors = []
    if builtins - shown:
        errors.append('Missing Tab/usage entries: ' + ', '.join(sorted(builtins - shown)))
    if shown - builtins:
        errors.append('Tab/usage names absent from evaluator: ' + ', '.join(sorted(shown - builtins)))
    duplicates = sorted(name for name, count in Counter(names).items() if count > 1)
    if duplicates:
        errors.append('Duplicate Tab/usage entries: ' + ', '.join(duplicates))
    if errors:
        errors.append('Update cola/src/gafunc.c:rtnprs and cola/src/gahelp.c:funcs[] together.')
        errors.append('See docs/ADDING_FUNCTIONS.md for the new-function checklist.')
        raise ValueError('\n'.join(errors))
    return len(builtins)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path,
                        default=Path(__file__).resolve().parent.parent,
                        help='repository root (defaults to this checkout)')
    args = parser.parse_args()
    try:
        count = check_function_catalog(args.repo_root)
    except (OSError, ValueError) as error:
        print('Function catalog check failed:\n' + str(error), file=sys.stderr)
        return 1
    print('Function catalog passed: %d built-ins match Tab/usage names' % count)
    return 0


if __name__ == '__main__':
    sys.exit(main())
