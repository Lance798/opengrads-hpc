#!/usr/bin/env bash
# Added in 2026 for P3 saturation diagnostics. GPLv2; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
launcher="${OPENGRADS_LAUNCHER:-$repo_root/opengrads}"
test_root="$(mktemp -d /tmp/opengrads-saturation.XXXXXX)"
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
"$test_root/writer" "$test_root"
python3 - "$launcher" "$build_root" "$test_root" "${OPENGRADS_P3_SOURCE:-$repo_root/../VVM/CODE/microphy_p3.F}" <<'PY'
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

launcher,build,directory,p3source=sys.argv[1:]
root=Path(directory)
env=dict(os.environ,OPENGRADS_BUILD_ROOT=build,OPENGRADS_COLOR='0')
water=(6.11239921,.443987641,.142986287e-1,.264847430e-3,.302950461e-5,.206739458e-7,.640689451e-10,-.952447341e-13,-.976195544e-15)
ice=(6.11147274,.503160820,.188439774e-1,.420895665e-3,.615021634e-5,.602588177e-7,.385852041e-9,.146898966e-11,.252751365e-14)
hardcopy='Cairo' if any((Path(build)/('src/.libs/libgxpCairo'+ext)).exists() for ext in ('.so','.dylib')) else 'gxdummy'

def es(t,phase=1):
    if t is None or phase is None or not math.isfinite(t) or not math.isfinite(phase) or t<=0:return None
    frozen=phase>0 and t<273.15
    if t>=(195.8 if frozen else 202):
        coeff=ice if frozen else water
        return 100*math.fsum(c*(t-273.15)**k for k,c in enumerate(coeff))
    if frozen:
        exponent=-9.09718*(273.16/t-1)-3.56654*math.log10(273.16/t)+.876793*(1-t/273.16)+math.log10(6.1071)
    else:
        exponent=-7.90298*(373.16/t-1)+5.02808*math.log10(373.16/t)-1.3816e-7*(10**(11.344*(1-t/373.16))-1)+8.1328e-3*(10**(-3.49149*(373.16/t-1))-1)+math.log10(1013.246)
    return 100*10**exponent

def qvs(t,p,phase=1):
    e=es(t,phase)
    return None if e is None or p is None or not math.isfinite(p) or p<=0 else .622*e/max(.001,p-e)

def run(commands,errors=False):
    p=subprocess.run([launcher,'-bl','-d','gxdummy','-h',hardcopy],input=commands+'\nquit\n',universal_newlines=True,
                     stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=90)
    if p.returncode or (not errors and re.search(r'Error|ERROR|Invalid expression|Unknown command|logic check',p.stdout)):
        raise AssertionError(p.stdout)
    return p.stdout

def grids(text):
    lines=text.splitlines();result=[]
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

def same(actual,wanted,context):
    assert len(actual)==len(wanted),context
    for i,(a,w) in enumerate(zip(actual,wanted)):
        assert a is None if w is None else a is not None and abs(a-w)<=max(2e-10,abs(w)*3e-10),(context,i,a,w)

# Polynomial in power-series form is independent of the C Horner loop. When
# the user's P3 source and a Fortran compiler exist, test the original functions too.
compiler=shlex.split(os.environ.get('FC','gfortran'))
if Path(p3source).is_file() and shutil.which(compiler[0]):
    source=Path(p3source).read_text()
    funcs=[]
    for name in ('polysvp1','qv_sat'):
        m=re.search(r'(?ims)^\s*real function '+name+r'\(.*?^\s*end function '+name+r'\s*$',source)
        assert m,name+' not found in P3 source'
        funcs.append(m.group(0))
    reference=root/'original_p3.F90'
    reference.write_text('module reference_p3\nimplicit none\nreal,parameter :: ep_2=.622\ncontains\n'+
                         '\n'.join(funcs)+'\nend module\nprogram reference\nuse reference_p3\nimplicit none\n'+
                         'real :: t,p\ninteger :: phase,status\ndo\nread(*,*,iostat=status)t,p,phase\n'+
                         'if(status/=0)exit\nwrite(*,\'(2(ES25.16E3,1X))\')polysvp1(t,phase),qv_sat(t,p,phase)\nend do\nend program\n')
    rows=[(t,p,phase) for t in list(range(150,331))+[195.8-1e-5,195.8,195.8+1e-5,202-1e-5,202,202+1e-5,273.15-1e-5,273.15,273.15+1e-5]
          for p in (20000,100000) for phase in (0,1)]
    for double in (True,False):
        executable=root/('original-double' if double else 'original-single')
        flags=['-fdefault-real-8','-fdefault-double-8'] if double else []
        subprocess.check_call(compiler+['-cpp','-ffree-line-length-none']+flags+[str(reference),'-o',str(executable)],cwd=str(root))
        selected=rows if double else [(t,100000,ph) for t in range(150,331,10) for ph in (0,1)]
        output=subprocess.check_output([str(executable)],input=''.join('%s %s %s\n'%row for row in selected),universal_newlines=True)
        maxrelative=0
        for row,line in zip(selected,output.splitlines()):
            t,p,phase=row;got=[float(x) for x in line.split()];wanted=[es(t,phase),qvs(t,p,phase)]
            for a,w in zip(got,wanted):
                relative=abs(a-w)/w;maxrelative=max(maxrelative,relative)
                assert abs(a-w)<=max(2e-10,abs(w)*(3e-10 if double else 1e-2)),(row,a,w)
        assert len(output.splitlines())==len(selected)
        print('Original P3 '+('double' if double else 'single')+' reference passed; maximum relative difference %.3g'%maxrelative)
else:print('Original P3 comparison skipped: set OPENGRADS_P3_SOURCE and FC to enable it.')

config=run('q config')
formats=[('dat','slice.dat','')]
if (root/'slice.nc').exists() and 'netcdf' in config:formats += [('nc4','slice4.nc','netcdf')]
if (root/'slice.h5').exists() and 'hdf5' in config:formats += [('h5','slice.h5','hdf5_grid'),('h5-template','slice%tm1.h5','hdf5_grid')]
if (root/'slice.bp').exists() and 'adios2-bp5' in config:formats += [('bp','slice.bp','bp5'),('bpzstd','slicezstd.bp','bp5')]
for label,dataset,dtype in formats:
  for reverse in (False,True):
    name=label+('-reverse' if reverse else '')
    native='t,z,y,x' if dtype in ('netcdf','hdf5_grid') else 'z,y,x' if dtype=='bp5' else '99'
    sdims='t,y,x' if dtype in ('netcdf','hdf5_grid') else 'y,x' if dtype=='bp5' else '99'
    if '%' in dataset and dtype=='hdf5_grid':native='z,y,x';sdims='y,x'
    options=('template ' if '%' in dataset else '')+('yrev zrev' if reverse else '')
    ctl=root/(name+'.ctl')
    ctl.write_text('dset ^'+dataset+'\n'+('dtype '+dtype+'\n' if dtype else '')+
                   ('options '+options+'\n' if options else '')+'undef -9999\n'+
                   'xdef 16 linear 10 0.1\nydef 8 linear 20 0.2\nzdef 4 levels 50 150 400 1000\n'+
                   'tdef 2 linear 00z01jan2000 1hr\nvars '+('5' if dtype in ('bp5','hdf5_grid') else '4')+'\nf 4 '+native+' Float\ng 4 '+native+' Double\n'+
                   'kmap 0 '+sdims+' Phase or pressure map\nsurface 0 '+sdims+' Pressure map\n'+
                   ('rho 4 z Profile\n' if dtype in ('bp5','hdf5_grid') else '')+'endvars\n')
    prefix='open '+str(ctl)+'\nset gxout print\nset prnopts %.17g 16 1\n'
    commands='';wanted=[]
    for t in range(2):
      for z in range(4):
        commands+='set t '+str(t+1)+'\nset z '+str(z+1)+'\n'
        values=[]
        for y in range(8):
          yy=7-y if reverse else y;zz=3-z if reverse else z
          for x in range(16):
            temp=(1000*t+100*(zz+1)+10*yy+x)*0.1+180
            if zz==0 and yy==2 and x==4:temp=None
            pressure=50000+(900+100*t+10*yy+x)*10
            phase=1 if temp is not None and temp<273.15 else -1
            km=1+(x//2+yy+t)%4
            if yy==0 and x<6:km=(0,5,1.5,None,None,None)[x]
            values.append((temp,pressure,phase,km))
        expressions=['es(f*0.1+180)','es(f*0.1+180,0)','es(f*0.1+180,1)','es(f*0.1+180,f*0.1+180<273.15)',
                     'qvs(f*0.1+180,50000+surface*10)','qvs(f*0.1+180,50000+surface*10,f*0.1+180<273.15)',
                     'qvs(f*0.1+180,100000,1)','qvs(250,50000+surface*10,1)','es(250,kmap)',
                     'qvs(f*0.1+180,kmap*10000,1)','qvs(250,50000+surface*10,kmap)',
                     'es(f*0.1+180,auto)','qvs(f*0.1+180,50000+surface*10,auto)']
        commands+=''.join('d '+expr+'\n' for expr in expressions)
        funcs=[lambda a:es(a[0]),lambda a:es(a[0],0),lambda a:es(a[0],1),lambda a:es(a[0],a[2]),
               lambda a:qvs(a[0],a[1]),lambda a:qvs(a[0],a[1],a[2]),lambda a:qvs(a[0],100000,1),
               lambda a:qvs(250,a[1],1),lambda a:es(250,a[3]),
               lambda a:qvs(a[0],None if a[3] is None else a[3]*10000,1),lambda a:qvs(250,a[1],a[3]),lambda a:es(a[0]),lambda a:qvs(a[0],a[1])]
        wanted += [[fn(v) for v in values] for fn in funcs]
    out=run(prefix+commands);actual=grids(out)
    assert len(actual)==len(wanted),(name,len(actual),len(wanted))
    for i,(a,w) in enumerate(zip(actual,wanted)):same(a,w,name+' grid '+str(i))
    # One-axis results from mean exercise expansion along each coordinate axis.
    out=run(prefix+'set t 1\nset z 2\n'+
            'define tt=f*0.1+180\ndefine pp=50000+surface*10\n'+
            'd qvs(mean(tt,y=1,y=8),pp,1)\nd es(tt,mean(surface,x=1,x=16)>940)\n')
    yyvalues=list(reversed(range(8))) if reverse else list(range(8));zz=2 if reverse else 1
    expected=[];expectedphase=[]
    for y,yy in enumerate(yyvalues):
      for x in range(16):
        avgtemp=(100*(zz+1)+35+x)*0.1+180
        expected.append(qvs(avgtemp,50000+(900+10*yy+x)*10,1))
        expectedphase.append(es((100*(zz+1)+10*yy+x)*0.1+180,1 if yy>=4 else -1))
    same(grids(out)[0],expected,name+' Y expansion');same(grids(out)[1],expectedphase,name+' X expansion')
    if dtype in ('bp5','hdf5_grid'):
      out=run(prefix+'set y 4\nset z 1 4\n'+
              'd qvs(f*0.1+180,50000*rho,1)\n'+
              'd qvs(f*0.1+180,mean(50000*rho,x=1,x=16),f*0.1+180<273.15)\n')
      expected=[];yy=4 if reverse else 3
      for z in range(4):
        zz=3-z if reverse else z
        for x in range(16):expected.append(qvs((100*(zz+1)+10*yy+x)*0.1+180,50000*(1.2,1.1,1.,.9)[zz],1))
      same(grids(out)[0],expected,name+' Z profile');same(grids(out)[1],expected,name+' Z expansion')
    # Define a volume and select a level again; direct display must agree.
    out=run(prefix+'set z 1 4\ndefine cube=qvs(f*0.1+180,100000,1)\nset z 3\n'+
            'd cube\nd qvs(f*0.1+180,100000,1)\n')
    a=grids(out);same(a[0],a[1],name+' volume define')
    if not reverse and hardcopy=='Cairo':
      a,b=root/(label+'-direct.png'),root/(label+'-defined.png')
      run('open '+str(ctl)+'\nset t 2\nset grads off\nset timelab off\nset gxout shaded\n'+
          'd qvs(f*0.1+180,50000+surface*10,1)\nprintim '+str(a)+' x640 y480\nclear canvas\n'+
          'define qs=qvs(f*0.1+180,50000+surface*10,1)\nd qs\nprintim '+str(b)+' x640 y480\n')
      assert a.read_bytes()==b.read_bytes(),name+' display/define PNG mismatch'
    print('Saturation passed: '+name)

prefix='open '+str(root/'dat.ctl')+'\nset gxout print\nset prnopts %.17g 16 1\n'
commands='';wanted=[]
for t in [150,190,195.8-1e-5,195.8,195.8+1e-5,202-1e-5,202,202+1e-5,250,273.15-1e-5,273.15,273.15+1e-5,300,330]:
  commands+='d es(%s)\nd es(%s,auto)\nd qvs(%s,100000)\nd qvs(%s,100000,auto)\n'%(t,t,t,t)
  wanted += [[es(t)],[es(t)],[qvs(t,100000)],[qvs(t,100000)]]
  for phase in (0,1,-1,.5):
    commands+='d es(%s,%s)\nd qvs(%s,100000,%s)\n'%(t,phase,t,phase)
    wanted += [[es(t,phase)],[qvs(t,100000,phase)]]
for expr,value in [('es(0)',None),('es(-1)',None),('qvs(300,0)',None),('qvs(300,-1)',None),
                   ('qvs(300,100)',qvs(300,100)),('es(250,maskout(1,-1))',None),
                   ('qvs(250,maskout(1,-1))',None),('qvs(250,100000,maskout(1,-1))',None)]:
  commands+='d '+expr+'\n';wanted.append([value])
actual=grids(run(prefix+commands));assert len(actual)==len(wanted)
for i,(a,w) in enumerate(zip(actual,wanted)):same(a,w,'scalar case '+str(i))
# Reject incompatible shared axes and malformed/unknown arguments cleanly.
different=root/'different.ctl';different.write_text((root/'dat.ctl').read_text().replace('xdef 16 linear 10 0.1','xdef 16 linear 10 0.2'))
out=run(prefix+'open '+str(different)+'\nd qvs(f.1*0.1+180,surface.2)\n'+
        'd es(f.1*0.1+180,kmap.2)\nd qvs(250,surface.1,kmap.2)\n'+
        'd es()\nd es(250,0,1)\nd qvs(250)\nd qvs(250,100000,0,1)\n'+
        'd es(250,unknown)\nd qvs(250,unknown)\n',errors=True)
assert out.count('matching shared coordinate axes')==3,out
assert 'ES error:' in out and 'QVS error:' in out,out
print('Saturation regression passed: P3 branches, default/explicit auto, conditional phases, scalar/axis expansion, missing/invalid inputs, pressure guard, volumes, display/define, and compressed readers.')
PY
