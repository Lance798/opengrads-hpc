#!/usr/bin/env bash
# ROLL output/input-time workers preserve selected spatial domains and masks.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-roll.XXXXXX")"
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
reference=launcher
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
def run(body,n=4,pressure=False,expect_error=False,extra='',missing=False):
 ctl=ctls[pressure,False];other=ctls[pressure,True]
 commands=f'open {other}\nopen {other}\nopen {ctl}\nopen {other}\nset dfile 3\n'+extra+f'\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+body+'\nquit\n'
 p=subprocess.run([reference if n==1 else launcher,'-blu','-d','gxdummy','-h','Cairo'],input=commands,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(env,GA_TIME_WORKERS='off' if n==1 else 'auto'),timeout=180)
 fatal=r'DEFINE error|DISPLAY error|Time evaluation failed|Syntax Error|Operation Error|runtime error|AddressSanitizer' if missing else r'Error|ERROR|Time evaluation failed|runtime error|AddressSanitizer'
 bad=bool(re.search(fatal,p.stdout))
 assert p.returncode==0 and bad==expect_error,p.stdout[-6000:]
 for count,pids in groups(p.stdout):
  assert int(count)<=n
  for pid in map(int,pids.split(',')):
   try:os.kill(pid,0)
   except ProcessLookupError:continue
   raise AssertionError(('surviving worker',pid))
 return p.stdout
xy='set x 2 9\nset y 2 7\nset z 2\nset t 1 32\n'
zt='set x 5\nset y 3\nset z 1 4\nset t 1 32\n'
point='set x 5\nset y 3\nset z 2\nset t 1 32\n'
expressions=['roll(f,1,x)','roll(f,-1,y)','roll(roll(f,-10,x),8,y)',
             'roll(f*g,-9223372036854775808,x)',
             'roll(f,123456789012345678901234567890,y)',
             'roll(f.3+g.4,-3,x)','roll(rho.4+f.3,1,y)']
render=''.join('set t '+str(t)+'\nd answer\n' for t in (1,17,32))
body=''.join(xy+'define answer='+expr+'\n'+render for expr in expressions)
reference_values=grids(run(body,1))
for n in (2,4):
    text=run(body,n);assert len(groups(text))==len(expressions),text[-4000:]
    assert 'Time file inputs: count=2 default=3' in text
    compare(grids(text),reference_values)
    assert len({pids for count,pids in groups(text)})==1,'pool was not reused'
# DEFINE spans output Z/T while each worker owns a complete XY plane.
body=xy.replace('set z 2','set z 1 4')+'define answer=roll(roll(f,-3,x),2,y)\n'
body+=''.join(f'set z {z}\nset t {t}\nd answer\n' for z in (1,2,4) for t in (1,17,32))
for n in (2,4):
    text=run(body,n);assert groups(text);compare(grids(text),grids(run(body,1)))
# Input-time workers retain XY/XZ/YZ grids and ordered native folds.
for setup,exprs in ((xy.replace('set t 1 32','set t 1'),
                    ['roll(f,-1,x)*g','roll(f.3+g.4,2,y)','roll(roll(f,3,x),-2,y)']),
                   ('set x 2 9\nset y 3\nset z 1 4\nset t 1\n',
                    ['roll(f,-1,x)','roll(f,3,y)']),
                   ('set x 5\nset y 2 7\nset z 1 4\nset t 1\n',
                    ['roll(f,-2,y)','roll(f,3,x)'])):
    body=setup+''.join('d '+fn+'('+expr+',t=1,t=32)\n' for expr in exprs for fn in ('sum','mean','min','max'))
    expected=grids(run(body,1))
    for n in (2,4):
        text=run(body,n);assert len(groups(text))==len(exprs)*4,(exprs,groups(text),text[-4000:])
        assert 'Time input expression:' in text
        compare(grids(text),expected)
# Native T/ZT views have singleton X/Y and retain identity. Outer spatial
# reductions establish their own selected XY grid before rolling.
body=zt+'d roll(f,1,x)\nd roll(f,-1,y)\n'+point+\
    'd aave(roll(f.3+g.4,-1,x),x=2,x=9,y=2,y=7)\n'
for n in (2,4):
    text=run(body,n);assert len(groups(text))==3,groups(text)
    compare(grids(text),grids(run(body,1)))
# Parent fallback must retain original .N numbers when workers are disabled
# or a short series cannot start a pool. Compare DISPLAY to per-time DEFINE.
area='aave(roll(f.3+g.4,-1,x),x=2,x=9,y=2,y=7)'
for last in (31,32):
    body=point.replace('1 32','1 '+str(last))+'define expected='+area+'\nd expected\nd '+area+'\n'
    for n in (1,4):
        values=grids(run(body,n));assert len(values)==2;compare(values[:1],values[1:])
# X/T and Y/T DISPLAY keep native grids; arithmetic metadata and values agree.
for setup,expr in (('set x 2 9\nset y 3\nset z 2\nset t 1 32\n','roll(f,-1,x)'),
                   ('set x 5\nset y 2 7\nset z 2\nset t 1 32\n','roll(f,2,y)')):
    for n in (2,4):
        compare(grids(run(setup+'d '+expr,n)),grids(run(setup+'d '+expr,1)))
# Static defined fields are fresh snapshots, with no mutation by a roll.
body='set x 2 9\nset y 2 7\nset z 2\nset t 1\ndefine terrain=g.4\n'+xy+\
     'define answer=roll(f+terrain,1,x)\n'+render+\
     'set t 1\nd terrain\nset t 1 32\ndefine terrain=terrain*0+7\n'+\
     'define answer=roll(f+terrain,-1,y)\n'+render
for n in (2,4):
    text=run(body,n);assert 'Time defined inputs: count=1' in text
    compare(grids(text),grids(run(body,1)))
for bad in ('roll(f,1,t)','roll(f,1,z)','roll(f,1.5,x)'):
    text=run(xy+'define answer='+bad,4,expect_error=True);assert not groups(text)
# Template sources and a missing step retain native per-time masks.
if fmt!='bp5':
    original_ctl=ctls[False,False]
    template=root/'template.ctl'
    description=original_ctl.read_text().replace('^'+regular,'^slice%tm1.'+ext).replace('undef -9999','options template\nundef -9999')
    if fmt=='h5':description=description.replace('t,z,y,x','z,y,x').replace('t,y,x','y,x')
    template.write_text(description);ctls[False,False]=template
    body=xy+'define answer=roll(f.3+g.4,-1,x)\n'+''.join('set t '+str(t)+'\nd answer\n' for t in (1,16,17,32))
    compare(grids(run(body,4)),grids(run(body,1)))
    piece=root/('slice16.'+ext);saved=piece.with_suffix('.saved')
    assert piece.exists(),sorted(p.name for p in root.iterdir())
    piece.rename(saved)
    try:
        body+=xy.replace('set t 1 32','set t 1')+'d sum(roll(f,-1,y),t=1,t=32)\n'
        compare(grids(run(body,4,missing=True)),grids(run(body,1,missing=True)))
    finally:saved.rename(piece);ctls[False,False]=original_ctl
print('ROLL worker values, cross-file sources, snapshots, output/input/display shapes passed:',fmt,flush=True)
heavy='('+ '+'.join(['sin(roll(f.3,-1,x))']*32)+')/32+pow(roll(g.4,1,y),2)*0.000001+qvsat(150+f.3/200,100000)+esat(150+g.4/200)*0.0001+if(f.3>1000,1,0)'
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
    print('ROLL worker progress/recovery passed:',fmt,cancel,fail,startup,flush=True)
print('ROLL time workers PASS:',fmt,'samples=',samples,'max_abs=',max_abs)
PYTEST
