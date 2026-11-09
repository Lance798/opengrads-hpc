#!/usr/bin/env bash
# Read-only validation against the user's VVM thermodynamic/template dataset.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ctl="${1:?Usage: TestNetCDFTimeWorkersReal.sh /path/to/thermodynamic.ctl}"
python3 -u - "$repo_root" "$ctl" <<'PY'
import math,os,re,subprocess,sys,time
from pathlib import Path
repo,ctl=map(Path,sys.argv[1:]);ctl=ctl.resolve()
launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
env=dict(os.environ,OPENGRADS_COLOR='0',OPENGRADS_UPDATE_CHECK='0',GA_TIME_TRACE='1',GA_PROGRESS='off')
samples=0;max_abs=0

def grids(text):
    lines=iter(text.splitlines());out=[]
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n,missing=int(m[1]),float(m[2]);values=[]
        while len(values)<n:values.extend(map(float,next(lines).split()))
        assert len(values)==n
        out.append([None if v==missing else v for v in values])
    return out

def run(commands,workers=True):
    script=f'open {ctl}\nset threads {4 if workers else 1}\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n'
    start=time.monotonic()
    result=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input=script,
        text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        env=dict(env,GA_TIME_WORKERS='auto' if workers else 'off'),timeout=600)
    text=result.stdout
    assert result.returncode==0 and not re.search(r'Error|ERROR|Time evaluation failed',text),text[-4000:]
    assert not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',text)),text[-4000:]
    for group in re.findall(r'Time workers: .*?pids=([0-9,]+)',text):
        for pid in map(int,group.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived quit',pid))
    return grids(text),text,time.monotonic()-start

def compare(actual,reference):
    global samples,max_abs
    assert len(actual)==len(reference)
    for a,b in zip(actual,reference):
        assert len(a)==len(b)
        for x,y in zip(a,b):
            assert (x is None)==(y is None),(x,y)
            if x is None:continue
            if not math.isfinite(x) or not math.isfinite(y):
                assert x==y or math.isnan(x) and math.isnan(y),(x,y)
                continue
            error=abs(x-y);max_abs=max(max_abs,error);samples+=1
            assert error<=1e-8+2e-12*abs(y),(x,y)

# Exercise all 26 functions in the shared whole-expression allowlist.
expressions=['abs(-qv)','sqrt(abs(qv))','exp(qv)','log(1+abs(qv))',
    'log10(1+abs(qv))','pow(qv,2)','mag(qv,qc)','sin(qv)','cos(qv)',
    'tan(qv)','asin(sin(qv))','acos(cos(qv))','atan2(qv,th)',
    'maskout(qv,(qv>0)-0.5)','const(maskout(qv,-1),0)',
    'esat(th)','qvsat(th,100000)',
    'ave(qv,x=1,x=16)','mean(qv,x=1,x=16)',
    'sum(qv,x=1,x=16)','sumg(qv,x=1,x=16)',
    'aave(qv,x=1,x=16,y=1,y=12)','amean(qv,x=1,x=16,y=1,y=12)',
    'asum(qv,x=1,x=16,y=1,y=12)','atot(qv,x=1,x=16,y=1,y=12)']
setup='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
commands=setup+'\n'.join('define answer='+e+'\nd answer' for e in expressions)
reference,_,serial=run(commands,False);actual,text,parallel=run(commands)
assert text.count('Time workers:')==len(expressions),text[-3000:]
compare(actual,reference)
print(f'VVM all 25 point/spatial functions: {samples} values, max_abs={max_abs:.17g}; serial={serial:.2f}s workers={parallel:.2f}s',flush=True)

commands='set x 1\nset y 1\nset z 1\nset t 1 32\n'+\
    'define answer=aave(zint(qv),x=1,x=384,y=1,y=384)\nd answer\n'+\
    'd aave(zint(qv),x=1,x=384,y=1,y=384)\n'
reference,_,serial=run(commands,False);actual,text,parallel=run(commands)
assert text.count('Time workers:')==2
compare(actual,reference)
print(f'VVM ZINT + full 384x384 area, 75 levels, 32 times: serial={serial:.2f}s workers={parallel:.2f}s; masks and values agree',flush=True)

commands='set x 1\nset y 1\nset z 1\nset t 1 last\n'+\
    'define answer=aave(qv,x=1,x=384,y=1,y=384)\nd answer\n'+\
    'd aave(qv,x=1,x=384,y=1,y=384)\n'
reference,_,_=run(commands,False);actual,text,_=run(commands)
assert text.count('Time workers:')==2 and 'times=361' in text
compare(actual,reference)
print('VVM set t 1 last: all 361 template times, full-area DEFINE/DISPLAY passed',flush=True)

# Input-time reductions retain their accumulator order and missing masks.
setup='set x 1 64\nset y 1 64\nset z 1\nset t 1\n'
expressions=[f'{fn}(qv,t=1,t=32)' for fn in ('ave','mean','sum','sumg','min','max','minloc','maxloc')]
expressions+=['sum(qv*th,t=1,t=32)','mean(qv+qc,t=1,t=32)']
commands=setup+'\n'.join('d '+e for e in expressions)
reference,_,_=run(commands,False);actual,text,_=run(commands)
assert text.count('Time workers:')>=len(expressions)
compare(actual,reference)
print('VVM all 8 input-time reductions, single fields and compound expressions: passed',flush=True)

# Reproduce the user's session-defined density. It deliberately stays local.
bar=ctl.with_name('bar.ctl')
commands=f'open {bar}\nset x 1\nset y 1\nset z 1 75\nset t 1\ndefine rho=rho.2\n'+\
    'set z 1\nset t 1 32\ndefine c=aave(zint(rho*qv),x=1,x=16,y=1,y=12)\nd c\n'
reference,_,_=run(commands,False);actual,text,_=run(commands)
assert 'Time workers:' not in text
compare(actual,reference)
print(f'VVM session-defined rho fallback: passed; total={samples} values max_abs={max_abs:.17g}',flush=True)
PY
