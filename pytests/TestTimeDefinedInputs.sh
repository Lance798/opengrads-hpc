#!/usr/bin/env bash
# Command-local static scalar/Z/XY/XYZ DEFINE snapshots in time workers.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-defined-inputs.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
source "$repo_root/pytests/time_worker_format.sh"
"${CC:-cc}" -std=gnu99 -O2 "$writer_define" -DNX=256 -DNY=256 -DNT=32 \
  "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
if [[ "$(uname -s)" == Linux ]]; then
  cat > "$test_root/slow.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
ssize_t read(int fd,void *p,size_t n) {
  static ssize_t (*next)(int,void *,size_t);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"read");
  if(fd==3 && n>8*1024*1024 && getenv("GA_TIME_AFFINITY"))usleep(500000);
  return next(fd,p,n);
}
C
  "${CC:-cc}" -shared -fPIC -O2 "$test_root/slow.c" -ldl -o "$test_root/slow.so"
fi
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,pty,re,select,signal,struct,subprocess,sys,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
fmt=os.environ.get('GA_TEST_TIME_FORMAT','h5');ext='h5' if fmt=='h5' else 'nc'
dtype='hdf5_grid' if ext=='h5' else 'netcdf'
regular='slice'+('_compressed' if fmt=='nc4' else '')+'.'+ext
env=dict(os.environ,OPENGRADS_BUILD_ROOT=os.environ.get('OPENGRADS_BUILD_ROOT',str(repo/'build')),
         OPENGRADS_COLOR='0',OPENGRADS_UPDATE_CHECK='0',GA_TIME_TRACE='1',GA_PROGRESS='off')
axis=('undef -9999\nxdef 256 linear 10 .1\nydef 256 linear -60 .25\n'
      'zdef 4 levels 50 150 400 1000\ntdef 32 linear 00z01jan2000 1hr\n')
ctls=[]
for template,reverse in ((False,False),(True,False),(False,True)):
    ctl=root/(('template' if template else 'regular')+('-reverse' if reverse else '')+'.ctl')
    ctls.append(ctl)
    dims='z,y,x' if template and ext=='h5' else 't,z,y,x'
    ctl.write_text('dset ^'+('slice%tm1.'+ext if template else regular)+'\ndtype '+dtype+'\n'+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        axis+'vars 2\nf 4 '+dims+' Field\ng 4 '+dims+' Other\nendvars\n')
bar=root/'bar.ctl'
bar.write_text('dset ^bar.dat\nundef -9999\nxdef 1 linear 10 .1\nydef 1 linear -60 .25\n'+
               'zdef 4 levels 50 150 400 1000\ntdef 1 linear 00z01jan2000 1hr\n'+
               'vars 3\nrho 4 99 Density\npibar 4 99 Exner\npbar 4 99 Pressure\nendvars\n')
(root/'bar.dat').write_bytes(struct.pack('=12f',1.2,1.1,1.,.9,1.,.95,.8,.6,100000.,85000.,50000.,20000.))
init=f'open {bar}\nset x 1\nset y 1\nset z 1 4\nset t 1\n'+\
     'define rhop=rho.2\ndefine pi=pibar.2\ndefine pp=pbar.2\nset z 1\ndefine scale=1.25\nset z 1 4\n'
zt='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
point='set x 1\nset y 1\nset z 1\nset t 1 32\n'
xy='set x 1 8\nset y 1 6\nset z 1 4\nset t 1 32\n'
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
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
samples=0;max_abs=0.
def compare(a,b):
    global samples,max_abs
    assert len(a)==len(b),(len(a),len(b))
    for x,y in zip(a,b):
        assert len(x)==len(y)
        for u,v in zip(x,y):
            assert (u is None)==(v is None),(u,v)
            if u is None:continue
            if not math.isfinite(u) or not math.isfinite(v):
                assert u==v or math.isnan(u) and math.isnan(v),(u,v)
                continue
            e=abs(u-v);max_abs=max(max_abs,e);samples+=1
            assert e<=1e-8+2e-12*abs(v),(u,v)
def run(ctl,commands,n=4,off=False,missing=False):
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo'],
        input=f'open {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n',
        universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        env=dict(env,GA_TIME_WORKERS='off' if off else 'auto'),timeout=180)
    fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error' if missing else r'Error|ERROR|Time evaluation failed'
    assert p.returncode==0 and not re.search(fatal,p.stdout),p.stdout[-5000:]
    assert not any(int(x)>0 for x in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-5000:]
    for _,pids in groups(p.stdout):
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived quit',pid))
    return p.stdout
def check(ctl,command,minimum=1,n=4,missing=False):
    reference=grids(run(ctl,command,1,True,missing));text=run(ctl,command,n,missing=missing)
    assert len(groups(text))>=minimum,text[-2500:]
    assert 'Time defined inputs:' in text,text[-2500:]
    compare(grids(text),reference)
    return text

expressions=['f*rhop','f*scale+rhop','esat((150+f/200)*pi)',
             'qvsat((150+f/200)*pi,pp)','maskout(f*rhop,(f>2000)-0.5)',
             'pow(f*pi,2)/pp','(f*rhop)+(f*rhop)']
for ctl in ctls:
    for expr in expressions:
        command=init+zt+'define answer='+expr+'\nd answer\nd '+expr+'\n'
        text=check(ctl,command,2)
        if expr.endswith('(f*rhop)'):assert 'count=1 ' in text,'duplicate profile copied twice'
    command=init+xy+'define answer=qvsat((150+f/200)*pi,pp)*scale\n'+\
        '\n'.join(f'set z {z}\nd answer' for z in range(1,5))
    for n in (2,4):check(ctl,command,n=n)
    expr='aave(zint(f*rhop),x=1,x=16,y=1,y=12)'
    check(ctl,init+point+'define answer='+expr+'\nd answer\nd '+expr+'\n',2)
    # All input-time selectors dispatch the full expression with definitions.
    command=init+'set x 1 8\nset y 1 6\nset z 2\nset t 1\n'+\
        '\n'.join(f'd {fn}(qvsat((150+f/200)*pi,pp)*scale,t=1,t=32)' for fn in
                  ('ave','mean','sum','sumg','min','max','minloc','maxloc'))
    check(ctl,command,8)
    check(ctl,init+'set x 1\nset y 1\nset z 1 4\nset t 1\nd sum(f*rhop,t=1,t=32)\n')
    print('Defined input values/columns/input reductions passed:',fmt,ctl.name,flush=True)

# Cropped profile offsets and absent/outside levels must retain ordinary masks.
command=init+'set z 2 3\ndefine cropped=rho.2\n'+zt+\
        'define answer=f*cropped\nd answer\nd f*cropped\n'
check(ctls[0],command,2)
# Linear Z coefficients must be copied as well as explicit nonuniform levels.
linear=root/'linear.ctl';linear.write_text(ctls[0].read_text().replace('zdef 4 levels 50 150 400 1000','zdef 4 linear 50 100'))
linearbar=root/'linearbar.ctl';linearbar.write_text(bar.read_text().replace('zdef 4 levels 50 150 400 1000','zdef 4 linear 50 100'))
check(linear,init.replace(str(bar),str(linearbar))+zt+'define answer=f*rhop\nd answer\nd f*rhop\n',2)

# Fresh copies on every command: redefine, SET DEFVAL, UNDEFINE and reuse.
command=init+zt+'define answer=f*rhop*scale\nd answer\n'+\
    'set t 1\nset z 1 4\ndefine rhop=rhop*2\nset defval scale 1 1 2.5\n'+zt+\
    'define answer=f*rhop*scale\nd answer\nset defval rhop 1 1 9\n'+\
    'define answer=f*rhop*scale\nd answer\nset t 1\nset z 1 4\nundefine rhop\ndefine rhop=rho.2\n'+zt+\
    'define answer=f*rhop*scale\nd answer\n'
text=check(ctls[0],command,4);g=groups(text);assert len(g)==4 and all(x==g[0] for x in g),g
values=grids(text);assert values[0]!=values[1] and values[1]!=values[2]
check(ctls[0],init+'close 2\n'+zt+'define answer=f*rhop\nd answer\nd f*rhop\n',2)
# Missing/nonfinite profile operands remain missing, or contribute zero to ZINT.
command=init+'define bad=maskout(rhop,-1)\nset z 1\ndefine badscalar=1/0\n'+zt+\
        'define answer=f*bad\nd answer\nd f*badscalar\n'+point+\
        'define answer=zint(f*bad)\nd answer\nd zint(f*bad)\n'
check(ctls[0],command,4)
badbar=root/'badbar.ctl';badbar.write_text(bar.read_text().replace('bar.dat','badbar.dat'))
(root/'badbar.dat').write_bytes(struct.pack('=12f',1.2,-9999.,float('nan'),float('inf'),1.,.95,.8,.6,100000.,0.,-1.,-9999.))
check(ctls[0],init.replace(str(bar),str(badbar))+zt+'define answer=qvsat((150+f/200)*pi,pp)+f*rhop\nd answer\n')

# Static XY and XYZ masks retain cropping offsets and missing cells.
for ctl in ctls:
    for z in ('1','1 4'):
        command=init+'set x 2 9\nset y 2 7\nset t 1\nset z '+z+'\n'+\
            'define landmask=(f>300)-0.5\n'+xy+\
            'define answer=maskout(f,landmask)*rhop\n'+\
            '\n'.join(f'set z {k}\nd answer' for k in range(1,5))
        check(ctl,command)
        command=init+'set x 1 8\nset y 1 6\nset t 1\nset z '+z+'\n'+\
            'define fixed=f\nset x 1 8\nset y 1 6\nset z 2\nset t 1\n'+\
            'd sum(f*fixed,t=1,t=32)\n'
        check(ctl,command)
# Time-varying inputs keep the original path.
for setup,expr in ((zt+'define unsupported=f\n','f+unsupported'),):
    command=init+setup+zt+'define answer='+expr+'\nd answer\nd '+expr+'\n'
    text=run(ctls[0],command);assert len(groups(text))==1 and 'Time defined inputs:' not in text
    compare(grids(text),grids(run(ctls[0],command,1,True)))
# Bound dependency metadata too: 32 scalar inputs fit, 33 fall back.
definitions='set z 1\n'+''.join(f'define v{i}=1\n' for i in range(33))
for n in (32,33):
    expr='f+'+'+'.join(f'v{i}' for i in range(n))
    command=init+definitions+zt+'define answer='+expr+'\nd answer\n'
    text=run(ctls[0],command);assert bool(groups(text))==(n==32)
    compare(grids(text),grids(run(ctls[0],command,1,True)))
# A 36 MiB static cube exceeds the old small-input limit but fits the budget.
# Four such cubes exceed the 128 MiB command cap and must stay local.
large=root/'large.ctl';large.write_text(ctls[0].read_text().replace('zdef 4 levels 50 150 400 1000','zdef 64 linear 50 100'))
large_setup='set x 1 256\nset y 1 256\nset z 1 64\nset t 1\ndefine big=lon+lat+lev\n'
check(large,large_setup+point+'define answer=f*big\nd answer\n')
command=large_setup+''.join(f'define big{i}=big+{i}\n' for i in range(3))+point+\
        'define answer=f+big+big0+big1+big2\nd answer\n'
text=run(large,command);assert not groups(text),'over-budget snapshots started workers'
compare(grids(text),grids(run(large,command,1,True)))
# Native-name precedence: a supported scalar may shadow a native field.
command=init+'set z 1\ndefine f=7\n'+zt+'define answer=f+g\nd answer\nd f+g\n'
check(ctls[0],command,2)
# Removing a native-name shadow must remove the worker's old binding too.
command=init+'set z 1\ndefine f=7\n'+zt+'define answer=f+g\nd answer\n'+\
        'undefine f\ndefine answer=f+g\nd answer\n'
text=check(ctls[0],command,2);g=groups(text);assert g[0]==g[1] and text.count('Time defined inputs:')==1
assert grids(text)[0]!=grids(text)[1]
command=init+'set x 1 8\nset y 1 6\nset z 1\nset t 1\ndefine landmask=const(f,1)\n'+xy+\
        'define answer=maskout(f,landmask)\nset z 1\nset t 1\nd answer\nset defval landmask 3 2 missing\n'+xy+\
        'define answer=maskout(f,landmask)\nset z 1\nset t 1\nd answer\n'
text=check(ctls[0],command,2);assert grids(text)[0]!=grids(text)[1]
# A defined profile alone does not start an I/O pool.
text=run(ctls[0],init+zt+'define answer=rhop\nd answer\nd rhop\n');assert not groups(text)
compare(grids(text),grids(run(ctls[0],init+zt+'define answer=rhop\nd answer\nd rhop\n',1,True)))

piece=root/('slice16.'+ext);saved=root/'saved';piece.rename(saved)
check(ctls[1],init+point+'define answer=aave(zint(f*rhop),x=1,x=16,y=1,y=12)\nd answer\n',missing=True)
saved.rename(piece)
# Parent graphics must remain the same with snapshots and with workers off.
images=[]
for off in (False,True):
    png=root/f'profile-{off}.png';images.append(png)
    run(ctls[0],init+zt+'define answer=qvsat((150+f/200)*pi,pp)\nset gxout shaded\nd answer\nprintim '+str(png)+' x640 y480\n',1 if off else 4,off)
assert images[0].read_bytes()==images[1].read_bytes(),'snapshot graphics differ'

def transfer_interruption(fail):
    e=dict(env,LD_PRELOAD=str(root/'slow.so'),GA_PROGRESS='.000001',GA_TIME_WORKERS='auto',LANG='C',LC_ALL='C')
    pid,fd=pty.fork()
    if pid==0:os.execve(launcher,[launcher,'-bl','-d','gxdummy','-h','gxdummy'],e)
    output=bytearray()
    def pump(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([fd],[],[],.01)[0]:
                try:output.extend(os.read(fd,65536))
                except OSError:return
    def command(s):
        start=len(output);os.write(fd,(s+'\r').encode());end=time.monotonic()+30
        while time.monotonic()<end:
            pump(.02)
            if b'ga->' in output[start+len(s):] or (s=='quit' and b'GX Package Terminated' in output[start:]):return
        raise AssertionError(('prompt timeout',output[-1500:]))
    try:
        pump(.3)
        for s in (f'open {large}','set threads 4',*large_setup.strip().splitlines(),
                  'set x 6','set y 4','set z 1','define gain=2','set t 1 32','define answer=7'):command(s)
        begin=len(output);os.write(fd,b'define answer=f*big\r');end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.01);current=output[begin:].decode('utf-8','replace');g=groups(current)
            if g and not acted and (fail or 'workers x 1 thread' in current):
                os.kill(int(g[0][1].split(',')[0]) if fail else pid,signal.SIGKILL if fail else signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('snapshot transfer interruption hung')
        assert acted and 'Time defined inputs:' in current,current[-2000:]
        if fail:assert 'DEFINE error' in current,current[-1500:]
        command('set t 1');command('set gxout print');command('set prnopts %.17g 32 1');command('d answer')
        printed=grids(output.decode('utf-8','replace'));assert printed[-1]==[7.],printed[-1]
        command('set t 1 32');command('define recovered=f*gain');command('d recovered')
        printed=grids(output.decode('utf-8','replace'));compare(printed[-1:],[[2*(135+1000*t) for t in range(32)]])
        command('quit');pump(.1);os.waitpid(pid,0)
        for _,pids in groups(output.decode('utf-8','replace')):
            for child in map(int,pids.split(',')):
                try:os.kill(child,0)
                except ProcessLookupError:continue
                raise AssertionError(('snapshot worker survived',child))
    finally:
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
        try:os.waitpid(pid,0)
        except ChildProcessError:pass
        os.close(fd)
if (root/'slow.so').exists():
    for fail in (False,True):
        transfer_interruption(fail)
        print('Large snapshot transfer cancellation/failure recovery passed:',fmt,fail,flush=True)
print(f'Defined inputs passed: {fmt}; samples={samples} max_abs={max_abs:.17g}; scalar/Z/XY/XYZ, offsets, masks, refresh, pool reuse, bounded fallback and graphics agree.',flush=True)
PY
