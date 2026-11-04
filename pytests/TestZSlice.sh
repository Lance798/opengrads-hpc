#!/usr/bin/env bash
# Added in 2026 for indexed vertical slices. GPLv2; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d /tmp/opengrads-zslice.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
flags=() libs=()
adios_root="${OPENGRADS_ADIOS2_ROOT:-}"
if [[ -x "$adios_root/bin/adios2-config" ]]; then
  read -r -a aflags <<< "$("$adios_root/bin/adios2-config" --serial --c-flags)"
  read -r -a alibs <<< "$("$adios_root/bin/adios2-config" --serial --c-libs)"
  flags+=(-DZS_ADIOS "${aflags[@]}"); libs+=("${alibs[@]}")
  adios_config="$adios_root/include/adios2/common/ADIOSConfig.h"
  if [[ -r "$adios_config" ]] && grep -Eq '^#define ADIOS2_HAVE_BLOSC2($|[[:space:]]+1)' "$adios_config"; then
    flags+=(-DZS_BLOSC)
  fi
fi
nc_config="${OPENGRADS_TEST_NETCDF_ROOT:+$OPENGRADS_TEST_NETCDF_ROOT/bin/}nc-config"
if command -v "$nc_config" >/dev/null 2>&1; then
  read -r -a nflags <<< "$("$nc_config" --cflags)"
  read -r -a nlibs <<< "$("$nc_config" --libs)"
  flags+=(-DZS_NETCDF "${nflags[@]}"); libs+=("${nlibs[@]}")
  for link_flag in "${nlibs[@]}"; do
    if [[ "$link_flag" == -L* ]]; then libs+=("-Wl,-rpath,${link_flag#-L}"); fi
  done
fi
if [[ -n "${OPENGRADS_TEST_HDF5_ROOT:-}" ]]; then
  flags+=(-DZS_HDF5 "-I$OPENGRADS_TEST_HDF5_ROOT/include")
  libs+=("-L$OPENGRADS_TEST_HDF5_ROOT/lib" "-Wl,-rpath,$OPENGRADS_TEST_HDF5_ROOT/lib" -lhdf5)
elif pkg-config --exists hdf5; then
  read -r -a hflags <<< "$(pkg-config --cflags hdf5)"
  read -r -a hlibs <<< "$(pkg-config --libs hdf5)"
  flags+=(-DZS_HDF5 "${hflags[@]}"); libs+=("${hlibs[@]}")
fi
"${CC:-gcc}" -std=gnu99 -Wall -Wextra -Werror "${flags[@]}" \
  "$repo_root/pytests/zslice_writer.c" "${libs[@]}" -lm -o "$test_root/writer"
"$test_root/writer" "$test_root"
python3 - "$launcher" "$build_root" "$test_root" <<'PY'
import math
import os
from pathlib import Path
import re
import subprocess
import sys

launcher, build, directory = sys.argv[1:]
root = Path(directory)
env = dict(os.environ, OPENGRADS_BUILD_ROOT=build, OPENGRADS_COLOR='0', GA_ZSLICE_TRACE='1')
nx, ny, nz = 16, 8, 4
heights = (50, 150, 400, 1000)
hardcopy = 'Cairo' if (Path(build) / 'src/.libs/libgxpCairo.so').exists() or (Path(build) / 'src/.libs/libgxpCairo.dylib').exists() else 'gxdummy'

def run(text, errors=False):
    p = subprocess.run([launcher, '-bl', '-d', 'gxdummy', '-h', hardcopy],
                       input=text+'\nquit\n', universal_newlines=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=90)
    if p.returncode or (not errors and re.search(r'Error|ERROR|Unknown command|logic check|Invalid expression', p.stdout)):
        raise AssertionError(p.stdout)
    return p.stdout

def grids(text):
    lines=text.splitlines(); result=[]
    for i,line in enumerate(lines):
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m: continue
        n=int(m.group(1)); missing=float(m.group(2)); values=[]
        for row in lines[i+1:]:
            if len(values)>=n: break
            values.extend(float(x) for x in row.split())
        assert len(values)==n, text
        result.append([None if x==missing else x for x in values])
    return result

def expected(t, kind='f', yr=False, zr=False, index=None):
    result=[]
    for y in range(ny):
        yy=ny-1-y if yr else y
        for x in range(nx):
            k=index if index is not None else 1+(x//2+yy+t)%nz
            if index is None and yy==0 and x<6: k=(0,5,1.5,-9999,float('nan'),float('inf'))[x]
            if not math.isfinite(k) or k<1 or k>nz or int(k)!=k: result.append(None); continue
            k=int(k); zz=nz-k if zr else k-1
            if kind=='lev': value=heights[k-1]
            elif kind=='surface': value=900+100*t+10*yy+x
            elif kind=='rho': value=(1.2,1.1,1.0,0.9)[zz]
            elif kind=='f' and zz==0 and yy==2 and x==4: value=None
            else: value=1000*t+100*(zz+1)+10*yy+x+(0.125 if kind=='g' else 0)
            result.append(value)
    return result

def same(actual, wanted, text):
    assert len(actual)==len(wanted), text
    assert all(a is None if w is None else a is not None and abs(a-w)<1e-10
               for a,w in zip(actual,wanted)), (actual,wanted,text)

config=run('q config')
formats=[('dat','slice.dat',''),('dat-template','slice%tm1.dat','')]
if (root/'slice.nc').exists() and 'netcdf' in config:
    formats += [('nc','slice.nc','netcdf'),('nc4','slice4.nc','netcdf'),
                ('nc-template','slice%tm1.nc','netcdf')]
if (root/'slice.h5').exists() and 'hdf5' in config:
    formats += [('h5','slice.h5','hdf5_grid'),('h5-template','slice%tm1.h5','hdf5_grid')]
if (root/'slice.bp').exists() and 'adios2-bp5' in config:
    formats += [('bp','slice.bp','bp5')]
    if (root/'slicezstd.bp').exists(): formats += [('bpzstd','slicezstd.bp','bp5')]
for label, dataset, dtype in formats:
    for reverse in (False,True):
        name=label+('-reverse' if reverse else '')
        ctl=root/(name+'.ctl')
        native='t,z,y,x' if dtype in ('netcdf','hdf5_grid') else 'z,y,x' if dtype=='bp5' else '99'
        surface_dims='t,y,x' if dtype in ('netcdf','hdf5_grid') else 'y,x' if dtype=='bp5' else '99'
        if '%' in dataset and dtype=='hdf5_grid': native='z,y,x'; surface_dims='y,x'
        options=('template ' if '%' in dataset else '')+('yrev zrev' if reverse else '')
        ctl.write_text('dset ^'+dataset+'\n'+('dtype '+dtype+'\n' if dtype else '')+
                       ('options '+options+'\n' if options else '')+
                       'undef -9999\nxdef 16 linear 10 0.1\nydef 8 linear 20 0.2\n'+
                       'zdef 4 levels 50 150 400 1000\ntdef 2 linear 00z01jan2000 1hr\n'+
                       ('vars 5\n' if dtype in ('bp5','hdf5_grid') else 'vars 4\n')+
                       'f 4 '+native+' Fixture float\ng 4 '+native+' Fixture double\n'+
                       'kmap 0 '+surface_dims+' Index map\nsurface 0 '+surface_dims+' Surface field\n'+
                       ('rho 4 z Reference density\n' if dtype in ('bp5','hdf5_grid') else '')+'endvars\n')
        prefix='open '+str(ctl)+'\nset gxout print\nset prnopts %.17g 16 1\n'
        commands=''
        wanted=[]
        for t in range(2):
            commands+='set t '+str(t+1)+'\n'
            for expr,kind in [('f','f'),('f+0','f'),('g','g'),('lev','lev'),('surface','surface')]:
                commands+='d zslice('+expr+',kmap)\n'
                wanted.append(expected(t,kind,reverse,reverse))
            commands+='define picked = zslice(f,kmap)\nd picked\nundefine picked\n'
            wanted.append(expected(t,'f',reverse,reverse))
            commands+='d zslice(f,2)\n'
            wanted.append(expected(t,'f',reverse,reverse,index=2))
            if dtype in ('bp5','hdf5_grid'):
                commands+='d zslice(rho,kmap)\n'
                wanted.append(expected(t,'rho',reverse,reverse))
        out=run(prefix+commands)
        actual=grids(out)
        assert len(actual)==len(wanted), out
        for a,w in zip(actual,wanted): same(a,w,out)
        if dtype=='bp5': assert 'ZSLICE I/O: BP5 blocks=4' in out, out
        elif dtype=='hdf5_grid': assert 'ZSLICE I/O: HDF5 chunks=' in out, out
        else: assert 'ZSLICE I/O: indexed row runs=' in out, out

        commands='set t 1\nset y 4\n'
        out=run(prefix+commands+'d zslice(f,kmap)\nset z 1 4\nd zslice(f,kmap)\n'+
                'set z 1\nset x 8\nset y 1 8\nd zslice(f,kmap)\nset y 4\nd zslice(f,kmap)\n')
        full=expected(0,'f',reverse,reverse); a=grids(out)
        same(a[0],full[3*nx:4*nx],out); same(a[1],a[0],out)
        same(a[2],[full[y*nx+7] for y in range(ny)],out)
        same(a[3],[full[3*nx+7]],out)

        # File qualifiers, calculated maps, and wholly invalid selections.
        out=run(prefix+'open '+str(ctl)+'\nd zslice(f.2,kmap.1+0)\n'+
                'd zslice(lev.2,kmap.1)\nd zslice(f,const(kmap,0,-a))\n')
        a=grids(out); same(a[0],full,out); same(a[1],expected(0,'lev',reverse,reverse),out)
        assert a[2]==[None]*(nx*ny), out

        # A pre-defined 3-D cube uses the evaluator fallback and must agree.
        out=run(prefix+'set z 1 4\ndefine cube=f\nset z 1\nd zslice(cube,kmap)\n')
        same(grids(out)[0],full,out)

        if not reverse and hardcopy=='Cairo':
            a,b=root/(label+'-direct.png'),root/(label+'-defined.png')
            run('open '+str(ctl)+'\nset grads off\nset timelab off\nset gxout shaded\n'+
                'd zslice(f,kmap)\nprintim '+str(a)+' x640 y480\nclear canvas\n'+
                'define picked=zslice(f,kmap)\nd picked\nprintim '+str(b)+' x640 y480\n')
            assert a.read_bytes()==b.read_bytes(), label+' display/define PNG mismatch'
        print('ZSLICE passed: '+name)

out=run('open '+str(root/'dat.ctl')+'\nset gxout print\n'+
        'd zslice(f)\nd zslice(f,kmap,kmap)\nset t 1 2\nd zslice(f,kmap)\n', errors=True)
assert out.count('ZSLICE error:')==2, out
for actual,t in zip(grids(out),range(2)): same(actual,expected(t),out)
assert len(grids(out))==2, out
print('ZSLICE regression passed: direct display/define, analytic values, native/fallback paths, nonuniform heights, invalid indices, missing data, time, profiles, and reversed axes.')
PY
