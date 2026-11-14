#!/usr/bin/env bash
# Dimension metadata, endpoint expressions and animation regression. GPLv2.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-dimensions.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
python3 - "$launcher" "$build_root" "$test_root" <<'PY'
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

launcher, build, directory = sys.argv[1:]
root = Path(directory)
axes = 'xyzte'
shape = (8, 5, 4, 20, 3)
def fixture(name, dims, data=True):
    nx, ny, nz, nt, ne = dims
    values = [100000*e+1000*t+100*z+10*y+x
              for e in range(ne) for t in range(nt) for z in range(nz)
              for y in range(ny) for x in range(nx)]
    if data:
        (root/(name+'.dat')).write_bytes(struct.pack('<%df' % len(values), *values))
    else:
        (root/(name+'.dat')).touch()  # OPEN needs a file, but no readable field exists.
    ctl = root/(name+'.ctl')
    ctl.write_text('dset ^'+name+'.dat\noptions little_endian\nundef -9999\n'+
                   f'xdef {nx} linear 120 0.5\nydef {ny} linear -20 2\n'+
                   f'zdef {nz} levels '+('1000 850 500 200' if nz==4 else '1000')+'\n'+
                   f'tdef {nt} linear 00z01jan2000 1hr\n'+
                   (f'edef {ne} names control warm cold\n' if ne>1 else '')+
                   f'vars 1\nqv {nz} 99 Fixture\nendvars\n')
    return ctl

ctl = fixture('main', shape)
other_shape = (6, 3, 1, 7, 1)
other = fixture('other', other_shape)
odd = fixture('odd', (7, 5, 1, 7, 1))
metadata = fixture('metadata', shape, data=False)
env = dict(os.environ, OPENGRADS_BUILD_ROOT=build, OPENGRADS_COLOR='0',
           OPENGRADS_UPDATE_CHECK='0', GA_PROGRESS='off')
point = ''.join(f'set {axis} 1\n' for axis in axes)
printing = 'set gxout print\nset prnopts %.17g 1000 1\n'
prefix = f'open {ctl}\nopen {other}\nset dfile 1\n'+point+printing
def run(commands, errors=False, initial=prefix):
    p = subprocess.run([launcher, '-bl', '-d', 'gxdummy', '-h', 'gxdummy'],
                       input=initial+commands+'\nquit\n', text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=60)
    if p.returncode or (not errors and re.search(r'Error|ERROR|error:|Unknown command', p.stdout)):
        raise AssertionError(p.stdout)
    return p.stdout

def grids(text):
    lines = text.splitlines()
    result = []
    for i, line in enumerate(lines):
        m = re.match(r'Printing Grid -- (\d+) Values', line)
        if not m:
            continue
        n, values = int(m.group(1)), []
        for row in lines[i+1:]:
            if len(values)>=n:
                break
            values.extend(float(x) for x in row.split())
        assert len(values)==n, text
        result.append(values)
    return result

checks = 0
def same(symbolic, numeric, field='qv'):
    global checks
    left = grids(run(symbolic+'\nd '+field))
    right = grids(run(numeric+'\nd '+field))
    assert left==right and left, (symbolic, left, right)
    checks += 1

for axis, size in zip(axes, shape):
    for name in ('n'+axis, 'last'):
        same(f'set {axis} 1 {name}', f'set {axis} 1 {size}')
        same(f'set {axis} {name}', f'set {axis} {size}')
        same(f'set {axis} 2 {name}-1', f'set {axis} 2 {size-1}')
for expr, expected in [('nx/2',4), ('(nx+2)*2/4',5), ('nx-2*2',4),
                       ('-(2-4)+nx/2',6), ('+last-1',7), ('2+3*1',5),
                       ('nx*1/3',3), ('nx*0.5',4), ('nx*.5',4),
                       ('.5*nx',4), ('nx*5e-1',4), ('nx*5e-01',4),
                       ('nx/3*3',8), ('(nx/3)*3',8), ('nx/3+0.5',3),
                       ('7/2',4), ('nx*0.5625',5), ('1+0.5',2)]:
    same('set x '+expr, 'set x '+str(expected))
same('set x nx/3 nx*0.8', 'set x 3 6')
same('set x nx*0.5 nx/2', 'set x 4')
for axis, size in zip(axes, shape):
    same(f'set {axis} n{axis}*0.5', f'set {axis} {(size+1)//2}')
    same(f'set {axis} 1 last/3', f'set {axis} 1 {int(size/3+0.5)}')
# Halfway values follow the existing numeric single-index convention,
# including negative/out-of-domain indices; never round intermediate values.
same('set x -(3/2)', 'set x -1', 'lon')
same('set x 1+0.49 4+0.49', 'set x 1 4', 'lon')
same('set x 1+0.5 4+0.5', 'set x 2 5', 'lon')
same(f'open {odd}\nset dfile 3\nset x nx*0.5',
     f'open {odd}\nset dfile 3\nset x 4')
same(f'open {odd}\nset dfile 3\nset x nx*1/3',
     f'open {odd}\nset dfile 3\nset x 2')
same('set dfile 2\nset x 1 nx', 'set dfile 2\nset x 1 6')
same('set dfile 2\nset t 1 nt', 'set dfile 2\nset t 1 7')

for axis, coords, field in [('lon',(120,123.5),'lon'), ('lat',(-20,-12),'lat'),
                            ('lev',(1000,200),'lev')]:
    same(f'set {axis} first last', f'set {axis} {coords[0]} {coords[1]}', field)
    same(f'set {axis} last', f'set {axis} {coords[1]}', field)
    same(f'set {axis} first', f'set {axis} {coords[0]}', field)
    same(f'set {axis} {coords[0]} last', f'set {axis} {coords[0]} {coords[1]}', field)
same('set z nz-1', 'set lev 500')
# Singletons, ordinary fractions, scientific notation and cyclic/outside bounds.
same('set dfile 2\nset z last', 'set dfile 2\nset z 1')
same('set x 1.5 4.5', 'set x 1.5e0 4.5e0', 'lon')
same('set lon 120.25 122.25', 'set lon 1.2025e2 1.2225e2', 'lon')
same('set x -1 nx+2', 'set x -1 10', 'lon')

# Rejections are atomic, including an invalid second endpoint.
bad = ['set x 2 ny', 'set x 2 nx/0', 'set x 2 nx+1junk',
       'set x 2 nx*nan', 'set x 2 nx*inf', 'set x 2 nx*1e309',
       'set x 2 nx*0.5junk', 'set x 2 nx*.', 'set x 2 nx/0.0',
       'set x 2 nx/1e-100',
       'set x 2 nx+2147483647', 'set x 2 2147483647*2',
       'set x 2 (-2147483647-1)-1', 'set x 2 -(-2147483647-1)',
       'set x 2 (nx-1', 'set x 2 nx-', 'set x 2 nx extra',
       'set x 2 '+'('*70+'nx'+')'*70,
       'set lon 120 last-1', 'set lat first ny', 'set lev 850 last+1',
       'set e 1 ne extra', 'set t 1 nt extra']
for command in bad:
    out = run(command+'\nd qv', errors=True)
    assert 'SET error' in out or 'SET Error' in out, out
    assert grids(out)==[[0.]], out
    checks += 1
out = run('set x 1 last', errors=True, initial='')
assert 'No files open' in out, out

out = run(''.join(f'd dimsize({axis},{file})\n' for file in (1,2) for axis in axes)+
          'set x 2 3\nd dimsize(x)\nset dfile 2\nd dimsize(t)\n'+
          'd dimsize(x,1)*dimsize(y,2)\nset dfile 1\n'+point+
          'define nx=99\ndefine x=77\nd nx\nd dimsize(x,1)\n'+
          f'open {metadata}\nd dimsize(t,3)\n')
assert grids(out)==[[float(v)] for v in shape+other_shape+(8,7,24,99,8,20)], out
for expr in ('dimsize()', 'dimsize(x,1,2)', 'dimsize(lon)', 'dimsize(xx)',
             'dimsize(x,0)', 'dimsize(x,-1)', 'dimsize(x,1.0)',
             'dimsize(x,1+1)', 'dimsize(x,3)', 'dimsize(x,99999999999999999999)',
             'dimsize(x,)', 'dimsize(,1)'):
    out = run('define guard=42\ndefine guard='+expr+'\nd guard', errors=True)
    assert 'DIMSIZE error' in out and grids(out)==[[42.]], out
    checks += 1

script = root/'sizes.gs'
script.write_text("'q dims'\nsavedrc=rc\nsavedresult=result\n"+
                  "nx=dimsize('x',1)\nny=dimsize('y',2)\n"+
                  "axis='t'\nnt=dimsize(axis)\n"+
                  "say 'SIZES 'nx' 'ny' 'nt' 'nx*ny\n"+
                  "if (rc != savedrc)\nsay 'SCRIPT_RC_CHANGED'\nendif\n"+
                  "if (result != savedresult)\nsay 'SCRIPT_RESULT_CHANGED'\nendif\n"+
                  "'set x 1 'nx\n'd dimsize(x,1)'\nsay result\n")
out = run('run '+str(script))
assert 'SIZES 8 3 20 24' in out and 'SCRIPT_RC_CHANGED' not in out and 'SCRIPT_RESULT_CHANGED' not in out, out
assert grids(out)==[[8.]], out
for expr in ("dimsize()", "dimsize('x',1,2)", "dimsize('bad')", "dimsize('x',3)"):
    script.write_text('n='+expr+"\nsay 'UNEXPECTED_SUCCESS'\n")
    out = run('run '+str(script), errors=True)
    assert 'DIMSIZE error' in out and 'UNEXPECTED_SUCCESS' not in out, out
    checks += 1
script.write_text("n=dimsize('x',1)\nsay n\n")
out = run('run '+str(script), errors=True, initial='')
assert 'DIMSIZE error' in out, out

# Print actual frames: each fixture value identifies its E/T/Z/Y/X indices.
for axis, size, factor in zip(axes, shape, (1,10,100,1000,100000)):
    out = run(f'set {axis} 1 last\nset loopdim {axis}\nset looping on\nset loopincr 3\nd qv')
    assert grids(out)==[[float(i*factor)] for i in range(0,size,3)], out
    checks += 1
anim = 'set t 1 last\nset looping on\n'
for clear, stride in [('cc',3), ('x',3), ('clear canvas',3), ('clear norset',3),
                      ('swap',3), ('c',1), ('clear',1)]:
    out = run(anim+'set loopincr 3\nd qv\n'+clear+'\n'+printing+'d qv')
    expected = [[float(t*1000)] for t in range(0,20,3)]+[[float(t*1000)] for t in range(0,20,stride)]
    assert grids(out)==expected, out
    checks += 1
for reset in ('reset','reinit'):
    out = run('set loopincr 3\n'+reset+'\n'+(f'open {ctl}\n' if reset=='reinit' else '')+
              point+printing+anim+'d qv')
    assert grids(out)==[[float(t*1000)] for t in range(20)], out
    checks += 1
for value in ('0','-1','1.5','1e2','bad','2147483648','2 extra',''):
    out = run(anim+'set loopincr 3\nset loopincr '+value+'\nd qv', errors=True)
    assert 'SET error' in out and grids(out)==[[float(t*1000)] for t in range(0,20,3)], out
    checks += 1
for stride, times in [(6,range(0,20,6)), (99,[0]), (2147483647,[0]), (1,range(20))]:
    out = run(anim+f'set loopincr {stride}\nd qv')
    assert grids(out)==[[float(t*1000)] for t in times], out
    checks += 1
# Automatic 3-D animation preserves each full XY plane.
out = run('set x 1 nx\nset y 1 ny\nset t 1 nt\nset loopincr 6\nd qv')
assert grids(out)==[[float(t*1000+y*10+x) for y in range(5) for x in range(8)] for t in range(0,20,6)], out
# The step does not subsample DEFINE, AVE or a non-looping time-series DISPLAY.
out = run('set loopincr 6\nset t 1 nt\ndefine series=qv\nd series\n'+
          'd qv\nd qv/dimsize(z,1)\nset t 1\nd ave(qv,t=1,t=20)\n'+
          'd ave(qv/dimsize(z),t=1,t=20)')
assert grids(out)==[[float(t*1000) for t in range(20)]]*2+[
    [float(t*250) for t in range(20)], [9500.], [2375.]], out
print(f'Dimension regression passed: {checks} comparisons/rejections, all five axes, physical endpoints, scalar/script metadata, animation sampling/clear/reset, and unchanged DEFINE/AVE/time-series results.')
PY
