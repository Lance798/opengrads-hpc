#!/usr/bin/env bash
# IF owns independent condition values for scalar, profile and plane branches.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 - "$repo_root" <<'PY'
import math,os,re,struct,subprocess,sys,tempfile
from pathlib import Path
repo=Path(sys.argv[1]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off')
values=[-3.,-2.,-1.,0.,1.,2.,3.,None]
cases=[('if(f>0,7,-3)',[-3.,-3.,-3.,-3.,7.,7.,7.,None]),
       ('if(f>0,f,-f)',[3.,2.,1.,0.,1.,2.,3.,None]),
       ('if(f>0,3,f)',[-3.,-2.,-1.,0.,3.,3.,3.,None]),
       ('if(f>0,f,-4)',[-4.,-4.,-4.,-4.,1.,2.,3.,None]),
       ('if(1,f,-4)',values),('if(-1,f,-4)',[-4.]*7+[None]),
       ('if(1,7,f)',[7.]*7+[None]),('if(-1,f,7)',[7.]*7+[None]),
       ('if(if(f>0,-1,1),10,20)',[10.,10.,10.,10.,20.,20.,20.,None])]
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
count=0
with tempfile.TemporaryDirectory(prefix='opengrads-if.') as directory:
    root=Path(directory);(root/'field.dat').write_bytes(struct.pack('=8f',*[v if v is not None else -9999 for v in values]))
    for name,nx,ny,nz in (('x',8,1,1),('xy',4,2,1),('z',1,1,8)):
        ctl=root/(name+'.ctl');ctl.write_text('dset ^field.dat\nundef -9999\nxdef '+str(nx)+' linear 10 .1\nydef '+str(ny)+' linear -20 .2\nzdef '+(str(nz)+' levels 1000 900 800 650 500 300 200 100' if nz>1 else '1 linear 1000 1')+'\ntdef 1 linear 00z01jan2000 1hr\nvars 1\nf '+str(nz)+' 99 Field\nendvars\n')
        setup=f'open {ctl}\nset x 1 {nx}\nset y 1 {ny}\nset z 1 {nz}\nset gxout print\nset prnopts %.17g 32 1\n'
        for n in (1,4):
            body=setup+f'set threads {n}\n'+''.join('d '+expr+'\n' for expr,wanted in cases)+'d if(1,7,9)\nd if(-1,7,9)\nquit\n'
            p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input=body,universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=60)
            assert p.returncode==0 and not re.search(r'Error|ERROR|Invalid expression|AddressSanitizer|runtime error',p.stdout),p.stdout
            actual=grids(p.stdout);wanted=[w for expr,w in cases]+[[7.],[9.]]
            assert actual==wanted,(name,n,actual,wanted)
            count+=sum(map(len,actual))
        # A failed branch must release temporary grids without poisoning reuse.
        p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input=setup+'d if(f>0,f,notavariable)\nd if(f>0,f,-f)\nquit\n',universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=60)
        assert p.returncode==0 and 'Error' in p.stdout,p.stdout
        assert grids(p.stdout)==[[3.,2.,1.,0.,1.,2.,3.,None]],p.stdout
print(f'IF regression passed: {count} analytic scalar/X/Z/XY values; masks, scalar branches, nested conditions and error recovery; 1/4 threads.')
PY
