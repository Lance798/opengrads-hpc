#!/usr/bin/env python3
"""Exercise GrADS completion through a real terminal (Python stdlib only)."""
import os
from pathlib import Path
import pty
import re
import select
import signal
import sys
import time


launcher = str(Path(sys.argv[1]).resolve())
work = Path(sys.argv[2]).resolve() / 'completion'
work.mkdir()
for directory in ('scripts1', 'scripts2', 'bin', 'localdir', 'clear'):
    (work / directory).mkdir()
for name in ('localplot.gs', 'clear.gs', 'localdata.txt', '.hidden.gs',
             'localdir/nested.gs', 'scripts1/remoteplot.gs',
             'scripts1/dupcase.gs', 'scripts2/dupcase.gs',
             'scripts1/dupcase_other.gs', 'option=value.gs', 'UpperPlot.gs',
             'clear_data.txt'):
    (work / name).write_text("say 'COMPLETION_SCRIPT_OK'\n")
tool = work / 'bin' / 'tabprobe-tool'
tool.write_text('#!/bin/sh\nprintf "COMPLETION_TOOL_OK\\n"\n')
tool.chmod(0o755)
not_executable = work / 'bin' / 'tabprobe-disabled'
not_executable.write_text('#!/bin/sh\nexit 0\n')
not_executable.chmod(0o644)
(work / 'bin' / 'tabprobe-directory').mkdir()
(work / 'linkdir').symlink_to('localdir', target_is_directory=True)

pid, fd = pty.fork()
if pid == 0:
    os.chdir(work)
    os.environ.update(HOME=str(work), TERM='dumb', INPUTRC='/dev/null',
                      OPENGRADS_COLOR='0', OPENGRADS_UPDATE_CHECK='0',
                      GA_PROGRESS='off', GA_COMPLETION_CACHE='1',
                      GASCRP='scripts1:scripts2:scripts1',
                      PATH=str(work / 'bin') + ':' + os.environ['PATH'])
    os.execv(launcher, [launcher, '-bl', '-d', 'gxdummy', '-h', 'gxdummy'])


def pump(seconds, terminal=None):
    if terminal is None:
        terminal = fd
    result = b''
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ready, _, _ = select.select([terminal], [], [], min(0.05, max(0, end - time.monotonic())))
        if ready:
            try:
                block = os.read(terminal, 65536)
            except OSError:
                break
            if not block:
                break
            result += block
    return result.decode('utf-8', 'replace')


def prompt():
    result = ''
    end = time.monotonic() + 15
    while 'ga-> ' not in result and time.monotonic() < end:
        result += pump(0.1)
    assert 'ga-> ' in result, 'GrADS did not return to the prompt: ' + result
    return result


def clean(text):
    return re.sub(r'\x1b\[[0-9;?]*[A-Za-z]|\x07|\r|\x08', '', text)


count = 0


def complete(keys, expected=(), absent=()):
    global count
    os.write(fd, keys.encode())
    raw = [pump(0.15)]
    for _ in range(3):
        os.write(fd, b'\t')
        raw.append(pump(0.15))
    os.write(fd, b'\x01\x0b')
    pump(0.05)
    assert '\x1b[' not in ''.join(raw), 'color=0 emitted ANSI escapes'
    parts = [clean(part) for part in raw]
    result = ''.join(parts)
    for word in expected:
        assert word in result, repr(keys) + ' missing ' + repr(word) + ': ' + result
    for word in absent:
        assert word not in result, repr(keys) + ' unexpectedly offered ' + repr(word) + ': ' + result
    count += 1
    return parts


def ordered(parts, labels):
    listings = [part for part in parts if all(label + ' (' in part for label in labels)]
    assert listings, 'missing group headings: ' + repr(parts)
    for part in listings:
        positions = [part.index(label + ' (') for label in labels]
        assert positions == sorted(positions), 'incorrect group order: ' + part


def reap(child, terminal):
    os.close(terminal)
    end = time.monotonic() + 5
    while time.monotonic() < end:
        done, _ = os.waitpid(child, os.WNOHANG)
        if done:
            return
        time.sleep(0.05)
    os.kill(child, signal.SIGKILL)
    os.waitpid(child, 0)


try:
    prompt()
    parts = complete('', ['localplot.gs', 'localdata.txt', 'clear.gs', 'localdir/'],
                     ['remoteplot.gs', '.hidden.gs', 'localdir//', 'Commands ('])
    ordered(parts, ['Scripts', 'Directories', 'Files'])
    complete('localp', ['localplot.gs'])
    complete('UpperP', ['UpperPlot.gs'])
    complete('upperp', absent=['UpperPlot.gs'])
    complete('locald', ['localdir/', 'localdata.txt'])
    complete('remotep', ['remoteplot.gs'])
    complete('clear', ['clear.gs'])
    parts = complete('clea', ['clear.gs'])
    ordered(parts, ['Commands', 'Scripts', 'Directories', 'Files'])
    assert 'clear/' in ''.join(parts) and 'clear//' not in ''.join(parts), parts
    assert 'clear_data.txt' in ''.join(parts), 'grouping unexpectedly filtered file candidates'
    assert any('clear' in part.split() and 'clear.gs' in part.split() for part in parts), parts
    complete('./localp', ['./localplot.gs'])
    complete('../completion/localp', ['../completion/localplot.gs'])
    complete('~/localp', ['localplot.gs'])
    complete('localdir/nes', ['localdir/nested.gs'])
    complete('linkdir/nes', ['linkdir/nested.gs'])
    complete('link', ['linkdir/'], ['linkdir//'])
    complete('.', ['.hidden.gs'])
    complete('run local', ['localplot.gs', 'localdir/'], ['localdata.txt'])
    complete('run remotep', ['remoteplot.gs'])
    complete('run option=', ['option=value.gs'])
    complete('run localplot.gs localda', ['localdata.txt'])
    complete('custom.gs localda', ['localdata.txt'])
    complete('custom.gs option=', ['option=value.gs'])
    complete('x = mea', ['mean('], ['localplot.gs'])
    complete('clear = mea', ['mean('])
    complete('set = mea', ['mean('])
    complete('new_var=mea', ['mean('])
    complete('! ls localda', ['localdata.txt'])
    complete('!ls localda', ['localdata.txt'])
    complete('!ls --color=auto localda', ['localdata.txt'])
    complete('! ls localdir/', ['localdir/nested.gs'])
    complete('!tabprobe-t', ['tabprobe-tool'])
    complete('! tabprobe-', ['tabprobe-tool'], ['tabprobe-disabled', 'tabprobe-directory'])
    complete('!echo test|tabprobe-t', ['tabprobe-tool'])
    complete('!echo test >localda', ['localdata.txt'])
    complete('set local', absent=['localplot.gs'])
    complete('d local', absent=['localplot.gs'])
    complete('nosuchprefix', absent=['localplot.gs'])

    ctl = Path(__file__).resolve().parent / 'data/model.ctl'
    os.write(fd, ('open ' + str(ctl) + '\r').encode())
    prompt()
    os.write(fd, b'define tmean = ave(ts,t=1,t=3)\r')
    prompt()
    ordered(complete('d t', ['ts', 'tmean', 'tloop(']), ['Variables', 'Functions'])
    ordered(complete('d ave(t', ['ts', 'tmean', 'tloop(', 't=']),
            ['Variables', 'Functions', 'Dimensions'])

    parts = complete('dupc', ['dupcase.gs', 'dupcase_other.gs'])
    for part in parts:
        listed = ' '.join(line for line in part.splitlines() if not line.startswith('ga-> '))
        assert listed.split().count('dupcase.gs') <= 1, 'duplicate GASCRP candidate: ' + part

    # Changes to existing directory contents and executable permissions become
    # visible after the metadata refresh interval, within the same session.
    complete('fresh', absent=['fresh_after_tab.gs'])
    fresh = work / 'fresh_after_tab.gs'
    fresh.write_text("say 'fresh'\n")
    not_executable.chmod(0o755)
    (work / 'scripts1' / 'remote_after_tab.gs').write_text("say 'remote'\n")
    time.sleep(2.1)
    complete('fresh', ['fresh_after_tab.gs'])
    complete('remote_after', ['remote_after_tab.gs'])
    complete('!tabprobe-d', ['tabprobe-disabled'], ['tabprobe-directory'])
    fresh.unlink()
    not_executable.chmod(0o644)
    (work / 'scripts1' / 'remote_after_tab.gs').unlink()
    time.sleep(2.1)
    complete('fresh', absent=['fresh_after_tab.gs'])
    complete('remote_after', absent=['remote_after_tab.gs'])
    complete('!tabprobe-d', absent=['tabprobe-disabled', 'tabprobe-directory'])

    # A sizeable directory: repeated prefix searches, then an external edit.
    large = work / 'large'
    large.mkdir()
    for i in range(3000):
        (large / ('plot%04d.gs' % i)).touch()
    os.write(fd, b'large/plot\t\t')
    question = ''
    deadline = time.monotonic() + 10
    while 'possibilities by type?' not in question and time.monotonic() < deadline:
        question += pump(0.1)
    assert 'Display all 3000 possibilities by type?' in question, question
    os.write(fd, b'n')
    declined = clean(prompt())
    assert 'large/plot' in declined and 'Scripts (' not in declined, declined
    os.write(fd, b'\x01\x0b')
    pump(0.1)
    complete('large/plot299', ['plot2990.gs', 'plot2999.gs'])
    complete('large/plot299', ['plot2990.gs', 'plot2999.gs'])
    complete('large/plot000', ['plot0000.gs', 'plot0009.gs'])
    (large / 'plot_new.gs').touch()
    time.sleep(2.1)
    complete('large/plot_n', ['plot_new.gs'])

    # Complete in the middle of a line, then actually execute it. This checks
    # that the suffix survives and that completion did not execute anything.
    os.write(fd, b'localp arg\x01' + b'\x06' * 6 + b'\t\r')
    out = prompt()
    assert 'COMPLETION_SCRIPT_OK' in out, out
    os.write(fd, b'!tabprobe-t\t\r')
    out = prompt()
    assert 'COMPLETION_TOOL_OK' in out, out
    os.write(fd, b'history\r')
    out = prompt()
    assert 'localplot.gs arg' in out, 'completion damaged the argument suffix: ' + out
    assert 'tabprobe-tool' in out, out
    os.write(fd, b'clear\r')
    out = prompt()
    assert 'COMPLETION_SCRIPT_OK' not in out, 'built-in clear ran clear.gs: ' + out
    os.write(fd, b'clear.gs\r')
    out = prompt()
    assert 'COMPLETION_SCRIPT_OK' in out, 'explicit clear.gs did not run the script: ' + out
    os.write(fd, b'quit\r')
    pump(0.3)
    print('Extended Tab checks passed: %d cases, ordered groups, collisions, refresh, 3000 files, execution and cursor suffix' % count)
finally:
    reap(pid, fd)

# Color is checked in a second real terminal. Category labels stay out of
# inserted command text, and color=0 was verified throughout the first one.
colorpid, colorfd = pty.fork()
if colorpid == 0:
    os.chdir(work)
    os.environ.update(HOME=str(work), TERM='dumb', INPUTRC='/dev/null',
                      OPENGRADS_COLOR='1', OPENGRADS_UPDATE_CHECK='0', GA_PROGRESS='off')
    os.execv(launcher, [launcher, '-bl', '-d', 'gxdummy', '-h', 'gxdummy'])
try:
    startup = ''
    deadline = time.monotonic() + 15
    while 'ga-> ' not in startup and time.monotonic() < deadline:
        startup += pump(0.1, colorfd)
    assert 'ga-> ' in startup, startup
    os.write(colorfd, b'clea\t\t\t')
    out = pump(0.6, colorfd)
    assert '\x1b[32mCommands' in out and '\x1b[35mScripts' in out, out
    assert '\x01' not in out and '\x02' not in out, 'prompt width markers leaked into output'
    os.write(colorfd, b'\x01\x0bl\t\t\t')
    out = pump(0.6, colorfd)
    assert '\x1b[34mDirectories' in out, out
    os.write(colorfd, b'\x01\x0bquit\r')
    pump(0.3, colorfd)
    print('Completion color checks passed: distinct category colors and color=0')
finally:
    reap(colorpid, colorfd)
