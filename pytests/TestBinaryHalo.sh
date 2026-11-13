#!/usr/bin/env bash
# Analytic stream-binary halo, byte-order and reduction checks. GPLv2.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "${OPENGRADS_LAUNCHER:-$repo_root/opengrads}" "${OPENGRADS_BUILD_ROOT:-$repo_root/build}" <<'PY'
import array,math,os,re,subprocess,sys,tempfile
from pathlib import Path
launcher,build=sys.argv[1:]; nx=ny=256; count=0; maximum=0.0
with tempfile.TemporaryDirectory(prefix='opengrads-binary-halo.') as directory:
    root=Path(directory)
    data=array.array('f',(-9999 if x==0 and y==0 else 1000*t+100*z+10*y+x
        for t in range(3) for z in range(1,5) for y in range(ny) for x in range(nx)))
    (root/'native.dat').write_bytes(data.tobytes()); data.byteswap()
    (root/'swapped.dat').write_bytes(data.tobytes())
    field=[None if not (1<=x<=nx and 1<=y<=ny) or (x==1 and y==1)
           else 1300+10*(y-1)+x-1 for y in range(258) for x in range(258)]
    integral=[0 if x==0 and y==0 else 1300*(1000+10*y+x)+412500
              for y in range(ny) for x in range(nx)]
    wy=[abs(math.sin(math.radians(-60+.25*y+.125))-
            math.sin(math.radians(-60+.25*y-.125))) for y in range(ny)]
    profile=[]
    for t in range(3):
        for z in range(1,5):
            base=1000*t+100*z
            total=sum(w*(nx*(base+10*y)+nx*(nx-1)/2) for y,w in enumerate(wy))-wy[0]*base
            profile.append(total/(nx*sum(wy)-wy[0]))
    expected=[field,integral,[None]*(258*4),profile]
    commands=('set t 2\nset z 3\nset x 0 257\nset y 0 257\nd f\n'
              'set z 1\nset x 1 256\nset y 1 256\nd zint(f)\n'
              'set x 0 257\nset y -5 -2\nd f\n'
              'set x 1\nset y 1\nset z 1 4\nset t 1 3\n'
              'define profile=aave(f,x=1,x=256,y=1,y=256)\nd profile\nquit\n')
    for endian in ('native','swapped'):
        ctl=root/(endian+'.ctl')
        ctl.write_text('dset ^'+endian+'.dat\n'+('options byteswapped\n' if endian=='swapped' else '')+
            'undef -9999\nxdef 256 linear 10 .1\nydef 256 linear -60 .25\n'
            'zdef 4 levels 50 150 400 1000\ntdef 3 linear 00z01jan2000 1hr\nvars 1\nf 4 99 Field\nendvars\n')
        for threads in (1,4):
            env=dict(os.environ,OPENGRADS_BUILD_ROOT=build,GA_PROGRESS='off',OPENGRADS_COLOR='0')
            result=subprocess.run([launcher,'-bl','-d','gxdummy','-h','gxdummy'],
                input='open '+str(ctl)+'\nset threads '+str(threads)+'\nset gxout print\nset prnopts %.17g 32 1\n'+commands,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,universal_newlines=True,env=env,timeout=90)
            assert result.returncode==0 and not re.search('Error|ERROR|Invalid expression',result.stdout),result.stdout[-2000:]
            lines=result.stdout.splitlines(); actual=[]
            for i,line in enumerate(lines):
                match=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
                if not match:continue
                n=int(match[1]); missing=float(match[2]); values=[]
                for row in lines[i+1:]:
                    if len(values)>=n:break
                    values.extend(float(v) for v in row.split())
                assert len(values)==n
                actual.append([None if v==missing else v for v in values])
            assert len(actual)==4,(endian,threads,len(actual))
            for grid,(got,wanted) in enumerate(zip(actual,expected)):
                assert len(got)==len(wanted)
                for value,reference in zip(got,wanted):
                    assert (value is None)==(reference is None),(endian,threads,grid,'mask')
                    if value is None:continue
                    error=abs(value-reference); maximum=max(maximum,error); count+=1
                    assert value==reference if grid<3 else error<=1e-9+abs(reference)*2e-12,(grid,value,reference)
            print('Binary halo passed:',endian,threads,flush=True)
print('Binary halo analytic checks passed: values=%d max_abs=%.17g'%(count,maximum))
PY
