#!/usr/bin/env python3
"""Exercise history-prefix arrows through real terminals (stdlib only)."""
import os
from pathlib import Path
import pty
import re
import select
import signal
import sys
import time

launcher = str(Path(sys.argv[1]).resolve())
work = Path(sys.argv[2]).resolve() / 'history'
work.mkdir()
UP, DOWN = '\x1b[A', '\x1b[B'
SS3_UP, SS3_DOWN = '\x1bOA', '\x1bOB'
BOL, EOL, RIGHT, KILL = '\x18\x01', '\x18\x05', '\x18\x06', '\x18\x0b'
seed = ['open a.ctl', 'set x 1 8', 'open b.ctl', 'd qv', 'open b.ctl',
        'Open upper.ctl', 'open  double.ctl', 'open c.ctl', 'q threads']
checks = 0


class Terminal:
    def __init__(self, name, history=seed, inputrc='', term='dumb', enabled=True,
                 personal=False, display='gxdummy'):
        self.home = work / name
        self.home.mkdir()
        self.seed = list(history)
        self.enabled = enabled
        self.log = self.home / '.grads.log'
        self.log.write_text(''.join(line+'\n' for line in history))
        config = self.home / ('.inputrc' if personal else 'inputrc')
        self.bol, self.eol, self.kill = BOL, EOL, KILL
        # Private harness keys work in both editing modes without assuming
        # Emacs Ctrl-A/E/F/K bindings in vi insertion mode.
        if inputrc is None:
            self.bol, self.eol, self.kill = '\x01', '\x05', '\x0b'
        else:
            config.write_text(inputrc+'\n"\\C-x\\C-a": beginning-of-line\n'
                              '"\\C-x\\C-e": end-of-line\n'
                              '"\\C-x\\C-f": forward-char\n'
                              '"\\C-x\\C-k": kill-line\n')
        self.done = False
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(self.home)
            env = dict(os.environ, HOME=str(self.home), TERM=term,
                       OPENGRADS_COLOR='0', OPENGRADS_UPDATE_CHECK='0',
                       OPENGRADS_HISTORY='1' if enabled else '0', GA_PROGRESS='off')
            if inputrc is None or personal:
                env.pop('INPUTRC', None)
            else:
                env['INPUTRC'] = str(config)
            if display=='Term':
                env.update(GA_TERM_MODE='file', GA_TERM_DIR=str(self.home/'plots'))
                env.pop('TMUX', None)
                env.pop('TMUX_PANE', None)
            os.execve(launcher, [launcher, '-bl', '-d', display, '-h', 'gxdummy'], env)
        self.prompt()

    def pump(self, seconds):
        result = b''
        deadline = time.monotonic()+seconds
        while time.monotonic()<deadline:
            ready, _, _ = select.select([self.fd], [], [], min(0.03, max(0,deadline-time.monotonic())))
            if not ready:
                continue
            try:
                block = os.read(self.fd, 65536)
            except OSError:
                break
            if not block:
                break
            result += block
        return result.decode('utf-8', 'replace')

    def prompt(self):
        result = ''
        deadline = time.monotonic()+15
        while 'ga-> ' not in result and time.monotonic()<deadline:
            result += self.pump(0.05)
        assert 'ga-> ' in result, result
        return result

    def keys(self, text):
        os.write(self.fd, text.encode())
        result = self.pump(0.06)
        assert 'Open Error' not in result and 'Scanning description' not in result, (self.home.name,result)
        return result

    def probe(self, expected, cursor=None):
        global checks
        # Quote only controlled fixture text. Printing it executes a harmless
        # shell probe, rather than executing the recalled GrADS command.
        assert "'" not in expected and '\n' not in expected
        if cursor is not None:
            self.keys('@')
            expected = expected[:cursor]+'@'+expected[cursor:]
        os.write(self.fd, (self.bol+"! printf 'HISTORY_BEGIN<%s>HISTORY_END\\n' '"+self.eol+"'\r").encode())
        result = self.prompt().replace('\r','')
        result = re.sub(r'\x1b\[[0-9;?]*[A-Za-z@]', '', result)
        matches = re.findall(r'^HISTORY_BEGIN<(.*)>HISTORY_END$', result, re.MULTILINE)
        assert matches == [expected], (self.home.name, expected, result)
        checks += 1

    def cancel(self):
        os.write(self.fd, b'\x03')
        self.prompt()  # Cancellation must show a fresh prompt without a second key.

    def close(self):
        try:
            os.write(self.fd, (self.bol+self.kill+'quit\r').encode())
            deadline = time.monotonic()+10
            while time.monotonic()<deadline:
                self.pump(0.05)
                child, status = os.waitpid(self.pid, os.WNOHANG)
                if child:
                    self.done = True
                    assert os.WIFEXITED(status) and os.WEXITSTATUS(status)==0, status
                    break
            assert self.done, 'GrADS did not exit'
            lines = self.log.read_text().splitlines()
            assert lines[:len(self.seed)] == self.seed, 'recall changed saved history'
            if not self.enabled:
                assert lines == self.seed, 'history=0 wrote history'
        finally:
            os.close(self.fd)
            if not self.done:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)


def case(name, keys, expected, cursor=None, **options):
    terminal = Terminal(name, **options)
    try:
        terminal.keys(keys)
        terminal.probe(expected, cursor)
    finally:
        terminal.close()


for mode, config in [('emacs',''), ('vi','set editing-mode vi\n')]:
    cases = [
        ('newest', 'open '+UP, 'open c.ctl', 5),
        ('older', 'open '+UP*2, 'open  double.ctl', 5),
        ('all-matches', 'open '+UP*4, 'open a.ctl', 5),
        ('oldest-boundary', 'open '+UP*9, 'open a.ctl', 5),
        ('down', 'open '+UP*3+DOWN, 'open  double.ctl', 5),
        ('restore', 'open '+UP*4+DOWN*4, 'open ', 5),
        ('repeat-restore', 'open '+UP+DOWN*4, 'open ', 5),
        ('restart', 'open '+UP+DOWN+UP, 'open c.ctl', 5),
        ('mixed-sequences', 'open '+UP+SS3_UP+SS3_DOWN, 'open c.ctl', 5),
        ('case-sensitive', 'Open '+UP, 'Open upper.ctl', 5),
        ('spaces', 'open  '+UP, 'open  double.ctl', 6),
        ('no-match', 'missing '+UP*2+DOWN, 'missing ', 8),
        ('anchored', 'pen '+UP, 'pen ', 4),
        ('set-prefix', 'set x '+UP, 'set x 1 8', 6),
        ('expression-prefix', 'd q'+UP, 'd qv', 3),
        ('empty', UP, 'q threads', len('q threads')),
        ('empty-up-down', UP*2+DOWN, 'q threads', len('q threads')),
        ('empty-restore', UP+DOWN, '', 0),
        ('ctrl-p', 'open '+'\x10', 'q threads', len('q threads')),
        ('ctrl-n', 'open '+'\x10\x0e', 'open ', len('open ')),
        ('edit-prefix', 'open '+UP+KILL+'b'+UP, 'open b.ctl', 6),
        ('cursor-prefix', 'open KEEP'+BOL+RIGHT*5+UP, 'open c.ctl', 5),
        ('restore-suffix', 'open KEEP'+BOL+RIGHT*5+UP+DOWN, 'open KEEP', 5),
        ('no-match-suffix', 'missing KEEP'+BOL+RIGHT*8+UP, 'missing KEEP', 8),
        ('ctrl-c', 'open '+UP+'\x03'+UP, 'q threads', len('q threads')),
        ('ctrl-c-restore-empty', 'open '+UP+'\x03'+UP+DOWN, '', 0),
        ('ctrl-c-empty-prefix', 'open KEEP'+BOL+UP+'\x03'+UP+DOWN, '', 0),
        ('ctrl-c-new-prefix', 'open '+UP+'\x03set x '+UP, 'set x 1 8', 6),
    ]
    for name, keys, expected, cursor in cases:
        if mode=='vi' and name in ('ctrl-p','ctrl-n'):
            continue  # vi insertion uses these keys for menu completion.
        # Deliver Ctrl-C separately so the terminal driver cannot discard
        # the following keys while flushing input for the signal.
        if '\x03' in keys:
            terminal = Terminal(mode+'-'+name, inputrc=config)
            try:
                before, after = keys.split('\x03')
                terminal.keys(before)
                terminal.cancel()
                terminal.keys(after)
                terminal.probe(expected, cursor)
            finally:
                terminal.close()
        else:
            case(mode+'-'+name, keys, expected, cursor, inputrc=config)

case('ss3', 'open '+SS3_UP*2+SS3_DOWN, 'open c.ctl', 5)
case('xterm', 'open '+UP*2, 'open  double.ctl', 5, term='xterm-256color')
case('unicode', 'open '+UP, 'open 資料.ctl', 5, history=['open 資料.ctl','q threads'])
case('no-history', 'open '+UP+DOWN, 'open ', 5, history=[])
case('disabled', 'open '+UP, 'open ', 5, enabled=False)
case('count', 'open '+'\x1b3'+UP, 'open b.ctl', 5)
case('negative-count', 'open '+UP*3+'\x1b-'+UP, 'open  double.ctl', 5)
for key, name in [('\x10','p'), ('\x0e','n')]:
    case('vi-menu-'+name, 'open '+key, 'open inputrc ', len('open inputrc '),
         inputrc='set editing-mode vi\n')
case('vi-command-mode', 'open draft'+'\x1b0i', 'open draft', 0,
     inputrc='set editing-mode vi\n')

# An explicitly requested ordinary binding must survive even though it is
# identical to Readline's original default. Test both common arrow encodings.
for mode, config in [('emacs',''), ('vi','set editing-mode vi\n')]:
    config += ('"\\e[A": previous-history\n"\\e[B": next-history\n'
               '"\\eOA": previous-history\n"\\eOB": next-history\n')
    case(mode+'-custom-up', 'open '+UP, 'q threads', len('q threads'), inputrc=config)
    case(mode+'-custom-ss3', 'open '+SS3_UP+SS3_DOWN, 'open ', 5, inputrc=config)
case('custom-function', 'open draft'+UP, 'open draft', 0,
     inputrc='"\\e[A": beginning-of-line\n')
case('custom-macro', 'open '+UP, 'open INSERTED', len('open INSERTED'),
     inputrc='"\\e[A": "INSERTED"\n')
case('named', 'open '+'\x18\x10'*2+'\x18\x0e', 'open c.ctl', 5,
     inputrc='$if opengrads\n"\\C-x\\C-p": opengrads-history-backward\n'
             '"\\C-x\\C-n": opengrads-history-forward\n$endif\n')

# No INPUTRC and no ~/.inputrc: exercise the real system fallback, rather
# than an empty test init file that hides system arrow bindings.
for name, keys, expected in [
        ('csi', 'open '+UP, 'open c.ctl'),
        ('ss3', 'open '+SS3_UP, 'open c.ctl'),
        ('restore', 'open '+UP+DOWN, 'open ')]:
    case('system-'+name, keys, expected, 5, inputrc=None, term='tmux-256color')
case('personal-override', 'open '+UP, 'q threads', len('q threads'), personal=True,
     inputrc='"\\e[A": previous-history\n')
case('personal-options', 'open '+UP, 'open c.ctl', 5, personal=True,
     inputrc='set bell-style none\n')
plugins = Path(os.environ.get('OPENGRADS_BUILD_ROOT','build'))/'src'/'.libs'
if any((plugins/('libgxdTerm'+suffix)).exists() for suffix in ('.so','.dylib')):
    case('system-term', 'open '+UP, 'open c.ctl', 5, inputrc=None,
         term='tmux-256color', display='Term')

# A completed command creates a fresh readline call; prefix state must not
# cross that boundary. Also check the current session's unsaved commands.
terminal = Terminal('new-prompt', history=[])
try:
    os.write(terminal.fd, b'set stat off\r')
    terminal.prompt()
    os.write(terminal.fd, b'q threads\r')
    terminal.prompt()
    terminal.keys('set '+UP)
    terminal.probe('set stat off', 4)
    terminal.keys('q '+UP)
    terminal.probe('q threads', 2)
finally:
    terminal.close()

terminal = Terminal('ctrl-r')
try:
    terminal.keys('\x12open b')
    terminal.keys('\x06')
    terminal.probe('open b.ctl')
finally:
    terminal.close()

print(f'Prefix history checks passed: {checks} real-terminal cases; Emacs/vi, CSI/SS3, draft/cursor restoration, editing, Ctrl-C/P/N/R, custom/named bindings, saved/current/disabled history, and no execution during recall.')
