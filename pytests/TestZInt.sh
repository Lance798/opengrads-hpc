#!/usr/bin/env bash
# Added in 2026 for height-coordinate column integrals. GPLv2; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d /tmp/opengrads-zint.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
flags=() libs=()
adios_root="${OPENGRADS_ADIOS2_ROOT:-}"
if [[ -x "$adios_root/bin/adios2-config" ]]; then
  read -r -a aflags <<< "$("$adios_root/bin/adios2-config" --serial --c-flags)"
  read -r -a alibs <<< "$("$adios_root/bin/adios2-config" --serial --c-libs)"
  flags+=(-DZS_ADIOS "${aflags[@]}"); libs+=("${alibs[@]}")
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
"$test_root/writer" "$test_root" zint
python3 - "$launcher" "$build_root" "$test_root" <<'PY'
import math
import os
from pathlib import Path
import re
import subprocess
import sys

launcher,build,directory=sys.argv[1:]
root=Path(directory)
env=dict(os.environ,OPENGRADS_BUILD_ROOT=build,OPENGRADS_COLOR='0',GA_ZINT_TRACE='1')
nx,ny,nz=16,8,4
heights=(50,150,400,1000)
edges=(0,100,275,700,1300)
rho=(1.2,1.1,1.0,0.9)
hardcopy='Cairo' if any((Path(build)/('src/.libs/libgxpCairo'+ext)).exists() for ext in ('.so','.dylib')) else 'gxdummy'

def run(commands,errors=False):
    p=subprocess.run([launcher,'-bl','-d','gxdummy','-h',hardcopy],input=commands+'\nquit\n',
                     universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=90)
    if p.returncode or (not errors and re.search(r'Error|ERROR|Unknown command|Invalid expression|logic check',p.stdout)):
        raise AssertionError(p.stdout)
    return p.stdout

def grids(text):
    lines=text.splitlines(); result=[]
    for i,line in enumerate(lines):
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n=int(m.group(1));missing=float(m.group(2));values=[]
        for row in lines[i+1:]:
            if len(values)>=n:break
            values.extend(float(x) for x in row.split())
        assert len(values)==n,text
        result.append([None if x==missing else x for x in values])
    return result

def same(actual,wanted,text):
    assert len(actual)==len(wanted),(len(actual),len(wanted),text)
    assert all(a is None if w is None else a is not None and abs(a-w)<=max(1e-8,abs(w)*2e-12)
               for a,w in zip(actual,wanted)),(actual,wanted,text)

def expected(t,kind='f',reverse=False,bottom=0,top=1300,profile=False):
    values=[]
    for y in range(ny):
        yy=ny-1-y if reverse else y
        for x in range(nx):
            km=1+(x//2+yy+t)%nz
            if yy==0 and x<6:km=(0,5,1.5,-9999,float('nan'),float('inf'))[x]
            def bound(b):
                if b=='map':return None if km==-9999 or not math.isfinite(km) else km*100
                if b=='surface':return 900+100*t+10*yy+x
                return b
            low,high=bound(bottom),bound(top)
            if low is None or high is None or low>high:values.append(None);continue
            total=0
            for k in range(nz):
                zz=nz-1-k if reverse else k
                weight=max(0,min(max(edges[k],edges[k+1]),high)-max(min(edges[k],edges[k+1]),low))
                if kind=='one':v=1
                elif kind=='lev':v=heights[k]
                elif kind=='surface':v=900+100*t+10*yy+x
                elif kind=='rho':v=rho[zz]
                else:
                    v=1000*t+100*(zz+1)+10*yy+x+(0.125 if kind=='g' else 0)
                    if yy==0 and (x<3 or (x==3 and zz==1)):continue
                    if kind=='f' and zz==0 and yy==2 and x==4:continue
                total+=v*(rho[zz] if profile else 1)*weight
            values.append(total)
    return values

config=run('q config')
formats=[('dat','slice.dat',''),('dat-template','slice%tm1.dat','')]
if (root/'slice.nc').exists() and 'netcdf' in config:
    formats += [('nc','slice.nc','netcdf'),('nc4','slice4.nc','netcdf'),('nc-template','slice%tm1.nc','netcdf')]
if (root/'slice.h5').exists() and 'hdf5' in config:
    formats += [('h5','slice.h5','hdf5_grid'),('h5-template','slice%tm1.h5','hdf5_grid')]
if (root/'slice.bp').exists() and 'adios2-bp5' in config:
    formats += [('bp','slice.bp','bp5'),('bpzstd','slicezstd.bp','bp5')]
for label,dataset,dtype in formats:
    for reverse in (False,True):
        name=label+('-reverse' if reverse else '')
        ctl=root/(name+'.ctl')
        native='t,z,y,x' if dtype in ('netcdf','hdf5_grid') else 'z,y,x' if dtype=='bp5' else '99'
        sdims='t,y,x' if dtype in ('netcdf','hdf5_grid') else 'y,x' if dtype=='bp5' else '99'
        if '%' in dataset and dtype=='hdf5_grid':native='z,y,x';sdims='y,x'
        options=('template ' if '%' in dataset else '')+('yrev zrev' if reverse else '')
        hasrho=dtype in ('bp5','hdf5_grid')
        ctl.write_text('dset ^'+dataset+'\n'+('dtype '+dtype+'\n' if dtype else '')+
                       ('options '+options+'\n' if options else '')+'undef -9999\n'+
                       'xdef 16 linear 10 0.1\nydef 8 linear 20 0.2\nzdef 4 levels 50 150 400 1000\n'+
                       'tdef 2 linear 00z01jan2000 1hr\nvars '+('5' if hasrho else '4')+'\n'+
                       'f 4 '+native+' Float field\ng 4 '+native+' Double field\n'+
                       'kmap 0 '+sdims+' Bound map\nsurface 0 '+sdims+' Surface field\n'+
                       ('rho 4 z Density profile\n' if hasrho else '')+'endvars\n')
        prefix='open '+str(ctl)+'\nset gxout print\nset prnopts %.17g 16 1\n'
        cases=[('f','f',0,1300),('f+0','f',0,1300),('g','g',0,1300),('1','one',0,1300),
               ('lev','lev',0,1300),('surface','surface',0,1300),
               ('f,125,875','f',125,875),('f,auto,875','f',0,875),('f,125','f',125,1300),
               ('f,auto,auto','f',0,1300),('f,-500,2000','f',-500,2000),
               ('f,2000,3000','f',2000,3000),('f,100,100','f',100,100),('f,500,100','f',500,100),
               ('f,kmap*100,surface','f','map','surface'),('f,0,kmap*100','f',0,'map'),
               ('f,kmap*100,100','f','map',100)]
        if hasrho:cases += [('rho','rho',0,1300),('f*rho','f',0,1300),('rho*f','f',0,1300),
                           ('(f*rho)+0','f',0,1300),('g*rho,kmap*100,surface','g','map','surface')]
        commands='';wanted=[]
        for t in range(2):
            commands+='set t '+str(t+1)+'\n'
            for expr,kind,bottom,top in cases:
                commands+='d zint('+expr+')\n'
                wanted.append(expected(t,kind,reverse,bottom,top,profile='*rho' in expr or 'rho*' in expr))
            commands+='define column=zint(f)\nd column\nundefine column\n'
            wanted.append(expected(t,'f',reverse))
        out=run(prefix+commands);actual=grids(out)
        assert len(actual)==len(wanted),out
        for index,(a,w) in enumerate(zip(actual,wanted)):
            try:same(a,w,name+' grid '+str(index))
            except AssertionError:
                print('Failing expression:',cases[index%(len(cases)+1)] if index%(len(cases)+1)<len(cases) else 'define')
                raise
        if dtype=='bp5':assert 'ZINT I/O: BP5 blocks=4' in out,out
        if dtype=='hdf5_grid':assert 'ZINT I/O: HDF5 chunks=' in out,out

        # Fixed lines/points, varying Z removal, defined cubes, and file qualifiers.
        out=run(prefix+'set y 4\nd zint(f)\nset z 1 4\nd zint(f)\nset z 1\n'+
                'set x 8\nset y 1 8\nd zint(f)\nset y 4\nd zint(f)\n')
        full=expected(0,'f',reverse);a=grids(out)
        same(a[0],full[3*nx:4*nx],out);same(a[1],a[0],out)
        same(a[2],[full[y*nx+7] for y in range(ny)],out);same(a[3],[full[3*nx+7]],out)
        out=run(prefix+'set z 1 4\ndefine cube=f\nset z 2\nd zint(cube)\n'+
                'open '+str(ctl)+'\nd zint(f.2)\nd zint(f.2,kmap.1*100,surface.2)\n')
        a=grids(out);same(a[0],full,out);same(a[1],full,out)
        same(a[2],expected(0,'f',reverse,'map','surface'),out)
        if hasrho:
            # A profile in its own 1x1 descriptor must work in the native product.
            pct=root/(name+'-profile.ctl')
            pct.write_text(ctl.read_text().replace('xdef 16','xdef 1').replace('ydef 8','ydef 1').split('vars ')[0]+
                           'vars 1\nrho 4 z Density profile\nendvars\n')
            out=run(prefix+'open '+str(pct)+'\nd zint(f.1*rho.2)\n')
            same(grids(out)[0],expected(0,'f',reverse,profile=True),out)
            out=run(prefix+'open '+str(pct)+'\nset x 0 16\nd zint(f.1*rho.2)\n')
            full=expected(0,'f',reverse,profile=True)
            extended=[]
            for y in range(ny):extended += [0]+full[y*nx:(y+1)*nx]
            same(grids(out)[0],extended,out)
        if not reverse and hardcopy=='Cairo':
            a,b=root/(label+'-direct.png'),root/(label+'-defined.png')
            run('open '+str(ctl)+'\nset grads off\nset timelab off\nset gxout shaded\n'+
                'd zint(f)\nprintim '+str(a)+' x640 y480\nclear canvas\n'+
                'define column=zint(f)\nd column\nprintim '+str(b)+' x640 y480\n')
            assert a.read_bytes()==b.read_bytes(),label+' direct/define PNG mismatch'
        if not reverse:
            descending=root/(label+'-descending.ctl')
            descending.write_text(ctl.read_text().replace('50 150 400 1000','1000 400 150 50'))
            heights=tuple(reversed(heights));edges=tuple(reversed(edges))
            out=run('open '+str(descending)+'\nset gxout print\nset prnopts %.17g 16 1\nd zint(f)\nd zint(f,125,875)\n')
            same(grids(out)[0],expected(0),out);same(grids(out)[1],expected(0,bottom=125,top=875),out)
            heights=tuple(reversed(heights));edges=tuple(reversed(edges))
        print('ZINT passed: '+name)

out=run('open '+str(root/'dat.ctl')+'\nset gxout print\nset prnopts %.17g 16 1\nd zint()\nd zint(f,0,100,200)\n'+
        'd zint(f,missingfield)\nset t 1 2\nd zint(f)\n',errors=True)
assert 'ZINT error:' in out and 'Syntax Error' in out,out
assert len(grids(out))==2,out
for a,t in zip(grids(out),range(2)):same(a,expected(t),out)
# Bounds must have the same horizontal grid. A scalar missing bound gives undef.
out=run('open '+str(root/'dat.ctl')+'\nset gxout print\nd zint(f,maskout(0,-1),1000)\n')
assert grids(out)[0]==[None]*(nx*ny),out
single=root/'single.ctl'
single.write_text((root/'dat.ctl').read_text().replace('zdef 4 levels 50 150 400 1000','zdef 1 linear 50 1').replace('f 4 ','f 1 ').replace('g 4 ','g 1 '))
out=run('open '+str(single)+'\nd zint(f)\n',errors=True)
assert 'at least two distinct height levels' in out,out
# A bound with an explicitly different horizontal domain is rejected.
different=root/'different-grid.ctl'
different.write_text((root/'dat.ctl').read_text().replace('xdef 16 linear 10 0.1','xdef 16 linear 10 0.2'))
out=run('open '+str(root/'dat.ctl')+'\nopen '+str(different)+'\nd zint(f.1,surface.2,1000)\n',errors=True)
assert 'matching horizontal grids' in out,out
print('ZINT regression passed: analytic overlap integrals, defaults, 2-D bounds, nansum, profiles, native/fallback, display/define, templates, reversed axes, and file qualifiers.')
PY
