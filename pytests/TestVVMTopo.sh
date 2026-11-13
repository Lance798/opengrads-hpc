#!/usr/bin/env bash
# Numeric terrain envelopes and command lookup in the bundled script library.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-vvmtopo.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
python3 -u - "$repo_root" "$test_root" <<'PY'
import os,random,re,struct,subprocess,sys
from pathlib import Path
repo,root=map(Path,sys.argv[1:])
launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off',GA_TIME_TRACE='1')
env.pop('GASCRP',None)

def grids(text):
    out=[];lines=iter(text.splitlines())
    for line in lines:
        match=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not match:continue
        n=int(match[1]);values=[]
        while len(values)<n:values.extend(map(float,next(lines).split()))
        assert len(values)==n
        out.append(values)
    return out

def execute(body,errors=False):
    p=subprocess.run([launcher,'-blu','-d','gxdummy','-h','gxdummy'],
        input=body+'quit\n',universal_newlines=True,cwd=root,env=env,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    assert p.returncode==0,p.stdout[-5000:]
    if not errors:
        assert not re.search(r'Error|ERROR|Unknown command|Syntax Error',p.stdout),p.stdout[-5000:]
    assert 'Time workers:' not in p.stdout
    return p.stdout

def run(ctl,command,n=4):
    setup=(f'open {ctl}\nset threads {n}\nset gxout print\n'
           'set prnopts %.17g 32 1\nset x 1\nset y 1\n') if ctl else ''
    body=setup+command+'\n'
    if ctl:
        # vvmtopo restores the caller's point selection; inspect the full
        # stored fields explicitly for the analytic numeric oracle below.
        descriptor=ctl.read_text()
        for axis in ('x','y'):
            size=re.search(r'^'+axis+r'def (\d+)',descriptor,re.M).group(1)
            body+='set '+axis+' 1 '+size+'\n'
        body+='\n'.join('d '+f for f in ('topou','topov','topow','toposrc','topo'))+'\n'
    return execute(body)

assert 'Usage: vvmtopo' in run(None,'vvmtopo')
rng=random.Random(1987)
cases=[('small',4,3,[0,4,2,0,7,1,3,8,0,5,0,1],False),
       ('x',7,1,[0,6,-3,2,0,1,-1],False),
       ('y',1,7,[3,0,-2,7,0,1,2],False),
       ('scalar',1,1,[0],False),
       ('random',31,19,[rng.randint(-8,150) for _ in range(31*19)],False),
       ('fraction',13,11,[rng.randint(-100,100)/8 for _ in range(13*11)],False),
       ('cyclic',17,9,[rng.randint(0,100) for _ in range(17*9)],True)]
commands=[('vvmtopo topo',1,1),('run vvmtopo topo',1,1),
          ('vvmtopo topo 0',0,1),('vvmtopo topo 0 0',0,0),
          ('vvmtopo topo 1 0',1,0),('vvmtopo topo 0 1',0,1),
          ('vvmtopo topo 1 1',1,1),('run vvmtopo.gs topo 0 0',0,0)]
count=0
for name,nx,ny,data,cyclic in cases:
    ctl=root/(name+'.ctl')
    ctl.write_text('dset ^'+name+'.dat\nundef -9999\nxdef '+str(nx)+' linear '+
        ('0 '+str(360/nx) if cyclic else '10 .1')+'\nydef '+str(ny)+
        ' linear -20 .2\nzdef 1 linear 1 1\ntdef 1 linear 00z01jan2000 1hr\n'
        'vars 1\ntopo 0 99 Terrain\nendvars\n')
    (root/(name+'.dat')).write_bytes(struct.pack('='+str(len(data))+'f',*data))
    corrected=[1 if h==0 else h for h in data]
    for command,px,py in commands:
        u=[max(corrected[y*nx+x],corrected[y*nx+((x+1)%nx if px else min(x+1,nx-1))])
           for y in range(ny) for x in range(nx)]
        v=[max(corrected[y*nx+x],corrected[((y+1)%ny if py else min(y+1,ny-1))*nx+x])
           for y in range(ny) for x in range(nx)]
        expected=[u,v,corrected,corrected,data]
        for n in (1,4):
            text=run(ctl,command,n)
            assert f'periodic_x={px} periodic_y={py}' in text,text[-2000:]
            assert grids(text)==expected,(name,command,n,grids(text),expected)
            count+=sum(map(len,expected))
    print('vvmtopo analytic/defaults/library lookup passed:',name,flush=True)
# Periodic neighbors are copied without subtract/add reconstruction, even
# when neighboring finite heights have very different magnitudes.
wide=[-1e30,1.,1e30,0.,2.,-1e30,4.,1.,1e30,3.,-1e30,2.]
wide=list(struct.unpack('=12f',struct.pack('=12f',*wide)))
ctl=root/'wide.ctl';ctl.write_text((root/'small.ctl').read_text().replace('^small.dat','^wide.dat'))
(root/'wide.dat').write_bytes(struct.pack('=12f',*wide))
c=[1. if h==0 else h for h in wide]
u=[max(c[y*4+x],c[y*4+(x+1)%4]) for y in range(3) for x in range(4)]
v=[max(c[y*4+x],c[((y+1)%3)*4+x]) for y in range(3) for x in range(4)]
for n in (1,4):
    assert grids(run(ctl,'vvmtopo topo',n))==[u,v,c,c,wide]
# Calls from another .gs script use the same installed command lookup.
driver=root/'caller.gs';driver.write_text("'vvmtopo topo'\nsay result\n")
text=run(root/'small.ctl','run '+str(driver))
assert 'periodic_x=1 periodic_y=1' in text
assert grids(text)==grids(run(root/'small.ctl','vvmtopo topo'))

# The caller's dimension environment survives success and calculation errors.
# A real multi-Z/T/E fixture also allows a zslice after restoration, with
# subdomain edge neighbors coming from the full stored terrain grid.
ctl=root/'dims.ctl'
ctl.write_text('dset ^dims.dat\nundef -9999\nxdef 4 linear 10 .1\n'
               'ydef 3 linear -20 .2\nzdef 8 linear 1 1\n'
               'tdef 2 linear 00z01jan2000 1hr\nedef 2 names a b\n'
               'vars 2\ntopo 0 99 Terrain\nu 8 99 Wind\nendvars\n')
terrain=[0,4,2,0,7,1,3,8,0,5,0,1]
values=[]
for e in range(2):
    for t in range(2):
        values.extend(terrain)
        for z in range(1,9):
            values.extend([1000*e+100*t+z]*12)
(root/'dims.dat').write_bytes(struct.pack('='+str(len(values))+'f',*values))

def dimensions(text):
    return re.findall(r'^[XYZTE] is (?:fixed|varying).*$',text,re.M)

selections=['set x 2 3\nset y 1 2\nset z 3\nset t 2\nset e 2\n',
            'set x 2\nset y 2\nset z 2 6\nset t 1 2\nset e 1 2\n',
            'set x 1.5 3.5\nset y 1.25 2.75\nset z 2.5\nset t 2\nset e 1\n']
for selection in selections:
    for command in ('vvmtopo topo','vvmtopo topo 0 0','vvmtopo nonexistent'):
        text=execute(f'open {ctl}\n'+selection+'q dims\n'+command+'\nq dims\n',
                     errors='nonexistent' in command)
        dims=dimensions(text)
        assert len(dims)==10 and dims[:5]==dims[5:],text
        if 'nonexistent' in command:
            assert 'Error: could not define toposrc' in text,text
        else:
            assert 'Created topou, topov, and topow' in text,text

text=execute(f'open {ctl}\nset gxout print\nset prnopts %.17g 32 1\n'+
             selections[0]+'vvmtopo topo 0 0\n'+
             'd topou\nd topov\nd topow\nd zslice(u,topou)\n'+
             'set x 1 4\nset y 1 3\nd topow\n')
assert grids(text)==[[4,2,3,8],[4,3,5,3],[4,2,1,3],
                     [1104,1102,1103,1108],[1 if h==0 else h for h in terrain]],text
print('VVMTopo dimension restoration passed: fixed/varying/fractional X/Y/Z/T/E, '
      'calculation errors, full stored domain, subdomain neighbors, zslice.')
print(f'VVMTopo passed: {count} analytic values; periodic defaults/overrides, '
      'zero correction, source preservation, degenerate/cyclic grids, '
      'bare/run/nested lookup outside repository, 1/4 threads.')
PY
