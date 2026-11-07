#!/usr/bin/env bash
# Same-field threshold masks: exact boundaries, native formats and fallback.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-mask-threshold.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
flags=() libs=() hflags=()
if pkg-config --exists hdf5; then
  read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
  read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
  flags+=(-DZS_HDF5 "${hflags[@]}"); libs+=("${hlibs[@]}")
fi
if command -v nc-config >/dev/null 2>&1; then
  read -r -a nflags <<< "$(nc-config --cflags)";read -r -a nlibs <<< "$(nc-config --libs)"
  flags+=(-DZS_NETCDF "${nflags[@]}");libs+=("${nlibs[@]}")
fi
adios_root="${OPENGRADS_ADIOS2_ROOT:-}"
if [[ -x "$adios_root/bin/adios2-config" ]]; then
  read -r -a aflags <<< "$("$adios_root/bin/adios2-config" --serial --c-flags)"
  read -r -a alibs <<< "$("$adios_root/bin/adios2-config" --serial --c-libs)"
  flags+=(-DZS_ADIOS "${aflags[@]}");libs+=("${alibs[@]}")
fi
"${CC:-cc}" -std=gnu99 -O2 -DNX=64 -DNY=64 -DNT=4 "${flags[@]}" \
  "$repo_root/pytests/zslice_writer.c" "${libs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
if pkg-config --exists hdf5; then
  cat > "$test_root/probe.c" <<'C'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <hdf5.h>
static unsigned long calls;
herr_t H5Dread(hid_t a,hid_t b,hid_t c,hid_t d,hid_t e,void *f) {
  static herr_t (*next)(hid_t,hid_t,hid_t,hid_t,hid_t,void *);
  if(!next)*(void **)(&next)=dlsym(RTLD_NEXT,"H5Dread");
  calls++;return next(a,b,c,d,e,f);
}
__attribute__((destructor)) static void report(void) { if(calls)fprintf(stderr,"H5_READS %lu\n",calls); }
C
  "${CC:-cc}" -shared -fPIC -O2 "${hflags[@]}" "$test_root/probe.c" -ldl -o "$test_root/probe.so"
fi
python3 -u - "$repo_root" "$test_root" <<'PY'
import math,os,re,subprocess,sys
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off',GA_MASK_TRACE='1')
def parse(text):
    lines=iter(text.splitlines());out=[]
    for line in lines:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n=int(m[1]);fill=float(m[2]);v=[]
        while len(v)<n:v.extend(map(float,next(lines).split()))
        assert len(v)==n;out.append([None if x==fill else x for x in v])
    return out
def run(ctl,commands,n=1,probe=False,error=False,graphics=False):
    e=dict(env)
    if probe:e['LD_PRELOAD']=str(root/'probe.so')
    p=subprocess.run([str(repo/'opengrads'),'-blu','-d','gxdummy','-h','Cairo' if graphics else 'gxdummy'],
        input=f'open {ctl}\nset threads {n}\nset x 1 64\nset y 1 64\nset z 1\nset t 1\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n',
        text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=e,timeout=120)
    fatal=bool(re.search(r'Error|ERROR|failed',p.stdout))
    assert p.returncode==0 and fatal==error,p.stdout[-3000:]
    if not error:assert not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-3000:]
    return p.stdout,parse(p.stdout)
samples=0
def compare(a,b):
    global samples
    assert len(a)==len(b)
    for x,y in zip(a,b):
        assert len(x)==len(y)
        for u,v in zip(x,y):
            assert (u is None)==(v is None),'mask mismatch'
            if u is None:continue
            assert u==v or (math.isnan(u) and math.isnan(v)),(u,v)
            samples+=1
formats=[('dat','slice.dat','',False),('dat-template','slice%tm1.dat','',True)]
if (root/'slice.nc').exists():formats += [('nc','slice.nc','netcdf',False),('nc4','slice_compressed.nc','netcdf',False),('nc-template','slice%tm1.nc','netcdf',True)]
if (root/'slice.h5').exists():formats += [('h5','slice.h5','hdf5_grid',False),('h5-template','slice%tm1.h5','hdf5_grid',True)]
if (root/'slice.bp').exists():formats += [('bp','slice.bp','bp5',False)]
ctls={}
for label,dataset,dtype,template in formats:
  for reverse in (False,True):
    dims='t,z,y,x' if dtype=='netcdf' or (dtype=='hdf5_grid' and not template) else 'z,y,x'
    plane='t,y,x' if dtype=='netcdf' or (dtype=='hdf5_grid' and not template) else 'y,x'
    ctl=root/(label+('-rev' if reverse else '')+'.ctl');ctls[label,reverse]=ctl
    ctl.write_text('dset ^'+dataset+'\n'+('dtype '+dtype+'\n' if dtype else '')+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        'undef -9999\nxdef 64 linear 10 .1\nydef 64 linear -60 .25\nzdef 4 levels 50 150 400 1000\ntdef 4 linear 00z01jan2000 1hr\nvars 4\n'+
        'f 4 '+(dims if dtype else '99')+' F\ng 4 '+(dims if dtype else '99')+' G\nkmap 0 '+(plane if dtype else '99')+' Map\nsurface 0 '+(plane if dtype else '99')+' Surface\nendvars\n')
    commands=[]
    for op in ('>','>='):
      for cut in ('104','103.99999999999999','104.00000000000001','1e-5','-2','1e8'):
        mask=f'(f{op}{cut})-0.5'
        commands += [f'd maskout(f,{mask})',f'd maskout(f+0,{mask})']
    for area in ('aave','amean'):
        commands += [f'd {area}(maskout(f,(f>104)-0.5),x=1.25,x=63.6,y=2.1,y=62.8)',
                     f'd {area}(maskout(f+0,(f>104)-0.5),x=1.25,x=63.6,y=2.1,y=62.8)']
    reference=None
    for n in (1,4):
      text,value=run(ctl,'\n'.join(commands),n);assert text.count('MASKOUT I/O: reused native input')==14
      compare(value[::2],value[1::2])
      if reference is not None:compare(value,reference)
      reference=value
    if not reverse:
      assert value[0][4] is None and value[2*6][4]==104,'strict/inclusive boundary changed'
      assert value[0][5]==105
    print('Threshold masks passed:',ctl.name,flush=True)

ctl=ctls['dat',False]
fallback=['maskout(f,(g>104)-0.5)','maskout(f,(f<104)-0.5)',
          'maskout(f,(f>104)-1)','maskout(f,((f>104))-0.5)']
for expr in fallback:
    text,value=run(ctl,'d '+expr);assert 'MASKOUT I/O:' not in text
    compare(value,run(ctl,'d '+expr.replace('maskout(f,','maskout(f+0,'))[1])
text,value=run(ctl,'define f=f+0\nd maskout(f,(f>104)-0.5)');assert 'MASKOUT I/O:' not in text
run(ctl,'d maskout(f,(ff>104)-0.5)',error=True)
_,source=run(ctl,'d f\nd maskout(f,(f>104)-0.5)\nd f')
compare(source[:1],source[2:])
_,scalar=run(ctl,'set x 5\nset y 1\nd maskout(f,(f>104)-0.5)\nd maskout(f,(f>=104)-0.5)')
assert scalar==[[None],[104.0]],scalar
if ('h5',False) in ctls:
    ctl=ctls['h5',False]
    fast,a=run(ctl,'d maskout(f,(f>104)-0.5)',probe=True)
    slow,b=run(ctl,'d maskout(f+0,(f>104)-0.5)',probe=True)
    compare(a,b)
    assert re.findall(r'H5_READS (\d+)',fast)==['1'],fast[-1000:]
    assert re.findall(r'H5_READS (\d+)',slow)==['2'],slow[-1000:]
    print('Threshold HDF5 calls: 2 -> 1',flush=True)
    image=root/'native.png';reference=root/'reference.png'
    run(ctl,'set gxout shaded\nd maskout(f,(f>104)-0.5)\nprintim '+str(image)+
        ' x640 y480\nclear canvas\nd maskout(f+0,(f>104)-0.5)\nprintim '+str(reference)+' x640 y480',graphics=True)
    assert image.read_bytes()==reference.read_bytes(),'threshold graphic differs'
print('Threshold mask regression passed: exact samples=%d; masks, boundary cases, source values, native formats and fallback agree.'%samples)
PY
