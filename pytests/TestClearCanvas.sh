#!/usr/bin/env bash
# Added in 2026 for Clear Canvas. GPLv2; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d /tmp/opengrads-cc-test.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
hardcopy=gxdummy
if find "$build_root/src/.libs" -maxdepth 1 \
    \( -name libgxpCairo.so -o -name libgxpCairo.dylib \) -print -quit | grep -q .; then
  hardcopy=Cairo
fi
python3 - "$repo_root" "$build_root" "$launcher" "$test_root" "$hardcopy" <<'PY'
import os
from pathlib import Path
import re
import subprocess
import sys

repo, build, launcher, temp, hardcopy = sys.argv[1:]
root = Path(temp)
env = dict(os.environ, OPENGRADS_BUILD_ROOT=build, OPENGRADS_COLOR='0')
common = 'set gxout shaded\nset grads off\nset timelab off\n'
old = 'set clevs 270 280 290 300\nset ccols 2 3 4 5 6\n'
new = 'set clevs 260 275 285 310\nset ccols 6 5 4 3 2\n'

def run(commands, errors=False):
    text = f'open {repo}/pytests/data/model.ctl\n' + common + commands + '\nquit\n'
    proc = subprocess.run([launcher, '-bl', '-d', 'gxdummy', '-h', hardcopy],
                          input=text, universal_newlines=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, env=env, timeout=60)
    if proc.returncode or (not errors and re.search(
            r'Error|ERROR|Unknown command|logic check|Invalid expression', proc.stdout)):
        raise AssertionError(proc.stdout)
    return proc.stdout

def shades(output):
    lines = output.splitlines()
    return [tuple(lines[i+1:i+1+int(line.split('=')[1])])
            for i, line in enumerate(lines) if line.startswith('Number of levels =')]

def snap(name):
    if hardcopy != 'Cairo':
        return ''
    return f'printim {root / (name + ".png")} x640 y480\n'

def equal(a, b):
    if hardcopy == 'Cairo':
        assert (root / (a + '.png')).read_bytes() == (root / (b + '.png')).read_bytes(), (a, b)

def reference(settings, expr, name):
    return run(settings + f'd {expr}\nq shades\n' + snap(name))

# Every drawn item disappears, while levels/colors, dimensions, plotting
# area, and palette survive. Repeated cc must also keep the remembered style.
decor = f'run {repo}/lib/scripts/cbarn.gs\ndraw title Clear Canvas fixture\n'
out = run('set parea 1 9 1 7\nset rgb 40 23 81 137\nset rbcols 40 2 3\n' + old +
          'd ts\nq shades\nq dims\n' + decor + snap('before') +
          'cc\nq gxinfo\nq shades\nq dims\n' + snap('blank') +
          'cc\nd ts\nq shades\n' + decor + snap('after') +
          'cc\nd ts+1\nq shades\n')
assert shades(out) == [('2 <= 270', '3 270 280', '4 280 290', '5 290 300', '6 300 >')] * 3, out
assert 'Last Graphic = Clear' in out and '\nNone\n' in out, out
dims = [line for line in out.splitlines() if re.match(r'^[XYZTE] is ', line)]
assert dims[:5] == dims[5:10] and len(dims) == 10, out
equal('before', 'after')
run('cc\n' + snap('blank-reference'))
equal('blank', 'blank-reference')

# Never freeze automatic levels, auto contour intervals, or auto line colors.
out = run('d ts\nq shades\ncc\nd ts*10\nq shades\n' + snap('auto'))
ref = reference('', 'ts*10', 'auto-reference')
assert shades(out)[0] != shades(out)[1] == shades(ref)[0], out
equal('auto', 'auto-reference')
for settings, name in [
    ('set cint 5\nset cmin 250\nset cmax 310\nset rbrange 240 320\n', 'interval'),
    (old + 'set black 280 290\n', 'black'),
]:
    out = run(settings + 'd ts\ncc\nd ts+1\nq shades\n' + snap(name))
    ref = reference(settings, 'ts+1', name + '-reference')
    assert shades(out) == shades(ref), out
    equal(name, name + '-reference')

# A later set wins, including disabling a flag with a value equal to the
# post-display default. Equal-length arrays must not be confused by undo.
for override, expected, name in [
    (new, new, 'override'),
    ('set clevs\nset ccols\n', '', 'disable'),
    ('set black off\n', old, 'black-off'),
    ('set cint 5\n', 'set cint 5\n', 'new-interval'),
]:
    setup = old + ('set black 280 290\n' if name == 'black-off' else '')
    out = run(setup + 'd ts\n' + override +
              'cc\nd ts\nq shades\n' + snap(name))
    ref = reference(expected, 'ts', name + '-reference')
    assert shades(out) == shades(ref), out
    equal(name, name + '-reference')

# Overrides work both before clearing and after a first cc re-arms the style.
for override, expected in [('set cint 5\n', 'set cint 5\n'), ('set clevs\n', '')]:
    out = run(old + 'd ts\ncc\n' + override + 'cc\nd ts\nq shades\n')
    assert shades(out) == shades(reference(expected, 'ts', 'pending-reference')), out

# Existing commands retain their original lifecycle, and discard a remembered
# canvas so a later cc cannot resurrect options from before a normal clear.
auto = shades(reference('', 'ts', 'unused'))
for command in ('c', 'clear norset', 'reset', 'reinit'):
    reopen = f'open {repo}/pytests/data/model.ctl\n' if command == 'reinit' else ''
    out = run(old + 'd ts\n' + command + '\n' + reopen + common +
              'cc\nd ts\nq shades\n')
    assert shades(out) == auto, (command, out)
out = run(old + 'd ts\nd ts\nq shades\n')
assert shades(out) == auto, out

# Long/short names share exactly the same behavior, including script calls.
for index, command in enumerate(('clear canvas', 'CC', 'Clear Canvas', 'c canvas')):
    out = run(old + 'd ts\n' + command + '\nd ts\nq shades\n' + snap('alias'))
    ref = reference(old, 'ts', 'alias-reference')
    assert shades(out) == shades(ref), (command, out)
    equal('alias', 'alias-reference')
script = root / 'canvas.gs'
script.write_text("'clear canvas'\n'd ts'\n")
out = run(old + 'd ts\nset undo 10\n' + f'run {script}\nq undo\nq shades\nundo\nq shades\n')
assert '2 steps can be undone (up to 10 kept)' in out and shades(out) == shades(ref) * 2, out
out = run(old + 'd ts\nset gxout stat\nd ts\nset gxout shaded\ncc\nd ts\nq shades\n')
assert shades(out) == shades(ref), out

# Undo restores the entire cleared frame (plot, labels, and color bar), as
# well as the style cache. A set after clearing must survive that undo.
out = run('set undo 10\n' + old + 'd ts\n' + decor + snap('undo-before') +
          'cc\nundo\nq shades\n' + snap('undo-after') +
          'cc\n' + new + 'undo\ncc\nd ts\nq shades\n' + snap('undo-new'))
assert 'Undid 1 step' in out and len(shades(out)) == 2, out
equal('undo-before', 'undo-after')
reference(new, 'ts', 'undo-new-reference')
equal('undo-new', 'undo-new-reference')
out = run('set undo 10\n' + old + 'd ts\ncc\n' + new +
          'd ts\nundo\ncc\nd ts\nq shades\n')
assert shades(out) == shades(reference(new, 'ts', 'unused-new')), out
out = run('set undo 10\n' + old + 'set black 280 290\nd ts\n' +
          'set clevs\nset ccols\nset black off\nundo\ncc\nd ts\nq shades\n')
assert shades(out) == auto, out

# A failed expression and an invalid cc must not replace/erase a good frame.
out = run(old + 'd ts\nd no_such_field\ncc\nd ts\nq shades\n', errors=True)
assert shades(out) == shades(reference(old, 'ts', 'unused-old')), out
out = run(old + 'd ts\ncc invalid\nq shades\n', errors=True)
assert 'Clear Canvas takes no arguments' in out and shades(out) == shades(reference(old, 'ts', 'unused-old')), out
out = run(old + 'd ts\nclear canvas invalid\nq shades\n', errors=True)
assert 'Clear Canvas takes no arguments' in out and shades(out) == shades(reference(old, 'ts', 'unused-old')), out

# Explicit line styling and vrange survive; automatically chosen line colors
# are recomputed for a new canvas instead of inheriting the overlay count.
line = 'set gxout line\nset x 1\nset y 10\nset t 1 5\n'
for settings, name in [('', 'line-auto'),
                       ('set ccolor 2\nset cstyle 3\nset cmark 4\nset vrange 250 350\n', 'line-explicit')]:
    run(line + settings + 'd ts\ncc\nd ts\n' + snap(name))
    run(line + settings + 'd ts\n' + snap(name + '-reference'))
    equal(name, name + '-reference')
run('set antialias off\n' + old + 'd ts\ncc\nd ts\n' + snap('antialias'))
run('set antialias off\n' + old + 'd ts\n' + snap('antialias-reference'))
equal('antialias', 'antialias-reference')

print('Clear Canvas checks passed: explicit/automatic styles, overrides, blank canvas, legacy clears, undo, axes, and antialiasing' +
      ('; rendered PNGs match.' if hardcopy == 'Cairo' else ' (Cairo image checks skipped).'))
PY
