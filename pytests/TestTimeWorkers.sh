#!/usr/bin/env bash
# Isolated time evaluation: scientific results, display, progress and recovery.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d /tmp/opengrads-time-workers.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
"${CC:-cc}" -std=gnu99 -O2 -DZS_HDF5 -DNX=256 -DNY=256 -DNT=32 \
  "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
python3 -u - "$repo_root" "$test_root" <<'PY'
import errno,math,os,pty,re,select,signal,subprocess,sys,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=str(repo/'opengrads')
env=dict(os.environ,OPENGRADS_BUILD_ROOT=os.environ.get('OPENGRADS_BUILD_ROOT',str(repo/'build')),
         OPENGRADS_COLOR='0',GA_TIME_TRACE='1',GA_PROGRESS='off')
header=('undef -9999\nxdef 256 linear 10 .1\nydef 256 linear -60 .25\n'
        'zdef 4 levels 50 150 400 1000\ntdef 32 linear 00z01jan2000 1hr\n'
        'vars 4\nf 4 {dims} F\ng 4 {dims} G\nsurface 0 {surface} Surface\nrho 4 z Density\nendvars\n')
ctls=[]
for template,reverse in ((False,False),(True,False),(False,True)):
    name=('template' if template else 'regular')+('-reverse' if reverse else '')
    ctl=root/(name+'.ctl');ctls.append(ctl)
    ctl.write_text('dset ^'+('slice%tm1.h5' if template else 'slice.h5')+'\ndtype hdf5_grid\n'+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        header.format(dims='z,y,x' if template else 't,z,y,x',surface='y,x' if template else 't,y,x'))

def parse(text):
    lines=iter(text.splitlines());grids=[]
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if m:
            n,missing=int(m[1]),float(m[2]);values=[]
            while len(values)<n:values.extend(float(v) for v in next(lines).split())
            assert len(values)==n
            grids.append([None if v==missing else v for v in values])
    return grids
samples=0;max_abs=0.
def compare(a,b):
    global samples,max_abs
    assert len(a)==len(b)
    for ga,gb in zip(a,b):
        assert len(ga)==len(gb)
        for x,y in zip(ga,gb):
            assert (x is None)==(y is None),'mask mismatch'
            if x is None:continue
            assert math.isfinite(x) and math.isfinite(y)
            error=abs(x-y);max_abs=max(max_abs,error);samples+=1
            assert error<=1e-8+2e-12*abs(y),(x,y)
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
def assert_dead(text):
    for _,pids in groups(text):
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived',pid))
def run(ctl,commands,n=4,off=False,missing=False):
    e=dict(env,GA_TIME_WORKERS='off' if off else 'auto')
    text=f'open {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n'
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo'],input=text,text=True,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=e,timeout=180)
    # A missing template produces HDF5 diagnostics but remains a successful
    # GrADS command with missing values. Still reject every nonzero command RC.
    fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error' if missing else r'Error|ERROR|Time evaluation failed'
    assert p.returncode==0 and not re.search(fatal,p.stdout) and not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-3000:]
    assert_dead(p.stdout)
    return p.stdout

profile='set x 1\nset y 1\nset z 1 4\nset t 1 32\n'
cases=[('ave(f,x=1,x=256)',profile),
       ('aave(f,x=1,x=256,y=1,y=256)',profile),
       ('aave(f,x=1.25,x=255.6,y=2.4,y=250.7)',profile),
       ('aave(maskout(f,(f>4500)-0.5),x=1,x=256,y=1,y=256)',profile),
       ('aave(surface,x=1,x=256,y=1,y=256)',profile),
       ('amean(f,x=1,x=256,y=1,y=256)',profile),
       ('amean(f,x=1.25,x=255.6,y=2.4,y=250.7)',profile),
       ('amean(maskout(f,(f>4500)-0.5),x=1,x=256,y=1,y=256)',profile),
       ('amean(surface,x=1,x=256,y=1,y=256)',profile),
       ('zint(f*rho)','set x 1 8\nset y 1 8\nset z 1\nset t 1 32\n')]
for ctl in ctls:
    for expression,setup in cases:
        command=setup+'define answer='+expression+'\nd answer\n'
        reference=parse(run(ctl,command,1,True))
        for n in (2,4,8):
            text=run(ctl,command,n);assert groups(text),(ctl,expression,'no workers')
            compare(parse(text),reference)
        if expression.startswith(('ave(','aave(','amean(')):
            direct=run(ctl,setup+'d '+expression)
            compare(parse(direct),reference)
    print('Time workers values/display passed:',ctl.name,flush=True)

# AMEAN also displays Z alone (serial assembly), and T alone (time workers).
for setup in ('set x 1\nset y 1\nset z 1 4\nset t 3\n',
              'set x 1\nset y 1\nset z 2\nset t 1 32\n'):
    expr='amean(f,x=1,x=256,y=1,y=256)'
    reference=parse(run(ctls[0],setup+'define answer='+expr+'\nd answer',1,True))
    compare(parse(run(ctls[0],setup+'d '+expr)),reference)

# Missing middle template time retains each function's established mask rules.
piece=root/'slice16.h5';saved=root/'slice16.saved';piece.rename(saved)
for expression,setup in (cases[1],cases[-1]):
    command=setup+'define answer='+expression+'\nd answer\n'
    compare(parse(run(ctls[1],command,missing=True)),parse(run(ctls[1],command,1,True,missing=True)))
saved.rename(piece)

# ^paths retain the descriptor symlink's directory, rather than its target's.
sub=root/'other';sub.mkdir();target=sub/'source.ctl';target.write_text(ctls[0].read_text())
linked=root/'linked.ctl';linked.symlink_to(target)
command=profile+'define answer='+cases[1][0]+'\nd answer\n'
compare(parse(run(linked,command)),parse(run(linked,command,1,True)))

# A persistent pool is reused by DEFINE and DISPLAY, then released on reinit.
text=run(ctls[0],profile+'define answer='+cases[1][0]+'\nd '+cases[1][0]+'\nd answer\n')
g=groups(text);assert len(g)==2 and g[0]==g[1],g
value=parse(text);compare(value[:1],value[1:])
shadow=run(ctls[0],'set x 1 256\nset y 1 256\nset z 1\nset t 1\ndefine f=7\n'+profile+
    'define answer=aave(f,x=1,x=256,y=1,y=256)\nd answer\n')
assert not groups(shadow),'defined field incorrectly sent to worker'

# DEFINE and direct DISPLAY must also render the same Z/T graphic.
a=root/'defined.png';b=root/'direct.png'
run(ctls[0],profile+'define answer='+cases[1][0]+'\nset gxout shaded\nd answer\nprintim '+str(a)+
    ' x640 y480\nclear canvas\nd '+cases[1][0]+'\nprintim '+str(b)+' x640 y480\n')
assert a.read_bytes()==b.read_bytes(),'DEFINE/DISPLAY graphics differ'
expr='amean(f,x=1,x=256,y=1,y=256)'
run(ctls[0],profile+'define answer='+expr+'\nset gxout shaded\nd answer\nprintim '+str(a)+
    ' x640 y480\nclear canvas\nd '+expr+'\nprintim '+str(b)+' x640 y480\n')
assert a.read_bytes()==b.read_bytes(),'AMEAN DEFINE/DISPLAY graphics differ'

heavy='aave('+ '+'.join(['sin(f)']*32)+',x=1,x=256,y=1,y=256)'
def terminal(cancel=False,fail=False,off=False):
    e=dict(env,GA_PROGRESS='off' if off else '.000001',GA_TIME_WORKERS='auto',LANG='C',LC_ALL='C',LC_TERMINAL='iTerm2')
    e.pop('TMUX',None)
    pid,fd=pty.fork()
    if pid==0:os.execve(launcher,[launcher,'-bl','-d','gxdummy','-h','gxdummy'],e)
    output=bytearray()
    def pump(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([fd],[],[],.02)[0]:
                try:output.extend(os.read(fd,65536))
                except OSError:return
    def command(s,limit=30):
        start=len(output);os.write(fd,(s+'\r').encode());end=time.monotonic()+limit
        while time.monotonic()<end:
            pump(.04)
            if b'ga->' in output[start+len(s):]:return
            if s=='quit' and b'GX Package Terminated' in output[start:]:return
        raise AssertionError(('prompt timeout',output[-1000:]))
    try:
        pump(.5)
        for s in (f'open {ctls[0]}','set threads 4','set x 1','set y 1','set z 1 4','set t 1 32'):command(s)
        begin=len(output);os.write(fd,('define answer='+heavy+'\r').encode());end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.02);current=output[begin:].decode('utf-8','replace')
            g=groups(current)
            if g and not acted and (fail or (cancel and 'workers x 1 thread' in current)):
                os.kill(int(g[-1][1].split(',')[0]) if fail else pid,signal.SIGKILL if fail else signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('worker command did not return to prompt')
        if cancel or fail:assert acted,'test did not reach active workers'
        if not cancel and not fail:assert 'Define memory allocation size' in current,current[-1000:]
        if fail:assert 'DEFINE error' in current,current[-1000:]
        command('set threads 1');command('set t 1');command('set z 1');command('d aave(f,x=1,x=256,y=1,y=256)')
        command('quit');pump(.1);os.waitpid(pid,0)
        text=output.decode('utf-8','replace');assert_dead(text)
        frames=re.findall(r'\r([^\r\n]*?)\x1b\[K',text)
        worker=[f for f in frames if 'workers x 1 thread' in f]
        if off:assert not worker and '\x1b]9;4;' not in text
        elif not fail:
            assert worker,'worker progress absent'
            pct=[int(re.search(r'(\d+)%',f)[1]) for f in worker]
            assert pct==sorted(pct),pct
            assert '\x1b]9;4;0\x07' in text,'iTerm2 progress not cleared'
        return text
    finally:
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
        try:os.waitpid(pid,0)
        except ChildProcessError:pass
        os.close(fd)
for cancel,fail,off in ((False,False,False),(True,False,False),(False,True,False),(False,False,True)):
    terminal(cancel,fail,off)
    print('Time workers terminal/recovery passed:',cancel,fail,off,flush=True)
print('Time workers passed: samples=%d max_abs=%.17g; masks agree; display, reuse, progress, cancellation, crash recovery and cleanup passed.'%(samples,max_abs))
PY
