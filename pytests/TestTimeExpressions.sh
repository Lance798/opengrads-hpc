#!/usr/bin/env bash
# General expressions: ordered time workers for DEFINE and T/Z-T DISPLAY.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d /tmp/opengrads-time-expressions.XXXXXX)"
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
            if not math.isfinite(x) or not math.isfinite(y):
                assert x==y or (math.isnan(x) and math.isnan(y)),(x,y)
                continue
            error=abs(x-y);max_abs=max(max_abs,error);samples+=1
            assert error<=1e-8+2e-12*abs(y),(x,y)
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
def assert_dead(text):
    for _,pids in groups(text):
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived',pid))
def run(ctl,commands,n=4,off=False,missing=False,error=False):
    e=dict(env,GA_TIME_WORKERS='off' if off else 'auto')
    text=f'open {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n'
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo'],input=text,text=True,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=e,timeout=180)
    # A missing template produces HDF5 diagnostics but remains a successful
    # GrADS command with missing values. Still reject every nonzero command RC.
    fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error' if missing else r'Error|ERROR|Time evaluation failed'
    bad=bool(re.search(fatal,p.stdout)) or any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout))
    assert p.returncode==0 and bad==error,p.stdout[-3000:]
    assert_dead(p.stdout)
    return p.stdout


xy='set x 1 8\nset y 1 6\nset z 1 4\nset t 1 32\n'
zt='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
core=['f','surface','rho','surface+rho','f+g','f-g','f*g','f/g','2-f','2/f','-(f-g)',
      '(f+g)*0.5-surface/rho','f/(f-f)','pow(1000-f,0.5)','mag(f,g)',
      'sqrt(f*f+g*g)','maskout(f,(f>2000)-0.5)',
      '(f>1000)&(g<20000)','(f<1000)||(g>20000)','f!=g']
maths=['pow(f,2)','abs(-f)','exp(f/10000)','log(f)','log10(f)',
       'sin(f/10000)','cos(f/10000)','tan(f/10000)',
       'asin(sin(f/10000))','acos(cos(f/10000))','asin(f)','acos(f)',
       'atan2(f,g)','exp(f)','const(f,0)',
       'maskout(f,(f>1e9)-0.5)','maskout(f,(g>2000)-0.5)+surface']
for ctl in ctls:
    for setup,expressions,direct in ((xy,core,False),(zt,core+maths,True)):
        render=''.join('set z '+str(k)+'\nd answer\n' for k in range(1,5))+'set z 1 4\n' if not direct else 'd answer\n'
        command=setup+''.join('define answer='+expr+'\n'+render for expr in expressions)
        reference=parse(run(ctl,command,1,True))
        text=run(ctl,command);assert len(groups(text))==len(expressions),(ctl,groups(text))
        compare(parse(text),reference)
        if direct:
            text=run(ctl,setup+''.join('d '+expr+'\n' for expr in expressions))
            assert len(groups(text))==len(expressions),(ctl,groups(text))
            compare(parse(text),reference)
    print('Time expressions DEFINE/ZT passed:',ctl.name,flush=True)

# T-only display, one varying spatial axis, non-unit coordinates and packing.
for setup in ('set x 6\nset y 4\nset z 2\nset t 1 32\n',
              'set x 2 7\nset y 4\nset z 2\nset t 1 32\n'):
    command=setup+'define answer=pow(f-g,2)+surface/rho\nd answer\n'
    reference=parse(run(ctls[0],command,1,True));text=run(ctls[0],command)
    assert groups(text);compare(parse(text),reference)
    if 'set x 6\n' in setup:
        text=run(ctls[0],setup+'d pow(f-g,2)+surface/rho\n')
        assert groups(text);compare(parse(text),reference)
packed=root/'packed.ctl'
packed.write_text('dset ^slice.h5\ndtype hdf5_grid\nunpack scale_factor add_offset\nundef -32768 _FillValue\n'+
    header[:header.index('vars 4')].split('undef -9999\n')[1]+
    'vars 1\npacked 4 t,z,y,x Packed\nendvars\n')
command=xy+'define answer=(packed+3)/2\n'+''.join('set z '+str(k)+'\nd answer\n' for k in range(1,5))
text=run(packed,command);assert groups(text)
compare(parse(text),parse(run(packed,command,1,True)))

# A missing template time must retain per-operand masks, including CONST.
piece=root/'slice16.h5';saved=root/'slice16.saved';piece.rename(saved)
command=zt+'define answer=f+g\nd answer\nd const(f,0)+g\n'
compare(parse(run(ctls[1],command,missing=True)),parse(run(ctls[1],command,1,True,missing=True)))
saved.rename(piece)

# Session-only data and dimensions not reconstructed by workers stay local.
for expr in ('f.1+g','f(t=2)+g','if(f>1000,f,g)'):
    command=zt+'define answer='+expr+'\nd answer\n'+('set t 1\n' if '(t=' in expr else '')+'d '+expr+'\n'
    text=run(ctls[0],command);assert not groups(text),expr
    compare(parse(text),parse(run(ctls[0],command,1,True)))
command='set x 1\nset y 1\nset z 1\nset t 1\ndefine saved=7\n'+zt+\
    'define answer=saved+f\nd answer\nd saved+f\n'
text=run(ctls[0],command);assert not groups(text)
compare(parse(text),parse(run(ctls[0],command,1,True)))
for setup in (zt.replace('1 32','1 31'),zt.replace('1 32','1')):
    text=run(ctls[0],setup+'define answer=f+g\nd answer\nd f+g\n')
    assert not groups(text),'short series created workers'
    compare(parse(text),parse(run(ctls[0],setup+'define answer=f+g\nd answer\nd f+g\n',1,True)))

# Fix Z outside a vertical sum; each output time retains the original fold.
setup='set x 5\nset y 3\nset z 1\nset t 1 32\n'
for expr in ('2*sum(f,z=1,z=4)','pow(mean(f,z=1,z=4),2)',
             'ave(f,z=1,z=4)','mean(f,z=1,z=4)','sum(f,z=1,z=4)','sumg(f,z=1,z=4)',
             'maskout(sum(f,z=1,z=4),(sum(f,z=1,z=4)>5000)-0.5)'):
    command=setup+'define answer='+expr+'\nd answer\nd '+expr+'\n'
    text=run(ctls[0],command);assert len(groups(text))==2,expr
    compare(parse(text),parse(run(ctls[0],command,1,True)))

# Additional allowed outer area reductions use the same complete evaluator.
for fn in ('aave','amean','asum','atot'):
    expr=fn+'(f,x=1,x=8,y=1,y=6)'
    command=zt+'define answer='+expr+'\nd answer\n'+('d '+expr+'\n' if fn in ('aave','amean') else '')
    text=run(ctls[0],command);assert len(groups(text))==(2 if fn in ('aave','amean') else 1),expr
    compare(parse(text),parse(run(ctls[0],command,1,True)))

# Unsupported native DISPLAY environments must not become valid only with workers.
for expr in ('asum(f,x=1,x=8,y=1,y=6)','atot(f,x=1,x=8,y=1,y=6)',
             'sin(aave(f,x=1,x=8,y=1,y=6))','2*zint(f)'):
    text=run(ctls[0],zt.replace('set z 1 4','set z 1')+'d '+expr+'\n',error=True)
    assert not groups(text),expr
    run(ctls[0],zt.replace('set z 1 4','set z 1')+'d '+expr+'\n',1,True,error=True)

# Reducing a DISPLAY axis keeps the native shape (not a repeated Z/T grid).
for expr in ('2*sum(f,z=1,z=4)','pow(mean(f,z=1,z=4),2)',
             'pow(mean(f,t=1,t=32),2)'):
    text=run(ctls[0],zt+'d '+expr+'\n');assert not groups(text),expr
    compare(parse(text),parse(run(ctls[0],zt+'d '+expr+'\n',1,True)))

# The same pool serves different expressions, DEFINE and direct DISPLAY.
text=run(ctls[0],zt+'define answer=f+g\nd f+g\nd pow(f,2)\n')
g=groups(text);assert len(g)==3 and g[0]==g[1]==g[2],g
value=parse(text);compare(value[:1],parse(run(ctls[0],zt+'d f+g',1,True)))

# Compare both worker graphics with the original evaluator's graphics.
for expr in ('f+g','mag(f,g)','pow(f,2)'):
    a=root/'defined.png';b=root/'direct.png';c=root/'serial.png'
    run(ctls[0],zt+'define answer='+expr+'\nset gxout shaded\nd answer\nprintim '+str(a)+
        ' x640 y480\nclear canvas\nd '+expr+'\nprintim '+str(b)+' x640 y480\n')
    run(ctls[0],zt+'set gxout shaded\nd '+expr+'\nprintim '+str(c)+' x640 y480\n',1,True)
    assert a.read_bytes()==b.read_bytes()==c.read_bytes(),('expression graphics differ',expr)

heavy='('+ '+'.join(['sin(f)']*32)+')/32+pow(g,2)*0.000001'
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
        for s in (f'open {ctls[0]}','set threads 4','set x 1 256','set y 1 256','set z 1','set t 1 32'):command(s)
        command('define answer=7');begin=len(output);os.write(fd,('define answer='+heavy+'\r').encode());end=time.monotonic()+30;acted=False
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
        command('set threads 1');command('set t 1');command('set z 1');command('set x 5');command('set y 3');command('set gxout print');command('d answer');command('d f+g')
        command('quit');pump(.1);os.waitpid(pid,0)
        text=output.decode('utf-8','replace');assert_dead(text)
        if cancel or fail:assert parse(text)[0]==[7.0],'failed replacement changed old DEFINE'
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
    print('Time expressions terminal/recovery passed:',cancel,fail,off,flush=True)
print('Time expressions passed: samples=%d max_abs=%.17g; masks, invalid classifications, DEFINE/DISPLAY, graphics, progress, cancellation and recovery agree.'%(samples,max_abs))
PY
