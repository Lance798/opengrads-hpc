#!/usr/bin/env bash
# Ordered input-time reductions through isolated readers. GPL-2.0-only.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-expression-reduction.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
source "$repo_root/pytests/time_worker_format.sh"
"${CC:-cc}" -std=gnu99 -O2 "$writer_define" -DNX=64 -DNY=64 -DNT=320 \
  "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
# Make the active-reader interval deterministic for terminal cancellation and
# failure tests. Only child native reads are delayed; production has no test hook.
cat > "$test_root/slow.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef ZS_NETCDF
#include <netcdf.h>
#else
#include <hdf5.h>
#endif
FILE *fopen(const char *path,const char *mode) {
  static FILE *(*next)(const char *,const char *);
  const char *kib=getenv("GA_TEST_MEM_KIB");
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"fopen");
  if(kib && !strcmp(path,"/proc/meminfo")) {
    FILE *file=tmpfile();
    if(file) { fprintf(file,"MemAvailable: %s kB\n",kib);rewind(file); }
    return file;
  }
  return next(path,mode);
}
#ifdef ZS_NETCDF
int nc_get_vara_double(int a,int b,const size_t *c,const size_t *d,double *e) {
  static int (*next)(int,int,const size_t *,const size_t *,double *);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"nc_get_vara_double");
  if(getenv("GA_TIME_AFFINITY"))usleep(20000);
  return next(a,b,c,d,e);
}
#else
herr_t H5Dread(hid_t a,hid_t b,hid_t c,hid_t d,hid_t e,void *f) {
  static herr_t (*next)(hid_t,hid_t,hid_t,hid_t,hid_t,void *);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"H5Dread");
  if(getenv("GA_TIME_AFFINITY"))usleep(20000);
  return next(a,b,c,d,e,f);
}
#endif
C
"${CC:-cc}" -shared -fPIC -O2 "$writer_define" "${hflags[@]}" "$test_root/slow.c" -ldl -o "$test_root/slow.so"
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,pty,re,select,signal,subprocess,sys,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'));env=dict(os.environ,GA_TIME_TRACE='1',GA_PROGRESS='off',OPENGRADS_COLOR='0')
header=('undef -9999\nxdef 64 linear 10 .1\nydef 64 linear -60 .25\n'
        'zdef 4 levels 50 150 400 1000\ntdef 320 linear 00z01jan2000 1hr\n'
        'vars 4\nf 4 {dims} F\ng 4 {dims} G\nsurface 0 {surface} Surface\nrho 4 z Density\nendvars\n')
fixture_format=os.environ.get('GA_TEST_TIME_FORMAT','h5')
extension='h5' if fixture_format=='h5' else 'nc'
dtype='hdf5_grid' if extension=='h5' else 'netcdf'
regular='slice'+('_compressed' if fixture_format=='nc4' else '')+'.'+extension
ctls=[]
for template,reverse in ((False,False),(True,False),(False,True)):
    ctl=root/(('template' if template else 'regular')+('-reverse' if reverse else '')+'.ctl');ctls.append(ctl)
    ctl.write_text('dset ^'+('slice%tm1.'+extension if template else regular)+'\ndtype '+dtype+'\n'+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        header.format(dims='z,y,x' if template and extension=='h5' else 't,z,y,x',surface='y,x' if template and extension=='h5' else 't,y,x'))

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
def run(ctl,commands,n=4,off=False,missing=False,mem_kib=None):
    e=dict(env,GA_TIME_WORKERS='off' if off else 'auto')
    if mem_kib is not None:e.update(LD_PRELOAD=str(root/'slow.so'),GA_TEST_MEM_KIB=str(mem_kib))
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo'],
        input=f'open {ctl}\nset threads {n}\nset cachesf 2\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n',
        text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
        env=e,timeout=180)
    fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error' if missing else r'Error|ERROR|Time evaluation failed'
    assert p.returncode==0 and not re.search(fatal,p.stdout) and not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-3000:]
    for _,pids in groups(p.stdout):
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('worker survived quit',pid))
    return p.stdout

setup='set x 1 64\nset y 1 64\nset z 2\nset t 1\n'
expressions=[f'{f}(f*g,t=1,t=320)' for f in ('ave','mean','sum','sumg','min','max','minloc','maxloc')]
expressions += ['sum(sin(f)+pow(g,2),t=1,t=320)',
                'sum(maskout(f,(g>200000)-0.5),t=1,t=320)',
                'sum(maskout(f,-1),t=1,t=320)',
                'sum(const(f/(g-g),0),t=1,t=320)',
                'sum(qvsat((150+f/2000)*rho,100000*pow(rho,1004/287)),t=1,t=320)',
                'sum(f*g,t=1,t=320,3)',
                'sum(f*g,t=1.25,t=319.6,-b)',
                'mean(f*g,t=1.25,t=319.6,-b)',
                'sum(sum(f*g,z=1,z=4),t=1,t=320)',
                'sum(asum(f*g,x=1,x=16,y=1,y=12),t=1,t=320)']
setup='set x 1 16\nset y 1 12\nset z 2\nset t 1\n'
commands=setup+'\n'.join('d '+expr for expr in expressions)
for ctl in ctls:
    reference=parse(run(ctl,commands,1,True));assert len(reference)==len(expressions)
    for n in (4,8,16):
        text=run(ctl,commands,n);assert len(groups(text))>=len(expressions),'input times not dispatched'
        assert 'Time input expression:' in text,'whole expressions were not dispatched'
        compare(parse(text),reference)
    print('Time expression reduction values passed:',ctl.name,flush=True)

# Available-memory estimates include reclaimable cache, but must still bound
# concurrency and fall back safely when the reported available memory is low.
setup='set x 1 64\nset y 1 64\nset z 2\nset t 1\n'
reference=parse(run(ctls[0],setup+'d sum(f*g,t=1,t=320)',1,True))
limited=run(ctls[0],setup+'d sum(f*g,t=1,t=320)',16,mem_kib=1024*1024)
assert groups(limited) and all(1<int(n)<16 for n,_ in groups(limited))
compare(parse(limited),reference)
limited=run(ctls[0],setup+'d sum(f*g,t=1,t=320)',16,mem_kib=128*1024)
assert not groups(limited),'low-memory execution incorrectly started workers'
compare(parse(limited),reference)
print('Time expression reduction memory caps passed: bounded workers and serial fallback',flush=True)

# A 256-time batch followed by 14 times must reuse a larger existing pool.
text=run(ctls[0],setup+'d sum(f*g,t=1,t=272)\nd mean(f*g,t=1,t=272)',16)
g=groups(text);assert [int(n) for n,_ in g]==[16,14,16,14],g
assert g[0][1]==g[2][1] and g[1][1]==g[3][1]
assert set(g[1][1].split(','))<=set(g[0][1].split(','))
compare(parse(text),parse(run(ctls[0],setup+'d sum(f*g,t=1,t=272)\nd mean(f*g,t=1,t=272)',1,True)))

# DEFINE must also retain all Z planes when only its input times vary.
command='set x 1 16\nset y 1 16\nset z 1 4\nset t 1\ndefine answer=sum(f*g,t=1,t=320)\n'+\
        '\n'.join(f'set z {z}\nd answer' for z in range(1,5))
compare(parse(run(ctls[0],command)),parse(run(ctls[0],command,1,True)))

# Z is a varying input axis, rather than a loop collapsed by a profile worker.
command='set x 1\nset y 1\nset z 1 4\nset t 1\nd sum(f*g,t=1,t=320)'
text=run(ctls[0],command);assert 'axes=2,-1' in text,text[-1000:]
compare(parse(text),parse(run(ctls[0],command,1,True)))

# Global longitude retains ordinary cyclic seams and extra boundary cells.
cyclic=root/'cyclic.ctl'
cyclic.write_text(ctls[0].read_text().replace('xdef 64 linear 10 .1','xdef 64 linear -177.1875 5.625').replace('ydef 64 linear -60 .25','ydef 64 linear -44.296875 1.40625'))
command='set lon 0 360\nset lat -20 20\nset z 1\nset t 1\nd sum(f*g,t=1,t=320)\nd sum(f,t=1,t=320)\nd sum(f+g,t=1,t=320)'
text=run(cyclic,command);assert 'Time input expression:' in text
compare(parse(text),parse(run(cyclic,command,1,True)))

# Context-sensitive relative bounds and nested time reductions use the evaluator.
for expr in ('sum(f*g,z-1,z+1)','sum(f*g,t=1,t=4)'):
    command=setup+'d sum('+expr+',t=1,t=40)'
    text=run(ctls[0],command)
    assert 'Time input expression:' not in text,expr
    compare(parse(text),parse(run(ctls[0],command,1,True)))

# Missing time files keep the ordinary per-cell denominator and paired masks.
piece=root/('slice160.'+extension);saved=root/'slice160.saved';piece.rename(saved)
command=setup+'d sum(f*g,t=1,t=320)\nd sum(f+sin(g),t=1,t=320)'
compare(parse(run(ctls[1],command,missing=True)),parse(run(ctls[1],command,1,True,True)))
saved.rename(piece)

# Native integer masks and unpacking agree with the independent evaluator,
# including scalar and single-element template attributes.
for ctl in ctls[:2]:
    packed=root/(ctl.stem+'-packed.ctl')
    packed.write_text(ctl.read_text().split('vars 4')[0]+
        'unpack scale_factor add_offset\nvars 1\npacked 4 '+
        ('z,y,x' if ctl==ctls[1] and extension=='h5' else 't,z,y,x')+' Packed\nendvars\n')
    command=setup+'d sum(packed*2,t=1,t=320)\nd sum(packed+packed,t=1,t=320)'
    value=parse(run(packed,command));compare(value[:1],value[1:])
    compare(value,parse(run(packed,command,1,True)))

sub=root/'other';sub.mkdir();target=sub/'source.ctl';target.write_text(ctls[0].read_text())
linked=root/'linked.ctl';linked.symlink_to(target)
command=setup+'d sum(f*g,t=1,t=320)'
compare(parse(run(linked,command)),parse(run(linked,command,1,True)))

# Scalar definitions can accompany native fields; definition-only work stays local.
command=setup+'define f=7\nd sum(f*g,t=1,t=320)\nd mean(maskout(f,-1),t=1,t=320)'
text=run(ctls[0],command);assert groups(text) and 'Time defined inputs: count=1' in text
compare(parse(text),parse(run(ctls[0],command,1,True)))
command=setup+'d mean(f*g,t=0,t=321)\nd sum(f*g,t=1,t=7)'
compare(parse(run(ctls[0],command)),parse(run(ctls[0],command,1,True)))

# Parent rendering agrees for serial/worker DEFINE and direct XY DISPLAY.
images=[]
for n in (1,4):
    for defined in (False,True):
        path=root/f'sum-{n}-{defined}.png';images.append(path)
        expr='sum(sin(f/10000)*g,t=1,t=320)'
        command=setup+('define answer='+expr+'\n' if defined else '')
        command+='set timelab off\nset gxout shaded\nd '+('answer' if defined else expr)+'\nprintim '+str(path)+' x640 y480'
        run(ctls[0],command,n,off=n==1)
assert all(p.read_bytes()==images[0].read_bytes() for p in images),'SUM graphics differ'
print('Time expression reduction graphics passed: serial/workers DEFINE/DISPLAY PNGs identical',flush=True)

def terminal(cancel=False,fail=False,off=False,display=False):
    e=dict(env,GA_PROGRESS='off' if off else '.000001',GA_TIME_WORKERS='auto',
           LD_PRELOAD=str(root/'slow.so'),LANG='C',LC_ALL='C',LC_TERMINAL='iTerm2')
    e.pop('TMUX',None)
    pid,fd=pty.fork()
    if pid==0:os.execve(launcher,[launcher,'-bl','-d','gxdummy','-h','Cairo'],e)
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
        command('define inputscale=1.25')
        command('define answer=7')
        begin=len(output)
        os.write(fd,(('d ' if display else 'define answer=')+'sum(f*g*inputscale,t=1,t=320)\r').encode());end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.01);current=output[begin:].decode('utf-8','replace');g=groups(current)
            if g and not acted and (fail or (cancel and 'workers x 1 thread' in current)):
                os.kill(int(g[-1][1].split(',')[0]) if fail else pid,signal.SIGKILL if fail else signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('expression reduction did not return to prompt')
        if cancel or fail:assert acted
        if not cancel and not fail:
            assert ('Time input expression:' if display else 'Define memory allocation size') in current,current[-1000:]
        if fail:assert ('DISPLAY error' if display else 'DEFINE error') in current,current[-1000:]
        command('set threads 1')
        if cancel or fail:
            command('set gxout print');command('d answer')
            printed=parse(output.decode('utf-8','replace'));assert printed and all(v==7 for v in printed[-1]),printed[-1][:10]
        command('d sum(f*g,t=1,t=2)');command('quit');pump(.1);os.waitpid(pid,0)
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
    print('Time expression reduction terminal/recovery passed:',cancel,fail,off,flush=True)
for cancel,fail in ((False,False),(True,False),(False,True)):
    terminal(cancel=cancel,fail=fail,display=True)
    print('Time expression reduction DISPLAY terminal/recovery passed:',cancel,fail,flush=True)
print('Time expression reduction workers passed: samples=%d max_abs=%.17g; masks, order, weights, increments, pairs, templates, pool reuse, DEFINE and fallback agree.'%(samples,max_abs))
PY
