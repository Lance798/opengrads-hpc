#!/usr/bin/env bash
# Cross-format reduction I/O and parallel column regression. GPLv2.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
test_root="$(mktemp -d /tmp/opengrads-reduction-io.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
flags=() libs=()
adios_root="${OPENGRADS_ADIOS2_ROOT:-}"
if [[ -x "$adios_root/bin/adios2-config" ]]; then
  read -r -a aflags <<< "$("$adios_root/bin/adios2-config" --serial --c-flags)"
  read -r -a alibs <<< "$("$adios_root/bin/adios2-config" --serial --c-libs)"
  flags+=(-DZS_ADIOS "${aflags[@]}"); libs+=("${alibs[@]}")
  if grep -Eq '^#define ADIOS2_HAVE_BLOSC2($|[[:space:]]+1)' "$adios_root/include/adios2/common/ADIOSConfig.h"; then flags+=(-DZS_BLOSC); fi
fi
nc_config="${OPENGRADS_TEST_NETCDF_ROOT:+$OPENGRADS_TEST_NETCDF_ROOT/bin/}nc-config"
if command -v "$nc_config" >/dev/null 2>&1; then
  read -r -a nflags <<< "$("$nc_config" --cflags)"
  read -r -a nlibs <<< "$("$nc_config" --libs)"
  flags+=(-DZS_NETCDF "${nflags[@]}"); libs+=("${nlibs[@]}")
  for flag in "${nlibs[@]}"; do if [[ "$flag" == -L* ]]; then libs+=("-Wl,-rpath,${flag#-L}"); fi; done
fi
if [[ -n "${OPENGRADS_TEST_HDF5_ROOT:-}" ]]; then
  flags+=(-DZS_HDF5 "-I$OPENGRADS_TEST_HDF5_ROOT/include")
  libs+=("-L$OPENGRADS_TEST_HDF5_ROOT/lib" "-Wl,-rpath,$OPENGRADS_TEST_HDF5_ROOT/lib" -lhdf5)
elif pkg-config --exists hdf5; then
  read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
  read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
  flags+=(-DZS_HDF5 "${hflags[@]}"); libs+=("${hlibs[@]}")
fi
"${CC:-gcc}" -std=gnu99 -Wall -Wextra -Werror -DNX=256 -DNY=256 -DNT=6 "${flags[@]}" \
  "$repo_root/pytests/zslice_writer.c" "${libs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root" zint
python3 -u - "${OPENGRADS_LAUNCHER:-$repo_root/opengrads}" "$build_root" "$test_root" <<'PY'
import math,os,re,subprocess,sys
from pathlib import Path
launcher,build,directory=sys.argv[1:]; root=Path(directory)
env=dict(os.environ,OPENGRADS_BUILD_ROOT=build,OPENGRADS_COLOR='0',GA_PROGRESS='off')
def run(ctl,commands,threads):
    text='open '+str(ctl)+'\nset threads '+str(threads)+'\nset gxout print\nset prnopts %.17g 32 1\n'+commands+'\nquit\n'
    p=subprocess.run([launcher,'-bl','-d','gxdummy','-h','gxdummy'],input=text,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=90)
    errors=[s for s in p.stdout.splitlines() if re.search(r'Error|ERROR|Invalid expression',s)]
    assert p.returncode==0 and not errors,'\n'.join(errors)+'\n'+p.stdout[-1000:]
    lines=p.stdout.splitlines(); grids=[]
    for i,line in enumerate(lines):
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n=int(m.group(1)); missing=float(m.group(2)); values=[]
        for row in lines[i+1:]:
            if len(values)>=n:break
            values.extend(float(v) for v in row.split())
        assert len(values)==n,(ctl,n,len(values))
        grids.append([None if v==missing else v for v in values])
    return grids
max_abs=max_rel=0.0
samples=0
nonfinite=0
def compare(a,b,label,exact=False):
    global max_abs,max_rel,samples,nonfinite
    assert len(a)==len(b),label
    for ga,gb in zip(a,b):
        assert len(ga)==len(gb),label
        for x,y in zip(ga,gb):
            assert (x is None)==(y is None),(label,'mask mismatch')
            if x is None:continue
            if not math.isfinite(x) or not math.isfinite(y):
                assert (math.isnan(x) and math.isnan(y)) or x==y,(label,x,y)
                nonfinite+=1
                continue
            err=abs(x-y); rel=err/max(abs(y),1e-30)
            max_abs=max(max_abs,err); max_rel=max(max_rel,rel); samples+=1
            assert x==y if exact else err<=1e-8+abs(y)*2e-12,(label,x,y,err)
formats=[('dat','slice.dat','',False),('dat-template','slice%tm1.dat','',True)]
if (root/'slice.nc').exists():formats += [('nc','slice.nc','netcdf',False),('nc4','slice4.nc','netcdf',False),('nc-template','slice%tm1.nc','netcdf',True)]
if (root/'slice.h5').exists():formats += [('h5','slice.h5','hdf5_grid',False),('h5-template','slice%tm1.h5','hdf5_grid',True)]
if (root/'slice.bp').exists():formats += [('bp','slice.bp','bp5',False)]
if (root/'slicezstd.bp').exists():formats += [('bpzstd','slicezstd.bp','bp5',False)]
# Every native block/chunk exceeds the OpenMP work threshold.
commands='set t 2\nset z 1\nset x 0 257\nset y 0 257\nd f\nset x 1 256\nset y 1 256\nd zint(f)\nd zint(f+0)\nd zint(g,125,875)\nset x 1\nset y 1\nd aave(f,x=1,x=256,y=1,y=256)\nd aave(zint(f),x=1,x=256,y=1,y=256)\n'
references={}
slabrefs={}
for label,dataset,dtype,template in formats:
  for reverse in (False,True):
    dims='t,z,y,x' if dtype=='netcdf' or (dtype=='hdf5_grid' and not template) else 'z,y,x'
    surface='t,y,x' if dtype=='netcdf' or (dtype=='hdf5_grid' and not template) else 'y,x'
    ctl=root/(label+('-rev' if reverse else '')+'.ctl')
    ctl.write_text('dset ^'+dataset+'\n'+('dtype '+dtype+'\n' if dtype else '')+
        ('options '+('template ' if template else '')+('yrev zrev' if reverse else '')+'\n' if template or reverse else '')+
        'undef -9999\nxdef 256 linear 10 0.1\nydef 256 linear -60 0.25\nzdef 4 levels 50 150 400 1000\n'+
        'tdef '+('2' if template else '6')+' linear 00z01jan2000 1hr\nvars 4\nf 4 '+(dims if dtype else '99')+' Float\ng 4 '+(dims if dtype else '99')+' Double\nkmap 0 '+(surface if dtype else '99')+' Index\nsurface 0 '+(surface if dtype else '99')+' Surface\nendvars\n')
    extra=''
    if not template:
        extra='set x 1 256\nset y 1 256\nset z 1\nd ave(f+g,t=1,t=6)\nd ave((f+g)+0,t=1,t=6)\nd ave(f+g,t=1.2,t=5.8,-b)\nd ave((f+g)+0,t=1.2,t=5.8,-b)\n'
    # Native XYZ slabs must preserve masks, nonuniform Z weights and the
    # level-by-level addition order of the expression evaluator.
    slabs='set x 1 128\nset y 1 128\nset z 1\n'
    for op in ('ave','mean','sum','sumg'):
        for limits in ('z=1,z=4','z=1.25,z=3.75,-b'):
            slabs+='d '+op+'(f,'+limits+')\nd '+op+'(f+0,'+limits+')\n'
    for threads in ((1,8) if reverse else (1,2,4,8)):
        actual=run(ctl,commands+extra+slabs,threads)
        assert len(actual)==(26 if extra else 22),(label,len(actual))
        compare([actual[1]],[actual[2]],label+' native/evaluator',exact=True)
        if extra:
            compare([actual[6],actual[8]],[actual[7],actual[9]],label+' batch/evaluator',exact=True)
        start=10 if extra else 6
        for idx in range(start,len(actual),2):
            compare([actual[idx]],[actual[idx+1]],label+' vertical slab/evaluator',exact=True)
        key=(reverse,template)
        if key not in references:references[key]=actual[:start]
        else:compare(actual[:start],references[key],label+' cross-format/threads')
        skey=(label,reverse)
        if skey not in slabrefs:slabrefs[skey]=actual[start:]
        else:compare(actual[start:],slabrefs[skey],label+' slab threads',exact=True)
    print('Reduction I/O passed:',label,'reversed' if reverse else 'normal')

if (root/'slice.h5').exists():
    # The bulk HDF5 reader must mask native integers before unpacking.
    ctl=root/'packed.ctl'
    ctl.write_text('dset ^slice.h5\ndtype hdf5_grid\nundef -9999 _FillValue\n'+
        'unpack scale_factor add_offset\nxdef 256 linear 10 0.1\nydef 256 linear -60 0.25\n'+
        'zdef 4 levels 50 150 400 1000\ntdef 6 linear 00z01jan2000 1hr\nvars 1\npacked 4 t,z,y,x Packed integer\nendvars\n')
    wanted=[None if x==0 and y==0 else ((700+10*y+x)%5000-2500)*0.125+10 for y in range(256) for x in range(256)]
    for threads in (1,8):
        actual=run(ctl,'set t 2\nset z 1\nd packed\nd zint(packed)\nd zint(packed+0)\n',threads)
        assert len(actual)==3
        compare([actual[0]],[wanted],'HDF5 packed integer',exact=True)
        compare([actual[1]],[actual[2]],'HDF5 packed ZINT',exact=True)
    template=root/'packed-template.ctl'
    template.write_text(ctl.read_text().replace('^slice.h5','^slice%tm1.h5').replace('dtype hdf5_grid','dtype hdf5_grid\noptions template').replace('tdef 6 ','tdef 2 ').replace('t,z,y,x','z,y,x'))
    compare(run(template,'set t 2\nset z 1\nd packed\n',8),[wanted],'HDF5 single-element attributes',exact=True)
    invalid=root/'packed-vector.ctl'
    invalid.write_text(ctl.read_text().replace('unpack scale_factor','unpack bad_scale'))
    raw=[None if x==0 and y==0 else ((700+10*y+x)%5000-2500)+10 for y in range(256) for x in range(256)]
    compare(run(invalid,'set t 2\nset z 1\nd packed\n',1),[raw],'HDF5 vector attribute rejected',exact=True)
    # A profile without native X/Y must broadcast across the requested map.
    ctl=root/'profile.ctl'
    ctl.write_text((root/'h5.ctl').read_text().split('vars ')[0]+'vars 1\nrho 4 z Density\nendvars\n')
    compare(run(ctl,'set z 2\nd rho\n',8),[[1.1]*(256*256)],'HDF5 profile broadcast',exact=True)

if (root/'slice.nc').exists():
    # Failed file opens must yield missing values and permit a later reopen.
    ctl=root/'missing-template.ctl'
    ctl.write_text((root/'nc-template.ctl').read_text().replace('tdef 2 ','tdef 3 '))
    actual=run(ctl,'set t 3\nd f\nset t 1\nd f\nset t 3\nd f\n',1)
    assert len(actual)==3 and all(v is None for v in actual[0]+actual[2]),'NetCDF missing template'
    compare([actual[1]],run(root/'nc-template.ctl','set t 1\nd f\n',1),'NetCDF template reopen',exact=True)
print('Non-finite legacy reduction values retain their classification:',nonfinite)
print('Reduction I/O regression passed: samples=%d max_abs=%.17g max_rel=%.17g; masks agree; native ZINT and batched sums match evaluator exactly.'%(samples,max_abs,max_rel))
PY
