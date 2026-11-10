#!/usr/bin/env bash
# Cross-file expressions retain native file, coordinate and time semantics.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-files.XXXXXX")"
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
python3 -u - "$repo_root" "$test_root" <<'PYTEST'
import math,os,re,subprocess,sys,struct,pty,select,signal,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
reference=os.environ.get('GA_TEST_REFERENCE_LAUNCHER',launcher)
fmt=os.environ.get('GA_TEST_TIME_FORMAT','h5');ext='bp' if fmt=='bp5' else 'h5' if fmt=='h5' else 'nc'
dtype='bp5' if fmt=='bp5' else 'hdf5_grid' if ext=='h5' else 'netcdf'
regular='slice'+('_compressed' if fmt=='nc4' else '')+'.'+ext
env=dict(os.environ,GA_TIME_TRACE='1',GA_PROGRESS='off',OPENGRADS_COLOR='0')
ctls={}
for pressure in (False,True):
 for reverse in (False,True):
  ctl=root/('p' if pressure else 'z');ctl=ctl.with_name(ctl.name+('-reverse' if reverse else '')+'.ctl')
  dims='z,y,x' if fmt=='bp5' else 't,z,y,x';surface='y,x' if fmt=='bp5' else 't,y,x'
  ctl.write_text('dset ^'+regular+'\ndtype '+dtype+'\n'+('options yrev zrev\n' if reverse else '')+
   'undef -9999\nxdef 256 linear 10 .1\nydef 256 linear -60 .25\nzdef 4 levels '+
   ('1000 850 500 100' if pressure else '50 150 400 1000')+'\ntdef 32 linear 00z01jan2000 1hr\n'+
   'vars 5\nf 4 '+dims+' Field\ng 4 '+dims+' Other\nkmap 0 '+surface+' Index\nsurface 0 '+surface+' Surface\nrho 4 z Density\nendvars\n')
  ctls[pressure,reverse]=ctl

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
 for xg,yg in zip(a,b):
  assert len(xg)==len(yg)
  for x,y in zip(xg,yg):
   assert (x is None)==(y is None),('mask',x,y)
   if x is None:continue
   if not math.isfinite(x) or not math.isfinite(y):
    assert x==y or math.isnan(x) and math.isnan(y),(x,y);continue
   e=abs(x-y);max_abs=max(max_abs,e);samples+=1
   assert e<=1e-8+2e-12*abs(y),(x,y)
def groups(text):return re.findall(r'Time workers: workers=(\d+) inner=1 .*?pids=([0-9,]+)',text)
def run(body,n=4,pressure=False,expect_error=False,extra=''):
 ctl=ctls[pressure,False];other=ctls[pressure,True]
 commands=f'open {other}\nopen {other}\nopen {ctl}\nopen {other}\nset dfile 3\n'+extra+f'\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+body+'\nquit\n'
 p=subprocess.run([reference if n==1 else launcher,'-blu','-d','gxdummy','-h','Cairo'],input=commands,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(env,GA_TIME_WORKERS='off' if n==1 else 'auto'),timeout=180)
 bad=bool(re.search(r'Error|ERROR|Time evaluation failed|runtime error|AddressSanitizer',p.stdout))
 assert p.returncode==0 and bad==expect_error,p.stdout[-6000:]
 for count,pids in groups(p.stdout):
  assert int(count)<=n
  for pid in map(int,pids.split(',')):
   try:os.kill(pid,0)
   except ProcessLookupError:continue
   raise AssertionError(('surviving worker',pid))
 return p.stdout
xy='set x 2 9\nset y 2 7\nset z 2\nset t 1 32\n'
point='set x 5\nset y 3\nset z 2\nset t 1 32\n'
zt='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
area='x=2,x=9,y=2,y=7'
maths=['abs(-f)','sqrt(abs(f))','exp(f/10000)','log(abs(f))','log10(abs(f))','sin(f/10000)','cos(f/10000)','tan(f/10000)','asin(sin(f/10000))','acos(cos(f/10000))','atan2(f,g)','pow(f,2)','mag(f,g)','const(f,0)','maskout(f,g-2000)','esat(150+f/200)','qvsat(150+f/200,100000)','zint(f*rho)','roll(f,-1,x)']
planes=['if(f>2000,f,g)','hdivg(f/10000,g/10000)','hcurl(f/10000,g/10000)','zslice(f,kmap)','smth9(f)','cdiff(f,x)']
reductions=[fn+'(f,x=2,x=9)' for fn in ('ave','mean','sum','sumg','min','max','minloc','maxloc')]+[fn+'(f,'+area+')' for fn in ('aave','amean','asum','atot','asumg','amin','amax','aminlocx','aminlocy','amaxlocx','amaxlocy')]
pressure=['vint(1000+surface/10000,f/1000000,100)','tvrh2q(250+f/10000,50+g/100000)','tvrh2t(250+f/10000,50+g/100000)']
assert len(maths+planes+reductions+pressure)==47
# Make every expression use two distinct source descriptors; the second
# reverses stored Y/Z, so wrong ordinal mappings cannot accidentally agree.
def qualify(expr):
 expr=re.sub(r'\b(f|g|rho|surface|kmap)\b',lambda m:m[0]+('.3' if m[0]=='f' else '.4'),expr)
 return '('+expr+')+0*g.4'
for ispressure,setup,cases in ((False,xy,maths+planes),(False,point,reductions),(True,xy,pressure)):
 body=''.join(setup+'define answer='+qualify(expr)+'\n'+''.join('set t '+str(t)+'\nd answer\n' for t in (1,17,32)) for expr in cases)
 expected=grids(run(body,1,ispressure));assert len(expected)==len(cases)*3
 for n in (4,8):
  text=run(body,n,ispressure);assert len(groups(text))==len(cases),(cases,groups(text),text[-3000:])
  assert text.count('Time file inputs: count=2 default=3')==len(cases)
  compare(grids(text),expected)
 print('All built-in cross-file DEFINE group passed:',fmt,ispressure,len(cases),flush=True)
# Input-time workers, direct T display, changed default and native field .N
# must bypass a DEFINE with the same name. Reuse the same pool across all.
body=zt+'define g=f.3*0+7\nd f.3+g.4\nset dfile 4\nd f+g.3\nset dfile 3\nd f+g\n'
body+=xy.replace('set t 1 32','set t 1')+''.join('d sum('+qualify(e)+',t=1,t=32)\n' for e in ('f*g','smth9(f)','cdiff(f,x)','zint(f*rho)','if(f>2000,f,g)'))
expected=grids(run(body,1))
for n in (4,8):
 text=run(body,n);assert len(groups(text))==8,(groups(text),text[-3000:]);compare(grids(text),expected)
# Close/reopen changes source metadata and invalidates the old pool.
body=zt+'define answer=f.3+g.4\nd answer\nclose 4\nopen '+str(ctls[False,False])+'\n'+zt+'define answer=f.3+g.4\nd answer\n'
expected=grids(run(body,1));text=run(body,4)
assert len(groups(text))==2 and groups(text)[0][1]!=groups(text)[1][1]
compare(grids(text),expected)
# A serial profile fallback must keep the parent's original file ordinals.
for last in (31,32):
    area='aave(f.3+g.4,x=2,x=9,y=2,y=7)'
    body=point.replace('1 32','1 '+str(last))+'define expected='+area+'\nd expected\nd '+area+'\n'
    for n in (1,4,8):
        values=grids(run(body,n));assert len(values)==2;compare(values[:1],values[1:])
# Equal paths are separate descriptors; clocks map through world time.
shift=root/'shift.ctl';shift.write_text(ctls[False,False].read_text().replace('00z01jan2000','01z01jan2000'))
body=zt+'define answer=f.3+g.5\nd answer\nd f.3+g.5\n'
for n in (4,8):
 text=run(body,n,extra='open '+str(shift));assert len(groups(text))==2
 compare(grids(text),grids(run(body,1,extra='open '+str(shift))))
# Native binary T=1 profile keeps missing times; it is never silently frozen.
bar=root/'bar.ctl';bar.write_text('dset ^bar.dat\nundef -9999\nxdef 1 linear 10 .1\nydef 1 linear -60 .25\nzdef 4 levels 50 150 400 1000\ntdef 1 linear 00z01jan2000 1hr\nvars 1\nrho 4 99 Density\nendvars\n')
(root/'bar.dat').write_bytes(struct.pack('=4f',1.2,1.1,1.,.9))
body=zt+'define answer=f.3*rho.5\nd answer\n'
text=run(body,4,extra='open '+str(bar));assert groups(text)
compare(grids(text),grids(run(body,1,extra='open '+str(bar))))
# Three sources, coordinate fields and both directions of ordinal remapping.
body=zt+'define answer=f.1+g.4+surface.5+0*lev.4\nd answer\n'
extra='open '+str(shift)
text=run(body,4,extra=extra);assert 'Time file inputs: count=4 default=3' in text
compare(grids(text),grids(run(body,1,extra=extra)))
# Each restart method keeps its own opener rather than inheriting the default.
if fmt in ('nc','nc4'):
 xdf=root/'input.xdf';xdf.write_text('dset ^'+regular+'\n')
 extra='sdfopen '+str(root/regular)+'\nxdfopen '+str(xdf)
 body=zt+'define answer=f.3+g.5+f.6\nd answer\n'
 text=run(body,4,extra=extra);assert 'Time file inputs: count=3' in text
 compare(grids(text),grids(run(body,1,extra=extra)))
if fmt=='bp5':
 extra='bpopen '+str(root/regular)
 # No coordinates are stored by this fixture; discovery uses index axes.
 # First retain the native rejection of incompatible world coordinates.
 body=zt+'define answer=f.3+g.5\n'
 text=run(body,4,extra=extra,expect_error=True);assert not groups(text)
 run(body,1,extra=extra,expect_error=True)
 # Then compare matching discovered and explicit descriptors.
 auto=root/'auto.ctl';auto.write_text(ctls[False,False].read_text().replace('10 .1','1 1').replace('-60 .25','1 1').replace('50 150 400 1000','1 2 3 4').replace('1hr','1mn'))
 extra+='\nopen '+str(auto)+'\nset dfile 6'
 body=zt+'define answer=f.6+g.5\nd answer\n'
 text=run(body,4,extra=extra);assert 'Time file inputs: count=2' in text
 compare(grids(text),grids(run(body,1,extra=extra)))
# Bound native inputs without opening every unrelated session file.
extra='\n'.join('open '+str(ctls[False,False]) for _ in range(30))
body=zt+'define answer='+'+'.join('f.'+str(i) for i in range(1,35))+'\nd answer\n'
text=run(body,4,extra=extra);assert not groups(text)
compare(grids(text),grids(run(body,1,extra=extra)))
# Existing dimension-qualified and unsupported operations remain local.
for expr in ('f.3(t=2)+g.4','skip(f.3,2)+g.4'):
 body=zt+'define answer='+expr+'\nd answer\n';text=run(body,4)
 assert not groups(text),expr;compare(grids(text),grids(run(body,1)))
# Invalid ordinals stay native errors and never spawn workers.
for expr in ('f.0+g','f.99+g','f.21474836470+g'):
 text=run(zt+'define answer='+expr,4,expect_error=True);assert not groups(text)
heavy='('+ '+'.join(['sin(f.3)']*32)+')/32+pow(g.4,2)*0.000001+qvsat(150+f.3/200,100000)+esat(150+g.4/200)*0.0001+if(f.3>1000,1,0)'
def terminal(cancel=False,fail=False,off=False,startup=False):
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
        for s in (f'open {ctls[False,False]}',f'open {ctls[False,True]}',f'open {ctls[False,False]}',f'open {ctls[False,True]}','set dfile 3','set threads 4','set x 1 256','set y 1 256','set z 1','set t 1 32'):command(s)
        expression=heavy
        if startup:
            gone=root/'gone.ctl';gone.write_text(ctls[False,True].read_text())
            command('open '+str(gone));gone.unlink()
            expression=heavy.replace('g.4','g.5')
        command('define answer=7');begin=len(output);os.write(fd,('define answer='+expression+'\r').encode());end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.02);current=output[begin:].decode('utf-8','replace')
            g=groups(current)
            if g and not acted and (fail or (cancel and 'workers x 1 thread' in current)):
                os.kill(int(g[-1][1].split(',')[0]) if fail else pid,signal.SIGKILL if fail else signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('worker command did not return to prompt')
        if cancel or fail:assert acted,'test did not reach active workers'
        if not cancel and not fail and not startup:assert 'Define memory allocation size' in current,current[-1000:]
        if fail or startup:assert 'DEFINE error' in current,current[-1000:]
        command('set threads 1');command('set t 1');command('set z 1');command('set x 5');command('set y 3');command('set gxout print');command('d answer');command('d f+g')
        if startup:
            # Restore the descriptor and retry in the same parent after its
            # failed pool was torn down. No stale partial-open handles survive.
            gone.write_text(ctls[False,True].read_text())
            command('set threads 4');command('set x 6');command('set t 1 32');command('define recovered=f.3+g.5')
            command('set t 1');command('d recovered')
        command('quit');pump(.1);os.waitpid(pid,0)
        text=output.decode('utf-8','replace')
        for _,pids in groups(text):
            for worker_pid in map(int,pids.split(',')):
                try:os.kill(worker_pid,0)
                except ProcessLookupError:continue
                raise AssertionError(('worker survived',worker_pid))
        if startup:
            assert len(groups(text))==2,'restored descriptor did not restart workers'
            assert 'Time evaluation failed' not in text[text.index('define recovered='):]
            assert len(grids(text))==3 and grids(text)[-1][0] is not None,(grids(text),text[-5000:])
        if cancel or fail or startup:assert grids(text)[0]==[7.0],'failed replacement changed old DEFINE'
        frames=re.findall(r'\r([^\r\n]*?)\x1b\[K',text)
        worker=[f for f in frames if 'workers x 1 thread' in f]
        if off:assert not worker and '\x1b]9;4;' not in text
        elif not fail and not startup:
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
for cancel,fail,startup in ((False,False,False),(True,False,False),(False,True,False),(False,False,True)):
 terminal(cancel,fail,startup=startup)
 print('Cross-file progress/recovery PASS:',fmt,cancel,fail,startup,flush=True)
print('Cross-file functions PASS:',fmt,'samples=',samples,'max_abs=',max_abs,flush=True)
PYTEST
