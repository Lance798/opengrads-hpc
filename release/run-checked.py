#!/usr/bin/env python3
"""Bound packaging commands and retain output, including a macOS hang sample.

Progress goes to stderr even when a caller captures stdout. Child output goes
to a regular file first, so inherited pipe descriptors cannot delay completion.
This is a build helper; it is not needed by the distributed application.
"""
import argparse
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


def report(message):
    print(f'[package] {message}', file=sys.stderr, flush=True)


def diagnose(process, prefix):
    """Collect bounded diagnostics before terminating an unresponsive command."""
    try:
        result = subprocess.run(
            ['/bin/ps', '-axo', 'pid,ppid,pgid,state,etime,comm'],
            capture_output=True, timeout=5)
        rows = result.stdout.decode(errors='replace').splitlines()
        members = [row for row in rows[1:]
                   if len(row.split()) >= 3 and row.split()[2] == str(process.pid)]
        prefix.with_suffix('.processes.txt').write_text('\n'.join(rows[:1] + members) + '\n')
        report('Process group:\n' + '\n'.join(members))
        # sample is provided by macOS. Sample the executable and, for shell
        # regressions, a few of its children, without an unbounded diagnostic.
        if sys.platform == 'darwin':
            for row in members[:4]:
                pid = row.split()[0]
                path = prefix.with_suffix(f'.sample-{pid}.txt')
                subprocess.run(['/usr/bin/sample', pid, '1', '-file', str(path)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=6)
                if path.exists():
                    report(f'Hang sample: {path}\n' + path.read_text(errors='replace')[-12000:])
    except (OSError, subprocess.TimeoutExpired) as error:
        report(f'Diagnostic collection: {error}')


def stop(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    # The leader may exit while a child ignores TERM. Kill the entire group.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', required=True)
    parser.add_argument('--log-dir', required=True, type=Path)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--allow-failure', action='store_true',
                        help='accept a normal nonzero exit, but never a timeout')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('a command and a positive finite timeout are required')
    args.log_dir.mkdir(parents=True, exist_ok=True)
    prefix = args.log_dir / re.sub(r'[^a-zA-Z0-9_-]', '_', args.label)
    log = prefix.with_suffix('.log')
    report(f'{args.label}: start (limit {args.timeout:g}s); log: {log}')
    started = time.monotonic()
    process = None
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    previous_handler = signal.signal(signal.SIGTERM, interrupted)
    try:
        with log.open('wb') as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            while True:
                remaining = args.timeout - (time.monotonic() - started)
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, args.timeout)
                try:
                    code = process.wait(timeout=min(15, remaining))
                    break
                except subprocess.TimeoutExpired:
                    report(f'{args.label}: running, {time.monotonic() - started:.1f}s')
    except subprocess.TimeoutExpired:
        report(f'{args.label}: TIMEOUT after {args.timeout:g}s')
        diagnose(process, prefix)
        stop(process)
        code = 124
    except KeyboardInterrupt:
        if process is not None:
            stop(process)
        code = 130
    except OSError as error:
        report(f'{args.label}: {error}')
        if process is not None:
            stop(process)
        code = 127
    finally:
        signal.signal(signal.SIGTERM, previous_handler)
    report(f'{args.label}: exit {code}, {time.monotonic() - started:.1f}s')
    # Show a short escaped tail on errors even if stdout is being captured by
    # Bash. The full log (including image escape sequences) remains on disk.
    if code:
        with log.open('rb') as output:
            output.seek(max(0, log.stat().st_size - 4000))
            report('Output tail: ' + repr(output.read().decode(errors='replace')))
    with log.open('rb') as output:
        while chunk := output.read(65536):
            sys.stdout.buffer.write(chunk)
    allowed = args.allow_failure and code >= 0 and code not in (124, 127, 130)
    return 0 if allowed else (code if code >= 0 else 128 - code)


if __name__ == '__main__':
    sys.exit(main())
