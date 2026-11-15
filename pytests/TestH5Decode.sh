#!/usr/bin/env bash
# Parallel native HDF5 decoding, boundaries, filter masks and failure recovery.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if ! pkg-config --exists hdf5; then printf 'SKIP: HDF5 development files unavailable\n';exit 0;fi
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-h5decode.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
"${CC:-cc}" -std=gnu99 -O2 -Wall -Wextra -Werror "${hflags[@]}" \
  "$repo_root/pytests/h5decode_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
if "$test_root/writer" "$test_root"; then :; else
  status=$?;if [[ $status == 77 ]]; then exit 0;fi;exit "$status"
fi
cat > "$test_root/slow.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <hdf5.h>
#if H5_VERSION_GE(2,0,0)
herr_t H5Dread_chunk2(hid_t a,hid_t b,const hsize_t *c,uint32_t *d,void *e,size_t *f) {
  static herr_t (*next)(hid_t,hid_t,const hsize_t *,uint32_t *,void *,size_t *);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"H5Dread_chunk2");
  usleep(20000);return next(a,b,c,d,e,f);
}
#endif
C
"${CC:-cc}" -shared -fPIC -O2 "${hflags[@]}" "$test_root/slow.c" -ldl -o "$test_root/slow.so"
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,pty,re,select,signal,subprocess,sys,time
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off',GA_ZINT_TRACE='1')
launcher=str(repo/'opengrads')
config=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input='q config\nquit\n',universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=30).stdout
version=re.search(r'hdf5-(\d+)\.',config)
if not version or int(version[1])<2 or not re.search(r'^Config:.*\bopenmp\b',config,re.M):
    print('SKIP: parallel raw decoding needs HDF5 2 and OpenMP');sys.exit(0)
def parse(text):
    lines=iter(text.splitlines());out=[]
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n=int(m[1]);fill=float(m[2]);values=[]
        while len(values)<n:values.extend(map(float,next(lines).split()))
        assert len(values)==n;out.append([None if v==fill else v for v in values])
    return out
samples=0;max_abs=0.
def compare(a,b):
    global samples,max_abs
    assert len(a)==len(b),(len(a),len(b))
    for x,y in zip(a,b):
        assert len(x)==len(y)
        for u,v in zip(x,y):
            assert (u is None)==(v is None),'missing masks differ'
            if u is None:continue
            if not math.isfinite(u) or not math.isfinite(v):assert u==v or (math.isnan(u) and math.isnan(v));continue
            error=abs(u-v);max_abs=max(max_abs,error);samples+=1
            assert error<=1e-8+abs(v)*2e-12,(u,v)
def run(ctl,commands,n=4,off=False,error=False,graphics=False):
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo' if graphics else 'gxdummy'],
        input=f'open {ctl}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\nset z 1\nset t 1\n'+commands+'\nquit\n',
        universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(env,GA_H5_DECODE='off' if off else 'auto'),timeout=180)
    assert p.returncode==0,p.stdout[-2000:]
    bad=bool(re.search(r'Error|ERROR|failed',p.stdout))
    assert bad==error,p.stdout[-2000:]
    codes=[int(v) for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)]
    assert any(v>0 for v in codes) if error else not any(v>0 for v in codes),p.stdout[-2000:]
    return p.stdout,parse(p.stdout)
variants=['float','double','rank4','permuted','bigendian','integer','packed','sparse','skipped','extra','partial','legacy','contiguous','uncompressed']
eligible={'float','double','rank4','packed','skipped','legacy'};ctls={}
for name in variants+['corrupt','badzip']:
  for reverse in (False,True):
    ctl=root/(name+('-reverse' if reverse else '')+'.ctl');ctls[name,reverse]=ctl
    dims='t,z,y,x' if name=='rank4' else 'y,x,z' if name=='permuted' else 'z,y,x'
    ctl.write_text(f'dset ^{name}.h5\ndtype hdf5_grid\n'+('options yrev zrev\n' if reverse else '')+
       ('unpack scale_factor add_offset\n' if name=='packed' else '')+
       'undef -9999\nxdef 258 linear 10 .1\nydef 130 linear -60 .25\n'+
       'zdef 8 levels 50 150 400 1000 1600 2500 4000 6000\ntdef 2 linear 00z01jan2000 1hr\n'+
       f'vars 2\nf 8 {dims} Field\nrho 8 z Density\nendvars\n')
    if name in ('corrupt','badzip'):continue
    command=('set t 2\n' if name=='rank4' else '')+\
       'd zint(f)\nd zint(f,125.25,5378.75)\nd zint(f*rho)\n'+\
       'set x 17 241\nset y 9 119\nd zint(f,125.25,5378.75)\n'+\
       'set x 0 259\nset y 0 131\nd zint(f)\n'
    _,reference=run(ctl,command,1,True)
    for n in (2,4):
        text,actual=run(ctl,command,n);compare(actual,reference)
        assert ('HDF5 parallel chunks=' in text)==(name in eligible),(name,n,text[-1000:])
    print('HDF5 decode passed:',ctl.name,flush=True)

# Explicit fallback, a single varying dimension, and byte-identical graphics.
ctl=ctls['float',False]
text,a=run(ctl,'d zint(f)',4,True);assert 'HDF5 parallel chunks=' not in text
compare(a,run(ctl,'d zint(f)',1,True)[1])
text,a=run(ctl,'set y 3\nd zint(f)',4);assert 'HDF5 parallel chunks=' not in text
compare(a,run(ctl,'set y 3\nd zint(f)',1,True)[1])
image=root/'parallel.png';reference=root/'native.png'
run(ctl,'set gxout shaded\nd zint(f)\nprintim '+str(image)+' x640 y480',4,graphics=True)
run(ctl,'set gxout shaded\nd zint(f)\nprintim '+str(reference)+' x640 y480',1,True,graphics=True)
assert image.read_bytes()==reference.read_bytes(),'decoded graphic differs'

# A failed checksum must discard the replacement DEFINE and preserve recovery.
for name in ('corrupt','badzip'):
  for n in (1,4):
    text,value=run(ctls[name,False],'define answer=7\ndefine answer=zint(f)\nset x 1\nset y 1\nd answer\nd rho',n,error=True)
    assert value==[[7.0],[0.5]],value
    if n>1:assert 'chunk decode/checksum failed' in text

def terminal(cancel=False,off=False):
    e=dict(env,GA_PROGRESS='off' if off else '.000001',GA_H5_DECODE='auto',LD_PRELOAD=str(root/'slow.so'),
           LC_TERMINAL='iTerm2',LANG='C',LC_ALL='C');e.pop('TMUX',None)
    pid,fd=pty.fork()
    if pid==0:os.execve(launcher,[launcher,'-bl','-d','gxdummy','-h','gxdummy'],e)
    output=bytearray()
    def pump(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if select.select([fd],[],[],.01)[0]:
                try:output.extend(os.read(fd,65536))
                except OSError:return
    def cmd(s):
        start=len(output);os.write(fd,(s+'\r').encode());end=time.monotonic()+30
        while time.monotonic()<end:
            pump(.02)
            if b'ga->' in output[start+len(s):] or (s=='quit' and b'GX Package Terminated' in output[start:]):return
        raise AssertionError(('prompt timeout',output[-1500:]))
    try:
        pump(.3)
        for s in (f'open {ctl}','set threads 4','set z 1','set t 1','define answer=7'):cmd(s)
        start=len(output);os.write(fd,b'define answer=zint(f)\r');end=time.monotonic()+30;acted=False
        while time.monotonic()<end:
            pump(.01);current=output[start:].decode('utf-8','replace')
            if cancel and not acted and re.search(r'zint.*\s[1-9]\d*%',current):os.kill(pid,signal.SIGINT);acted=True
            if 'ga->' in current:break
        else:raise AssertionError('decoded command did not return')
        if cancel:assert acted
        else:assert 'Define memory allocation size' in current,current[-1000:]
        cmd('set threads 1');cmd('set x 1');cmd('set y 1');cmd('set gxout print');cmd('d answer');cmd('quit');pump(.1);os.waitpid(pid,0)
        text=output.decode('utf-8','replace')
        if cancel:assert parse(text)==[[7.0]],text[-2000:]
        frames=re.findall(r'\r([^\r\n]*?)\x1b\[K',text);decoded=[f for f in frames if 'zint' in f]
        if off:assert not decoded and '\x1b]9;4;' not in text
        else:
            assert decoded,'decoded progress absent'
            pct=[int(re.search(r'(\d+)%',f)[1]) for f in decoded];assert pct==sorted(pct),pct
            assert '\x1b]9;4;0\x07' in text,'iTerm progress not cleared'
    finally:
        try:os.kill(pid,signal.SIGKILL)
        except ProcessLookupError:pass
        try:os.waitpid(pid,0)
        except ChildProcessError:pass
        os.close(fd)
for cancel,off in ((False,False),(True,False),(False,True)):
    terminal(cancel,off);print('HDF5 decode terminal passed:',cancel,off,flush=True)
print('HDF5 decode regression passed: samples=%d max_abs=%.17g; masks, filter masks, checksum failure, ordering, fallback, graphics and cancellation agree.'%(samples,max_abs))
PY
