#!/usr/bin/env bash
# Read-only VVMex HDF5 regression at 1 and 4 calculation cores.
# The descriptor must expose the RCEMIP th/pibar/rhobar/qv/u/v fields.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ctl="${1:?Usage: TestHDF5TimeWorkersReal.sh /path/to/vvm.ctl}"
python3 -u - "$repo_root" "$ctl" <<'PY'
import math,os,re,subprocess,sys,time
from pathlib import Path
repo=Path(sys.argv[1]);ctl=str(Path(sys.argv[2]).resolve())
launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
env=dict(os.environ,OPENGRADS_COLOR='0',OPENGRADS_UPDATE_CHECK='0',GA_TIME_TRACE='1',GA_PROGRESS='off')
zt='set x 5\nset y 3\nset z 1 6\nset t 1 32\n'
point='set x 1\nset y 1\nset z 1\nset t 1 32\n'
profiles='set x 1\nset y 1\nset z 1 45\nset t 1\ndefine rho_saved=rhobar\ndefine pi_saved=pibar\ndefine p_saved=100000*pow(pibar,1004/287)\nset z 1\ndefine gain=1.25\n'
masks='set x 1 64\nset y 1 64\nset t 1\nset z 1\ndefine land_saved=(lon>-14.7)-0.5\nset z 1 45\ndefine cube_saved=(qv>0.000001)-0.5\n'
areas='x=1,x=64,y=1,y=64'
commands=zt+'define nativewind=mag(u,v)\nd nativewind\nd mag(u,v)\n'+\
    'define nativesat=aave(qvsat(th*pibar,100000*pow(pibar,1004/287)),'+areas+')\nd nativesat\n'+\
    point+'define nativecwv=aave(zint(qv*rhobar),'+areas+')\nd nativecwv\n'+profiles+zt+\
    'define savedsat=qvsat(th*pi_saved,p_saved)*gain\nd savedsat\nd qvsat(th*pi_saved,p_saved)*gain\n'+\
    'define savedes=esat(th*pi_saved)\nd savedes\nd esat(th*pi_saved)\n'+\
    point+'define savedcwv=aave(zint(qv*rho_saved),'+areas+')\nd savedcwv\n'+masks+point+\
    'define maskedcwv=aave(zint(maskout(maskout(qv,land_saved),cube_saved)*rho_saved),'+areas+')\nd maskedcwv\n'+\
    'set x 1 16\nset y 1 12\nset z 2\nset t 1\n'+\
    '\n'.join('d '+fn+'(qvsat(th*pi_saved,p_saved)*gain,t=1,t=32)' for fn in ('ave','mean','sum','sumg','min','max','minloc','maxloc'))+'\n'

def grids(text):
    out=[];lines=iter(text.splitlines())
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n,missing=int(m[1]),float(m[2]);v=[]
        while len(v)<n:v.extend(map(float,next(lines).split()))
        assert len(v)==n
        out.append([None if x==missing else x for x in v])
    return out

def run(n):
    script=f'open {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n'
    start=time.monotonic()
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input=script,universal_newlines=True,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(env,GA_TIME_WORKERS='off' if n==1 else 'auto'),timeout=600)
    assert p.returncode==0 and not re.search(r'Error|ERROR|Time evaluation failed',p.stdout),p.stdout[-4000:]
    assert not any(int(x)>0 for x in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-4000:]
    groups=re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',p.stdout)
    if n==1:assert not groups
    else:
        assert len(groups)>=18,(n,len(groups))
        assert all(int(count)==n for count,_ in groups)
        assert 'Time defined inputs: count=3' in p.stdout
    for _,pids in groups:
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived quit',pid))
    return grids(p.stdout),time.monotonic()-start
reference,seconds=run(1)
print(f'HDF5 baseline: cores=1 seconds={seconds:.2f} grids={len(reference)}',flush=True)
for n in (4,):
    actual,seconds=run(n);assert len(actual)==len(reference)
    samples=0;max_abs=0.
    for a,b in zip(actual,reference):
        assert len(a)==len(b)
        for u,v in zip(a,b):
            assert (u is None)==(v is None),(n,u,v)
            if u is None:continue
            if not math.isfinite(u) or not math.isfinite(v):
                assert u==v or math.isnan(u) and math.isnan(v)
                continue
            error=abs(u-v);max_abs=max(max_abs,error);samples+=1
            assert error<=1e-8+2e-12*abs(v),(n,u,v)
    print(f'HDF5 real passed: cores={n} seconds={seconds:.2f} values={samples} max_abs={max_abs:.17g}; native + static scalar/profile/XY/XYZ; all 8 input-time reductions; masks/cleanup agree.',flush=True)
PY
