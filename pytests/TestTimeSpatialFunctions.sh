#!/usr/bin/env bash
# Independent-time coverage for the twenty additional grid built-ins.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-spatial.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
if [[ "${GA_TEST_TIME_FORMAT:-h5}" == bp5 ]]; then
  adios_config="${OPENGRADS_ADIOS2_ROOT:?BP5 tests need OPENGRADS_ADIOS2_ROOT}/bin/adios2-config"
  writer_define=-DZS_ADIOS
  read -r -a hflags <<< "$("$adios_config" --serial --c-flags)"
  read -r -a hlibs <<< "$("$adios_config" --serial --c-libs)"
  if grep -Eq '^#define ADIOS2_HAVE_BLOSC2($|[[:space:]]+1)' "$OPENGRADS_ADIOS2_ROOT/include/adios2/common/ADIOSConfig.h"; then
    hflags+=(-DZS_BLOSC)
  fi
else
  source "$repo_root/pytests/time_worker_format.sh"
fi
"${CC:-cc}" -std=gnu99 -O2 "$writer_define" -DNX=256 -DNY=256 -DNT=32 \
  "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,re,subprocess,sys,pty,select,signal,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
fmt=os.environ.get('GA_TEST_TIME_FORMAT','h5');ext='bp' if fmt=='bp5' else 'h5' if fmt=='h5' else 'nc'
dtype='bp5' if fmt=='bp5' else 'hdf5_grid' if ext=='h5' else 'netcdf';regular='slice'+('_compressed' if fmt=='nc4' else '')+'.'+ext
env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off',GA_TIME_TRACE='1')
ctls=[]
for template,reverse in (((False,False),(False,True)) if fmt=='bp5' else ((False,False),(True,False),(False,True))):
    pair=[]
    for pressure in (False,True):
        ctl=root/(('template' if template else 'regular')+('-reverse' if reverse else '')+('-pressure' if pressure else '')+'.ctl')
        dims='z,y,x' if fmt=='bp5' or template and ext=='h5' else 't,z,y,x';surface='y,x' if fmt=='bp5' or template and ext=='h5' else 't,y,x'
        ctl.write_text('dset ^'+('slice%tm1.'+ext if template else regular)+'\ndtype '+dtype+'\n'+
            ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
            'undef -9999\nxdef 256 linear 10 .1\nydef 256 linear -60 .25\nzdef 4 levels '+
            ('1000 850 500 100' if pressure else '50 150 400 1000')+'\ntdef 32 linear 00z01jan2000 1hr\n'+
            'vars 5\nf 4 '+dims+' Field\ng 4 '+dims+' Other\nkmap 0 '+surface+' Index\nsurface 0 '+surface+' Surface\nrho 4 z Density\nendvars\n')
        pair.append(ctl)
    ctls.append(pair)

if fmt=='bp5' and (root/'slicezstd.bp').exists():
    pair=[]
    for original in ctls[0]:
        ctl=original.with_name('compressed-'+original.name)
        ctl.write_text(original.read_text().replace('^slice.bp','^slicezstd.bp'))
        pair.append(ctl)
    ctls.append(pair)

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
    for ga,gb in zip(a,b):
        assert len(ga)==len(gb),(len(ga),len(gb))
        for x,y in zip(ga,gb):
            assert (x is None)==(y is None),('mask',x,y)
            if x is None:continue
            if not math.isfinite(x) or not math.isfinite(y):
                assert x==y or math.isnan(x) and math.isnan(y),(x,y)
                continue
            e=abs(x-y);max_abs=max(max_abs,e);samples+=1
            assert e<=1e-8+2e-12*abs(y),(x,y)
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
def run(ctl,body,n=4,error=False,opener="open"):
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo'],input=f'{opener} {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+body+'\nquit\n',universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(env,GA_TIME_WORKERS='off' if n==1 else 'auto'),timeout=180)
    bad=bool(re.search(r'Error|ERROR|Time evaluation failed|runtime error|AddressSanitizer',p.stdout))
    assert p.returncode==0 and bad==error,p.stdout[-6000:]
    for count,pids in groups(p.stdout):
        assert int(count)<=4
        for pid in map(int,pids.split(',')):
            try:os.kill(pid,0)
            except ProcessLookupError:continue
            raise AssertionError(('surviving worker',pid))
    return p.stdout
xy='set x 2 9\nset y 2 7\nset z 2\nset t 1 32\n'
point='set x 5\nset y 3\nset z 2\nset t 1 32\n'
zt='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
area='x=2,x=9,y=2,y=7'
plane=[('if','if(f>2000,f,g)'),('hdivg','hdivg(f/10000,g/10000)'),('hcurl','hcurl(f/10000,g/10000)'),
       ('zslice','zslice(f,kmap)'),('smth9','smth9(f)'),('cdiff','cdiff(f,x)')]
scalar=[(fn,fn+'(f,x=2,x=9)') for fn in ('min','max','minloc','maxloc')]+\
       [(fn,fn+'(f,'+area+')') for fn in ('asumg','amin','amax','aminlocx','aminlocy','amaxlocx','amaxlocy')]
pressure=[('vint','vint(1000+surface/10000,f/1000000,100)'),('tvrh2q','tvrh2q(250+f/10000,50+g/100000)'),('tvrh2t','tvrh2t(250+f/10000,50+g/100000)')]
assert len({name for name,expr in plane+scalar+pressure})==20
render=''.join('set t '+str(t)+'\nd answer\n' for t in (1,17,32))
for ctl,pctl in ctls:
    for source,setup,cases in ((ctl,xy,plane),(ctl,point,scalar),(pctl,xy,pressure)):
        command=''.join(setup+'define answer='+expr+'\n'+render for name,expr in cases)
        reference=grids(run(source,command,1))
        assert len(reference)==3*len(cases)
        for n in (2,4):
            text=run(source,command,n);assert len(groups(text))==len(cases),(source,n,cases,groups(text))
            compare(grids(text),reference)
    # New functions inside an input-time expression, including full X/Z CDIFF.
    for setup,expressions in ((xy.replace('set t 1 32','set t 1'),['smth9(f)','hdivg(f,g)','hcurl(f,g)','cdiff(f,xp)','cdiff(f,ym)','zslice(f,kmap)','if(f>2000,f,g)']),
                             ('set x 2 9\nset y 3\nset z 1 4\nset t 1\n',['cdiff(f,z)','cdiff(f,zp)','cdiff(f,zm)'])):
        command=setup+''.join('d sum('+expr+',t=1,t=32)\n' for expr in expressions)
        text=run(ctl,command);assert len(groups(text))==len(expressions),(expressions,groups(text))
        compare(grids(text),grids(run(ctl,command,1)))
    # T-dependent neighborhoods and reductions of a displayed axis retain the
    # original evaluator instead of evaluating separate scalar time points.
    for expr in ('smth9(f)','cdiff(f,t)','cdiff(f,tm)','cdiff(f,z)','min(f,z=1,z=4)','maxloc(f,z=1,z=4)','2*vint(surface,f,100)'):
        command=zt+'d '+expr+'\n';text=run(ctl,command)
        assert not groups(text),expr
        compare(grids(text),grids(run(ctl,command,1)))
    # Pointwise and fixed-axis extrema can use direct T DISPLAY safely.
    for source,expr in ((ctl,'if(f>2000,f,g)'),(pctl,'tvrh2q(250+f/10000,50+g/100000)'),(pctl,'tvrh2t(250+f/10000,50+g/100000)'),(ctl,'min(f,z=1,z=4)'),(ctl,'max(f,z=1,z=4)'),(ctl,'minloc(f,z=1,z=4)'),(ctl,'maxloc(f,z=1,z=4)')):
        text=run(source,point+'d '+expr+'\n');assert len(groups(text))==1,expr
        compare(grids(text),grids(run(source,point+'d '+expr+'\n',1)))
    # Missing and tied extrema retain masks and the native first-hit location.
    for fn in ('asumg','amin','amax','aminlocx','aminlocy','amaxlocx','amaxlocy'):
        for field in ('const(f,5)','maskout(f,-1)'):
            command=point+'define answer='+fn+'('+field+','+area+')\nd answer\n'
            text=run(ctl,command);assert groups(text)
            compare(grids(text),grids(run(ctl,command,1)))
    print('Time spatial functions values/axes/masks/ties passed:',fmt,ctl.name,flush=True)
# Fractional bounds and a cyclic seam retain native cell inclusion and ties.
cyclic=root/'cyclic.ctl'
cyclic.write_text(ctls[0][0].read_text().replace('xdef 256 linear 10 .1','xdef 256 linear 0 1.40625'))
expressions=[fn+'(f,lon=350,lon=380,lat=-59.875,lat=-58.375)' for fn in
             ('asumg','amin','amax','aminlocx','aminlocy','amaxlocx','amaxlocy')]
command=point+''.join('define answer='+expr+'\nd answer\n' for expr in expressions)
text=run(cyclic,command);assert len(groups(text))==len(expressions)
compare(grids(text),grids(run(cyclic,command,1)))
command=point+''.join('define answer='+fn+'(const(f,5),x=1.5,x=8.25)\nd answer\n'
                      for fn in ('min','max','minloc','maxloc'))
text=run(ctls[0][0],command);assert len(groups(text))==4
compare(grids(text),grids(run(ctls[0][0],command,1)))

# Invalid native DISPLAY environments do not become valid only at higher N.
for expr in ('hdivg(f,g)','hcurl(f,g)','zslice(f,kmap)','asumg(f,'+area+')','amin(f,'+area+')'):
    for n in (1,4):
        text=run(ctls[0][0],point+'d '+expr+'\n',n,error=True);assert not groups(text),expr
# Same worker PIDs serve multiple new functions; inner parallelism stays one.
text=run(ctls[0][0],point+'define answer=amin(f,'+area+')\ndefine answer=amax(f,'+area+')\n'+xy+'define answer=zslice(f,kmap)\n')
g=groups(text);assert len(g)==3 and g[0]==g[1]==g[2],g
if fmt=='bp5':
    dataset=root/'slice.bp'
    for command in ('set x 1\nset y 1\nset z 1 4\nset t 1\ndefine snapshot=rho\n'+point+'define answer=f*snapshot\nd answer\n',
                    point+'define answer=if(f>2000,f,g)\nd answer\nd if(f>2000,f,g)\n',
                    xy+'define answer=pow(f,2)+surface\n'+render,
                    xy.replace('set t 1 32','set t 1')+'d sum(if(f>2000,f,g),t=1,t=32)\n'):
        reference=grids(run(dataset,command,1,opener='bpopen'))
        for n in (2,4):
            text=run(dataset,command,n,opener='bpopen');assert groups(text)
            compare(grids(text),reference)
    text=run(dataset,point+'define answer=f\nd answer\nd f\n',opener='bpopen')
    assert not groups(text),'bare BP5 native field lost its batching path'
if fmt=='bp5':
    def assert_dead(text):
        for count,pids in groups(text):
            for pid in map(int,pids.split(',')):
                try:os.kill(pid,0)
                except ProcessLookupError:continue
                raise AssertionError(('worker survived',pid))
    heavy='('+ '+'.join(['sin(f)']*32)+')/32+pow(g,2)*0.000001+qvsat(150+f/200,100000)+esat(150+g/200)*0.0001+if(f>1000,1,0)'
    def terminal(cancel=False,fail=False,off=False,opener="open"):
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
            for s in (f'{opener} {ctls[0][0] if opener=="open" else root/"slice.bp"}','set threads 4','set x 1 256','set y 1 256','set z 1','set t 1 32'):command(s)
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
            if cancel or fail:assert grids(text)[0]==[7.0],'failed replacement changed old DEFINE'
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
    for opener in ('open','bpopen'):
        for cancel,fail,off in ((False,False,False),(True,False,False),(False,True,False),(False,False,True)):
            terminal(cancel,fail,off,opener)
            print('BP5 terminal/recovery passed:',opener,cancel,fail,off,flush=True)
print(f'Time spatial functions passed: {fmt}; 20 names; samples={samples} max_abs={max_abs:.17g}; DEFINE/input-time/direct DISPLAY, native fallback, pool reuse and cleanup; <=4 cores.')
PY
