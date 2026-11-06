#!/usr/bin/env bash
# Ordered input-time reductions through isolated readers. GPL-2.0-only.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-mean.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
"${CC:-cc}" -std=gnu99 -O2 -DZS_HDF5 -DNX=64 -DNY=64 -DNT=320 \
  "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
# Make the active-reader interval deterministic for terminal cancellation and
# failure tests. Only child HDF5 calls are delayed; production has no test hook.
cat > "$test_root/slow.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <hdf5.h>
herr_t H5Dread(hid_t a,hid_t b,hid_t c,hid_t d,hid_t e,void *f) {
  static herr_t (*next)(hid_t,hid_t,hid_t,hid_t,hid_t,void *);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"H5Dread");
  if(getenv("GA_TIME_AFFINITY"))usleep(20000);
  return next(a,b,c,d,e,f);
}
C
"${CC:-cc}" -shared -fPIC -O2 "${hflags[@]}" "$test_root/slow.c" -ldl -o "$test_root/slow.so"
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,pty,re,select,signal,subprocess,sys,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);env=dict(os.environ,GA_TIME_TRACE='1',GA_PROGRESS='off',OPENGRADS_COLOR='0')
header=('undef -9999\nxdef 64 linear 10 .1\nydef 64 linear -60 .25\n'
        'zdef 4 levels 50 150 400 1000\ntdef 320 linear 00z01jan2000 1hr\n'
        'vars 4\nf 4 {dims} F\ng 4 {dims} G\nsurface 0 {surface} Surface\nrho 4 z Density\nendvars\n')
ctls=[]
for template,reverse in ((False,False),(True,False),(False,True)):
    ctl=root/(('template' if template else 'regular')+('-reverse' if reverse else '')+'.ctl');ctls.append(ctl)
    ctl.write_text('dset ^'+('slice%tm1.h5' if template else 'slice.h5')+'\ndtype hdf5_grid\n'+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        header.format(dims='z,y,x' if template else 't,z,y,x',surface='y,x' if template else 't,y,x'))

def parse(text):
    lines=iter(text.splitlines());result=[]
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n,missing=int(m[1]),float(m[2]);values=[]
        while len(values)<n:values.extend(map(float,next(lines).split()))
        assert len(values)==n
        result.append([None if v==missing else v for v in values])
    return result
samples=0;max_abs=0.
def compare(a,b):
    global samples,max_abs
    assert len(a)==len(b),(len(a),len(b))
    for x,y in zip(a,b):
        assert len(x)==len(y)
        for u,v in zip(x,y):
            assert (u is None)==(v is None),'missing masks differ'
            if u is None:continue
            if math.isnan(u) or math.isnan(v):assert math.isnan(u) and math.isnan(v);continue
            if math.isinf(u) or math.isinf(v):assert u==v;continue
            samples+=1;err=abs(u-v);max_abs=max(max_abs,err)
            assert err<=1e-8+2e-12*abs(v),(u,v)
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
def run(ctl,commands,n=4,off=False,missing=False):
    p=subprocess.run([str(repo/'opengrads'),'-blu','-d','gxdummy','-h','gxdummy'],
        input=f'open {ctl}\nset threads {n}\nset cachesf 2\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n',
        text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        env=dict(env,GA_TIME_WORKERS='off' if off else 'auto'),timeout=180)
    fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error' if missing else r'Error|ERROR|Time evaluation failed'
    assert p.returncode==0 and not re.search(fatal,p.stdout) and not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-3000:]
    for _,pids in groups(p.stdout):
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived quit',pid))
    return p.stdout

setup='set x 1 64\nset y 1 64\nset z 2\nset t 1\n'
expressions=[f'{f}(f,t=1,t=320)' for f in ('ave','mean','sum','sumg','min','max','minloc','maxloc')]
expressions+=['ave(f+g,t=1,t=320)','mean(f+g,t=1,t=320)',
              'ave(f,t=1.25,t=319.6,-b)','mean(f,t=1.25,t=319.6,-b)',
              'ave(f,t=1,t=320,3)','mean(surface,t=1,t=320)']
commands=setup+'\n'.join('d '+expr for expr in expressions)
for ctl in ctls:
    reference=parse(run(ctl,commands,1,True));assert len(reference)==len(expressions)
    for n in (4,8,16):
        text=run(ctl,commands,n);assert len(groups(text))>=len(expressions),'input times not dispatched'
        compare(parse(text),reference)
    print('Time mean values passed:',ctl.name,flush=True)

# A 256-time batch followed by 62 times must reuse a larger existing pool.
text=run(ctls[0],setup+'d mean(f,t=1,t=320)\nd ave(f,t=1,t=320)',128)
g=groups(text);assert [int(n) for n,_ in g]==[128,62,128,62],g
assert g[0][1]==g[2][1] and g[1][1]==g[3][1]
assert set(g[1][1].split(','))<=set(g[0][1].split(','))
compare(parse(text),parse(run(ctls[0],setup+'d mean(f,t=1,t=320)\nd ave(f,t=1,t=320)',1,True)))

# DEFINE must also retain all Z planes when only its input times vary.
command='set x 1 16\nset y 1 16\nset z 1 4\nset t 1\ndefine answer=mean(f,t=1,t=320)\n'+\
        '\n'.join(f'set z {z}\nd answer' for z in range(1,5))
compare(parse(run(ctls[0],command)),parse(run(ctls[0],command,1,True)))

# Missing time files keep the ordinary per-cell denominator and paired masks.
piece=root/'slice160.h5';saved=root/'slice160.saved';piece.rename(saved)
command=setup+'d mean(f,t=1,t=320)\nd mean(f+g,t=1,t=320)'
compare(parse(run(ctls[1],command,missing=True)),parse(run(ctls[1],command,1,True,True)))
saved.rename(piece)

# Native integer masks and unpacking agree with the independent evaluator,
# including scalar and single-element template attributes.
for ctl in ctls[:2]:
    packed=root/(ctl.stem+'-packed.ctl')
    packed.write_text(ctl.read_text().split('vars 4')[0]+
        'unpack scale_factor add_offset\nvars 1\npacked 4 '+
        ('z,y,x' if ctl==ctls[1] else 't,z,y,x')+' Packed\nendvars\n')
    command=setup+'d mean(packed,t=1,t=320)\nd mean(packed+0,t=1,t=320)'
    value=parse(run(packed,command));compare(value[:1],value[1:])
    compare(value,parse(run(packed,command,1,True)))

sub=root/'other';sub.mkdir();target=sub/'source.ctl';target.write_text(ctls[0].read_text())
linked=root/'linked.ctl';linked.symlink_to(target)
command=setup+'d mean(f,t=1,t=320)'
compare(parse(run(linked,command)),parse(run(linked,command,1,True)))

# Defined inputs and unsupported expressions remain on the original path.
command=setup+'define f=7\nd mean(f,t=1,t=320)\nd mean(maskout(f,-1),t=1,t=320)'
text=run(ctls[0],command);assert not groups(text)
compare(parse(text),parse(run(ctls[0],command,1,True)))
command=setup+'d ave(f,t=0,t=321)\nd mean(f,t=1,t=7)'
compare(parse(run(ctls[0],command)),parse(run(ctls[0],command,1,True)))

def terminal(cancel=False,fail=False,off=False):
    e=dict(env,GA_PROGRESS='off' if off else '.000001',GA_TIME_WORKERS='auto',
           LD_PRELOAD=str(root/'slow.so'),LANG='C',LC_ALL='C',LC_TERMINAL='iTerm2')
    e.pop('TMUX',None)
    pid,fd=pty.fork()
    if pid==0:os.execve(str(repo/'opengrads'),[str(repo/'opengrads'),'-bl','-d','gxdummy','-h','gxdummy'],e)
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
        raise AssertionError(('prompt timeout',output[-1000:]))
    try:
        pump(.3)
        for s in (f'open {ctls[0]}','set threads 4','set x 1 64','set y 1 64','set z 2','set t 1','set gxout stat'):command(s)
        begin=len(output);os.write(fd,b'define answer=mean(f,t=1,t=320)\r');end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.01);current=output[begin:].decode('utf-8','replace');g=groups(current)
            if g and not acted and (fail or (cancel and 'workers x 1 thread' in current)):
                os.kill(int(g[-1][1].split(',')[0]) if fail else pid,signal.SIGKILL if fail else signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('mean did not return to prompt')
        if cancel or fail:assert acted
        if not cancel and not fail:assert 'Define memory allocation size' in current,current[-1000:]
        if fail:assert 'DEFINE error' in current,current[-1000:]
        command('set threads 1');command('d mean(f,t=1,t=2)');command('quit');pump(.1);os.waitpid(pid,0)
        text=output.decode('utf-8','replace')
        for _,pids in groups(text):
            for child in map(int,pids.split(',')):
                try:os.kill(child,0)
                except ProcessLookupError:continue
                raise AssertionError(('mean child survived',child))
        frames=re.findall(r'\r([^\r\n]*?)\x1b\[K',text)
        worker=[f for f in frames if 'workers x 1 thread' in f]
        if off:assert not worker and '\x1b]9;4;' not in text
        elif not fail:
            assert worker,'mean worker progress absent'
            pct=[int(re.search(r'(\d+)%',f)[1]) for f in worker];assert pct==sorted(pct),pct
            assert '\x1b]9;4;0\x07' in text,'mean iTerm progress not cleared'
    finally:
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
        try:os.waitpid(pid,0)
        except ChildProcessError:pass
        os.close(fd)
for cancel,fail,off in ((False,False,False),(True,False,False),(False,True,False),(False,False,True)):
    terminal(cancel,fail,off)
    print('Time mean terminal/recovery passed:',cancel,fail,off,flush=True)
print('Time mean workers passed: samples=%d max_abs=%.17g; masks, order, weights, increments, pairs, templates, pool reuse, DEFINE and fallback agree.'%(samples,max_abs))
PY
