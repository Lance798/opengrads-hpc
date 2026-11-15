#!/usr/bin/env python3
"""Verify interactive history and color defaults in isolated terminal sessions."""
import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import sys
import time

launcher = str(Path(sys.argv[1]).resolve())
work = Path(sys.argv[2]).resolve() / 'defaults'
work.mkdir()
args = ['-bl', '-d', 'gxdummy', '-h', 'gxdummy']


def environment(home, overrides):
    home.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for key in ('OPENGRADS_COLOR', 'OPENGRADS_HISTORY'):
        env.pop(key, None)
    env.update(HOME=str(home), TMPDIR=str(work), TERM='dumb', INPUTRC='/dev/null',
               GA_PROGRESS='off', OPENGRADS_UPDATE_CHECK='0')
    env.update(overrides)
    return env


def terminal(home, commands=(), extra=(), overrides=None, eof=False,
             immediate=False, expected=0, recall=False):
    env = environment(home, overrides or {})
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(home)
        os.execve(launcher, [launcher] + args + list(extra), env)

    def pump(seconds):
        result = b''
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([fd], [], [], min(0.05, max(0, end - time.monotonic())))[0]:
                try:
                    block = os.read(fd, 65536)
                except OSError:
                    break
                if not block:
                    break
                result += block
        return result.decode('utf-8', 'replace')

    def prompt():
        result = ''
        marker = 'ga> ' if '-E' in extra or '-u' in extra else 'ga-> '
        end = time.monotonic() + 15
        while marker not in result and time.monotonic() < end:
            result += pump(0.1)
        assert marker in result, result
        return result

    done = False
    output = []
    try:
        if not immediate:
            output.append(prompt())
            if recall:
                os.write(fd, b'\x10\x10\x10')
                recalled = pump(0.15)
                assert 'q threads' in recalled, 'previous command not recalled: ' + recalled
                os.write(fd, b'\x01\x0b')
                pump(0.05)
            for command in commands:
                os.write(fd, (command + '\r').encode())
                output.append(prompt())
            os.write(fd, b'\x04' if eof else b'quit\r')
        end = time.monotonic() + 15
        while time.monotonic() < end:
            output.append(pump(0.1))
            exited, status = os.waitpid(pid, os.WNOHANG)
            if exited:
                done = True
                code = os.WEXITSTATUS(status) if os.WIFEXITED(status) else -os.WTERMSIG(status)
                assert code == expected, (code, output)
                break
        assert done, 'GrADS did not exit: ' + ''.join(output)
        return ''.join(output)
    finally:
        os.close(fd)
        if not done:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)


home = work / 'interactive'
first = terminal(home, ['q threads', '! echo COLOR=$OPENGRADS_COLOR'])
assert '\x1b[' in first and 'COLOR=1' in first, first
log = home / '.grads.log'
assert 'q threads' in log.read_text(), 'default session did not save history'
second = terminal(home, ['history'], recall=True)
assert re.search(r'\[\d+\] q threads', second), second

home = work / 'eof'
terminal(home, ['q threads'], eof=True)
assert 'q threads' in (home / '.grads.log').read_text(), 'EOF lost the final command'

home = work / 'disabled'
home.mkdir()
seed = 'q threads\nquit # (End of session: previous)\n'
(home / '.grads.log').write_text(seed)
disabled = terminal(home, ['history', '! echo COLOR=$OPENGRADS_COLOR'],
                    overrides={'OPENGRADS_HISTORY': '0', 'OPENGRADS_COLOR': '0'})
assert '\x1b[' not in disabled and 'COLOR=0' in disabled, disabled
assert 'q threads' not in disabled, 'history=0 loaded the previous session'
assert (home / '.grads.log').read_text() == seed, 'history=0 changed the log'

home = work / 'custom'
custom = home / 'chosen.log'
terminal(home, ['q threads'], extra=['-H', str(custom)],
         overrides={'OPENGRADS_HISTORY': '0'})
assert 'q threads' in custom.read_text() and not (home / '.grads.log').exists()

home = work / 'pipe'
env = environment(home, {})
result = subprocess.run([launcher] + args, input=b'q threads\nquit\n',
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
assert result.returncode == 0, result.stdout
assert not (home / '.grads.log').exists(), 'pipe input enabled history by default'

home = work / 'one-shot'
terminal(home, extra=['-x', '-c', 'q threads'], immediate=True, expected=99)
assert not (home / '.grads.log').exists(), 'one-shot execution enabled history'

for flag, name in (('-E', 'no-editing'), ('-u', 'ipc')):
    home = work / name
    terminal(home, ['q threads'], extra=[flag], overrides={'OPENGRADS_COLOR': '0'})
    assert not (home / '.grads.log').exists(), flag + ' enabled history automatically'

home = work / 'empty-home'
terminal(home, ['q threads'], overrides={'HOME': ''})
assert not (home / '.grads.log').exists(), 'empty HOME enabled history'

home = work / 'startup-quit'
home.mkdir()
(home / '.grads.log').write_text(seed)
terminal(home, extra=['-c', 'quit'], immediate=True)
assert 'q threads' in (home / '.grads.log').read_text(), 'startup quit deleted old history'

print('Prompt defaults passed: color=1, cross-session history/recall, EOF, explicit -H, opt-outs, pipes, one-shot, IPC and startup quit')
