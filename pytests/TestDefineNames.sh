#!/usr/bin/env bash
# Defined-name parsing and lifecycle regression. GPLv2; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d /tmp/opengrads-define-names.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
python3 - "$launcher" "$build_root" "$test_root" <<'PY'
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

launcher,build,directory=sys.argv[1:]
root=Path(directory)
values=[1000*t+100*z+10*y+x for t in range(2) for z in range(2) for y in range(3) for x in range(4)]
(root/'data.dat').write_bytes(struct.pack('<48f',*values))
ctl=root/'data.ctl'
ctl.write_text('dset ^data.dat\noptions little_endian\nundef -9999\n'+
               'xdef 4 linear 10 0.5\nydef 3 linear 20 0.5\nzdef 2 levels 50 150\n'+
               'tdef 2 linear 00z01jan2000 1hr\nvars 1\nqv_raw 2 99 Fixture\nendvars\n')
env=dict(os.environ,OPENGRADS_BUILD_ROOT=build,OPENGRADS_COLOR='0')
prefix='open '+str(ctl)+'\nset gxout print\nset prnopts %.17g 16 1\n'
def run(commands,errors=False):
    p=subprocess.run([launcher,'-bl','-d','gxdummy','-h','gxdummy'],input=prefix+commands+'\nquit\n',
                     universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=60)
    if p.returncode or (not errors and re.search(r'Error|ERROR|Unknown command|Warning:.*name not found',p.stdout)):
        raise AssertionError(p.stdout)
    return p.stdout

def grids(text):
    lines=text.splitlines();result=[]
    for i,line in enumerate(lines):
        m=re.match(r'Printing Grid -- (\d+) Values',line)
        if not m:continue
        n=int(m.group(1));values=[]
        for row in lines[i+1:]:
            if len(values)>=n:break
            values.extend(float(x) for x in row.split())
        assert len(values)==n,text
        result.append(values)
    return result

out=run('define qv_sfc = qv_raw\nd qv_sfc\nu_sfc = qv_sfc+2\nd u_sfc\n'+
        'qv_sfc = qv_sfc+1\nd qv_sfc\nq define qv_sfc\n'+
        'set defval qv_sfc 2 1 77\nq defval qv_sfc 2 1\nd qv_sfc\n'+
        'define a__1_ = qv_sfc+3\nd a__1_\nundefine u_sfc\nundefine a__1_\nundefine qv_sfc\nq define\n')
base=values[:12];modified=[v+1 for v in base];modified[1]=77
assert grids(out)==[base,[v+2 for v in base],[v+1 for v in base],modified,[v+3 for v in modified]],out
for name in ('u_sfc','a__1_','qv_sfc'):assert name+' UNDEFINEd and storage released' in out,out
assert 'Gridded Defined Variable: qv_sfc' in out,out
# Existing names and their underscore suffixes must remain distinct.
name='a1234567_1234567'
assert len(name)==16
out=run('define qv=10\nqv_1=20\ndefine '+name+'=30\n'+name+'='+name+'+1\n'+
        'd qv\nd qv_1\nd '+name+'\nset x 1\nset y 1\n'+
        'define qs_sat=qvsat(280,100000)\nd qs_sat\nd qvsat(280,100000)\n')
a=grids(out)
assert all(v==10 for v in a[0]) and all(v==20 for v in a[1]) and all(v==31 for v in a[2]),out
assert a[3]==a[4],out
# The X clear-canvas command must not intercept explicit/implied definitions
# named x, expressions using them, or the independent SET X dimension option.
out=run('x=qv_raw+1\nd x\ndefine x=qv_raw*2\nd x\n'+
        'X = qv_raw+3\nd x\nset x 2 3\nset y 1\nd x\nundefine x\n')
assert grids(out)==[[v+1 for v in base],[v*2 for v in base],[v+3 for v in base],[4.,5.]],out
# Volumes retain underscores through dimension selection and vertical functions.
out=run('set z 1 2\nset t 1 2\ndefine qv_cube=qv_raw\nset z 2\nset t 2\nd qv_cube\n'+
        'set t 1\nset z 1\nd zslice(qv_cube,2)\nd zint(qv_cube)\n')
assert grids(out)==[values[36:48],values[12:24],[100*(a+b) for a,b in zip(values[:12],values[12:24])]],out
# Invalid names must fail without overwriting an existing prefix variable.
out=run('define guard=99\ndefine _bad=1\ndefine 1bad=1\ndefine guard-bad=1\n'+
        'define guard.bad=1\ndefine '+'a'+'_'*18+'=1\ndefine missing_=\n'+
        'd guard\nq define\n',errors=True)
assert out.count('must start with a letter')==2,out
assert out.count("characters other than letters, digits, or '_'")==3,out
assert 'DEFINE error: Missing expression' in out,out
assert len(grids(out))==1 and all(v==99 for v in grids(out)[0]),out
print('Defined-name regression passed: explicit/implied assignment, underscores, replacement, query/edit/undefine, volumes, vertical functions, saturation, distinct prefixes, and invalid names.')
PY
