#!/usr/bin/env bash
# Circular X/Y shifts preserve values, masks and the evaluated grid extent.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-roll.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
python3 -u - "$repo_root" "$test_root" <<'PY'
import json,math,os,re,struct,subprocess,sys
from pathlib import Path
repo,root=map(Path,sys.argv[1:]);launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off')
nx,ny,nz,nt=13,7,3,3
values=[float(t*10000+z*1000+y*20+x)/8 for t in range(nt) for z in range(nz) for y in range(ny) for x in range(nx)]
values[:8]=[0.,-0.,-9999.,float('nan'),float('inf'),-float('inf'),-3.,5.]
(root/'f.dat').write_bytes(struct.pack('='+str(len(values))+'f',*values))
header=f'undef -9999\nxdef {nx} linear 10 .1\nydef {ny} linear -20 .25\nzdef {nz} levels 1000 700 300\ntdef {nt} linear 00z01jan2000 1hr\nvars 1\nf {nz} 99 Field\nendvars\n'
ctl=root/'f.ctl';ctl.write_text('dset ^f.dat\n'+header)

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

def same(a,b):
    assert len(a)==len(b),(len(a),len(b))
    for x,y in zip(a,b):
        if x is None or y is None:assert x is y,(x,y)
        elif math.isnan(x) or math.isnan(y):assert math.isnan(x) and math.isnan(y),(x,y)
        else:assert struct.pack('=d',x)==struct.pack('=d',y),(x,y)

def run(body,n=4,path=ctl,error=False):
    text=f'open {path}\nset threads {n}\nset gxout print\nset prnopts %.17g 32 1\n'+body+'\nquit\n'
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],input=text,universal_newlines=True,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=90)
    assert p.returncode==0,p.stdout[-3000:]
    assert bool(re.search(r'Error|ERROR|ROLL error|AddressSanitizer|runtime error',p.stdout))==error,p.stdout[-4000:]
    return grids(p.stdout)

def expected(data,shape,axis,shift):
    if axis is None:return data[:]
    rows,cols=shape;length=cols if axis==1 else rows
    return [data[(y*cols+(x-shift)%cols) if axis==1 else ((y-shift)%rows)*cols+x]
            for y in range(rows) for x in range(cols)]

requests=[];samples=0
shifts=[0,1,-1,2,-3,13,-13,14,-14,2**63-1,-2**63,2**80+17,-(10**150+7)]
cases=[('xy','set x 1 13\nset y 1 7\nset z 1\nset t 1',(7,13),{'x':1,'y':0}),
       ('crop','set x 2 8\nset y 2 5\nset z 2\nset t 1',(4,7),{'x':1,'y':0}),
       ('x','set x 1 13\nset y 3\nset z 1\nset t 1',(1,13),{'x':1,'y':None}),
       ('y','set x 5\nset y 1 7\nset z 1\nset t 1',(7,1),{'x':None,'y':0}),
       ('xz','set x 2 8\nset y 3\nset z 1 3\nset t 1',(3,7),{'x':1,'y':None}),
       ('yz','set x 5\nset y 1 7\nset z 1 3\nset t 1',(3,7),{'x':None,'y':1}),
       ('xt','set x 2 8\nset y 3\nset z 1\nset t 1 3',(3,7),{'x':1,'y':None}),
       ('yt','set x 5\nset y 1 7\nset z 1\nset t 1 3',(3,7),{'x':None,'y':1}),
       ('zt','set x 5\nset y 3\nset z 1 3\nset t 1 3',(3,3),{'x':None,'y':None}),
       ('scalar','set x 1\nset y 1\nset z 1\nset t 1',(1,1),{'x':None,'y':None})]
for name,setup,shape,axes in cases:
    base=run(setup+'\nd f',1)[0]
    specs=[(axis,s) for axis in ('x','y') for s in shifts]
    wanted=[expected(base,shape,axes[axis],s) for axis,s in specs]
    for axis,s in specs:
        requests.append(dict(data=base,shape=shape,axis=axes[axis],shift=str(s)))
    command=setup+'\n'+''.join(f'd roll(f,{s},{axis})\n' for axis,s in specs)+'d roll(roll(f,-2,x),3,y)\nd f\n'
    nested=expected(expected(base,shape,axes['x'],-2),shape,axes['y'],3)
    for n in (1,4,8):
        actual=run(command,n);assert len(actual)==len(wanted)+2
        for a,b in zip(actual,wanted+[nested,base]):same(a,b);samples+=len(a)
    print('ROLL analytic values/masks/extent passed:',name,flush=True)
# Coordinates and arithmetic still align with the unshifted grid metadata.
setup=cases[1][1];data=run(setup+'\nd f\nd lon\nd lat',1)
expected_grid=expected(data[0],(4,7),1,-2)
for coordinate,base_coordinate in zip(('lon','lat'),data[1:]):
    same(run(setup+'\nd roll(f,-2,x)+'+coordinate)[0],[a+b for a,b in zip(expected_grid,base_coordinate)])
# No subtraction/addition reconstruction, including extreme finite doubles.
setup=cases[1][1];base=run(setup+'\nd if(f>150,1e300,-1e300)',1)[0]
same(run(setup+'\nd roll(if(f>150,1e300,-1e300),1,x)')[0],expected(base,(4,7),1,1))
same(run('d roll(7,123456789012345678901234567890,x)')[0],[7.])
# Invalid syntax must fail before affecting subsequent valid evaluations.
for bad in ('roll(f,1)','roll(f,1,x,2)','roll(f,1,z)','roll(f,1,t)',
            'roll(f,1,xy)','roll(f,1,xp)','roll(f,1.5,x)','roll(f,1.0,x)',
            'roll(f,1e2,x)','roll(f,1+1,x)','roll(f,nan,x)'):
    a=run(cases[1][1]+'\nd '+bad+'\nd roll(f,-1,x)',error=True)
    assert len(a)==1,bad;same(a[0],expected(run(cases[1][1]+'\nd f',1)[0],(4,7),1,-1))
# Exercise parallel row copies and compare every exported float32 bit,
# including missing values, signed zero and nonfinite values.
large=root/'large.ctl';large.write_text('dset ^large.dat\n'+header.replace('xdef 13','xdef 513').replace('ydef 7','ydef 129').replace('zdef 3 levels 1000 700 300','zdef 1 linear 1000 1').replace('tdef 3','tdef 1').replace('f 3 99','f 1 99'))
raw=[float((i*37)%8192)/16 for i in range(513*129)];raw[:8]=values[:8]
(root/'large.dat').write_bytes(struct.pack('='+str(len(raw))+'f',*raw))
large_specs=[('f',None,0),('roll(f,1,x)',1,1),('roll(f,-19,x)',1,-19),('roll(f,7,y)',0,7),('roll(f,-130,y)',0,-130)]
reference=None
for n in (1,4,8):
    body='set x 1 513\nset y 1 129\nset z 1\nset t 1\n'
    paths=[]
    for i,(expr,axis,shift) in enumerate(large_specs):
        path=root/f'large-{n}-{i}.bin';paths.append(path)
        body+=f'set gxout fwrite\nset fwrite -le {path}\nd {expr}\ndisable fwrite\n'
    run(body,n,large)
    native=paths[0].read_bytes();assert len(native)==513*129*4
    words=[native[i:i+4] for i in range(0,len(native),4)]
    for path,(expr,axis,shift) in zip(paths,large_specs):
        assert path.read_bytes()==b''.join(expected(words,(129,513),axis,shift)),(n,expr)
    if reference is None:reference=native
    else:assert native==reference
# Optional independent NumPy oracle, including separate mask movement.
if os.environ.get('GA_TEST_NUMPY_PYTHON'):
    code='''import json,sys,numpy as np
req=json.load(sys.stdin);out=[]
for r in req:
 a=np.asarray(r['data'],dtype=object).reshape(r['shape'])
 axis=r['axis'];b=a if axis is None else np.roll(a,int(r['shift']),axis=axis)
 out.append(b.ravel().tolist())
json.dump(out,sys.stdout)
'''
    p=subprocess.run([os.environ['GA_TEST_NUMPY_PYTHON'],'-c',code],input=json.dumps(requests),universal_newlines=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
    assert p.returncode==0,p.stderr
    for r,a in zip(requests,json.loads(p.stdout)):
        same(a,expected(r['data'],r['shape'],r['axis'],int(r['shift'])))
    print('NumPy oracle passed:',len(requests),'value/mask permutations',flush=True)
print(f'ROLL passed: {samples} analytic values, masks/nonfinite/signed-zero, '
      'selected extent, metadata, nesting, arbitrary integer shifts, error recovery; '
      '1/4/8 threads and complete parallel-grid float32 bit comparisons.')
PY
