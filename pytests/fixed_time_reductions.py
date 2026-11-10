# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
import json, math, os, re, struct, subprocess, sys
from pathlib import Path
repo,work=map(Path,sys.argv[1:]);work.mkdir(exist_ok=True)
launcher=os.environ.get('OPENGRADS_LAUNCHER',str(repo/'opengrads'))
reference=os.environ.get('GA_SPATIAL_REFERENCE',launcher)
fmt=os.environ.get('GA_TEST_TIME_FORMAT','binary')
if fmt=='binary':
    nx,ny,nz,nt=17,13,5,3
    data=[(x-8)*.125+(y-6)*.25+z*.5+t for t in range(nt) for z in range(nz) for y in range(ny) for x in range(nx)]
    for i,v in enumerate(data):
        if i%53==0:data[i]=-9999.
    data[4:10]=[0.,-0.,float('nan'),float('inf'),-float('inf'),-3.]
    header='undef -9999\nxdef 17 linear 10 .1\nydef 13 linear -30 .2\nzdef 5 levels 50 150 400 1000 2000\ntdef 3 linear 00z01jan2000 1hr\nvars 1\nf 5 99 Field\nendvars\n'
    for j in (1,2):
        values=data if j==1 else [x if x==-9999 else x*.25+1 for x in data]
        (work/('f%d.dat'%j)).write_bytes(struct.pack('='+str(len(values))+'f',*values))
        (work/('f%d.ctl'%j)).write_text('dset ^f%d.dat\n'%j+header)
else:
    nx,ny,nz,nt=17,13,4,3
    ext='bp' if fmt=='bp5' else 'h5' if fmt=='h5' else 'nc'
    dtype='bp5' if fmt=='bp5' else 'hdf5_grid' if fmt=='h5' else 'netcdf'
    datafile='slice'+('_compressed' if fmt=='nc4' else '')+'.'+ext
    dims='z,y,x' if fmt=='bp5' else 't,z,y,x'
    for j in (1,2):
        field='f' if j==1 else 'g=>f'
        (work/('f%d.ctl'%j)).write_text('dset ^'+datafile+'\ndtype '+dtype+'\nundef -9999\nxdef 17 linear 10 .1\nydef 13 linear -30 .2\nzdef 4 levels 50 150 400 1000\ntdef 3 linear 00z01jan2000 1hr\nvars 1\n'+field+' 4 '+dims+' Field\nendvars\n')
expressions=['f*f','f+f.2*2','abs(f)','sqrt(abs(f))','exp(f/10)','log(abs(f)+1)',
 'log10(abs(f)+1)','sin(f)','cos(f)','tan(f/10)','asin(sin(f))','acos(cos(f))',
 'pow(f,2)','mag(f,f.2)','atan2(f,f.2)','maskout(f,f-1)','if(f>0,f,f.2)',
 'const(f,2)','const(f,2,-u)','const(f,2,-a)','esat(280+f)',
 'esat(280+f,auto)','esat(280+f,0)','qvsat(280+f,90000)',
 'qvsat(280+f,90000,auto)','qvsat(280+f,90000,0)']
lines=['open '+str(work/'f1.ctl'),'open '+str(work/'f2.ctl'),'set t 1','set gxout print','set prnopts %.17g 32 1']
labels=[]
def add(label,commands):
    labels.extend(label for command in commands if command.startswith('d '))
    lines.extend(commands)
for expr in expressions:
    for axis,size in [('x',nx),('y',ny),('z',nz)]:
        for fn in ('ave','mean','sum','sumg','min','max','minloc','maxloc'):
            setup=['set x 2 16','set y 2 12','set z 2']
            if axis=='x':setup[0]='set x 1'
            if axis=='y':setup[1]='set y 1'
            add((expr,axis,fn),setup+['d '+fn+'('+expr+','+axis+'=1,'+axis+'='+str(size)+')'])
    for fn in ('mean','maxloc'):
        add((expr,'direct_xz',fn),['set x 2 16','set y 1','set z 1 5','d '+fn+'('+expr+',y=1,y=13)'])
        add((expr,'define_xz',fn),['define answer='+fn+'('+expr+',y=1,y=13)','d answer','undefine answer'])
    for fn in ('aave','amean','asum','asumg','atot','amin','amax','aminlocx','aminlocy','amaxlocx','amaxlocy'):
        add((expr,'profile',fn),['set x 1','set y 1','set z 1 5',
                'define answer='+fn+'('+expr+',x=1,x=17,y=1,y=13)','d answer','undefine answer'])
# Subsets, scalar Z profiles, fractional bounds, nonuniform/decreasing Z,
# negative shifts/neighborhoods and nested reductions must remain correct.
for expr in ['f*f','f.2*f','roll(f,2,x)','cdiff(f,x)','smth9(f)','mean(f,x=1,x=17)']:
    add((expr,'fallback_or_subset'),['set x 2 16','set y 1','set z 2 4',
      'd ave('+expr+',y=1.3,y=12.7,-b)','define answer=ave('+expr+',y=1.3,y=12.7,-b)','d answer','undefine answer',
      'set x 1']+([] if expr.startswith('cdiff') else ['d minloc('+expr+',x=1,x=17)']))
add(('relative_bounds'),['set dfile 1','set x 2 16','set y 5','set z 1 5',
    'd mean(f*f,y-1,y+1)','define answer=mean(f*f,y-1,y+1)','d answer','undefine answer',
    'set x 5','define answer=aave(f*f,x-2,x+2,y-2,y+2)','d answer','undefine answer'])
if fmt=='binary':
    short=[]
    for t in range(nt):short.extend(data[t*nz*ny*nx:(t*nz+3)*ny*nx])
    (work/'short.dat').write_bytes(struct.pack('='+str(len(short))+'f',*short))
    (work/'short.ctl').write_text('dset ^short.dat\n'+header.replace('f 5 99','f 3 99'))
    profile=[1.+z*.1+t*.01 for t in range(nt) for z in range(nz)]
    (work/'profile.dat').write_bytes(struct.pack('='+str(len(profile))+'f',*profile))
    (work/'profile.ctl').write_text('dset ^profile.dat\n'+header.replace('xdef 17','xdef 1').replace('ydef 13','ydef 1'))
    named=[]
    for t in range(nt):
        for name in ('f','u','a','auto','lon','lat','lev'):named.extend(data[t*nz*ny*nx:(t+1)*nz*ny*nx])
    (work/'names.dat').write_bytes(struct.pack('='+str(len(named))+'f',*named))
    (work/'names.ctl').write_text('dset ^names.dat\n'+header.replace('vars 1','vars 7').replace('f 5 99 Field','f 5 99 Field\nu 5 99 U\na 5 99 A\nauto 5 99 Auto\nlon 5 99 Lon\nlat 5 99 Lat\nlev 5 99 Lev'))
    lines.extend(['open '+str(work/'short.ctl'),'open '+str(work/'profile.ctl'),'open '+str(work/'names.ctl')])
    for expr in ('f*f.3','f*f.4','const(f*f.4,2,-u)'):
        add((expr,'short_or_profile'),['set dfile 1','set t 1','set x 2 16','set y 1','set z 1 5',
            'd mean('+expr+',y=1,y=13)','define answer=mean('+expr+',y=1,y=13)','d answer','undefine answer',
            'set y 2 12','set z 1','d mean('+expr+',z=1,z=5)'])
    add(('literal_shadowing'),['set dfile 5','set x 2 16','set y 1','set z 1 5',
        'define ga_sp0_0=const(f,9)','define ga_spout0=const(f,7)',
        'define answer=mean(const(f,2,-u)+mag(f,-u)+esat(280+f,auto),y=1,y=13)',
        'd answer','undefine answer','define answer=mean(f+ga_sp0_0+ga_spout0,y=1,y=13)',
        'd answer','d ga_sp0_0','d ga_spout0','undefine answer',
        'define answer=mean(f+lon+lat+lev,y=1,y=13)','d answer','undefine answer',
        'set dfile 1','define answer=mean(f+lon.5+lat.5+lev.5,y=1,y=13)','d answer','undefine answer'])

script='\n'.join(lines).replace('set z 1 5','set z 1 '+str(nz))+'\nquit\n'
(work/'commands.txt').write_text(script)
def grids(text):
    out=[];it=iter(text.splitlines())
    for line in it:
        m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
        if not m:continue
        n=int(m[1]);undef=float(m[2]);a=[]
        while len(a)<n:a.extend(float(x) for x in next(it).split())
        assert len(a)==n
        out.append([None if v==undef else v for v in a])
    return out
def run(launcher,n,mode):
    p=subprocess.run([str(launcher),'-blu','-d','gxdummy','-h','gxdummy'],
       input='set threads '+str(n)+'\n'+script,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,universal_newlines=True,
       env=dict(os.environ,OPENGRADS_COLOR='0',GA_PROGRESS='off',
                GA_TIME_WORKERS='off',GA_SPATIAL_BATCH=mode),timeout=180)
    (work/(Path(launcher).name+'-'+str(n)+'-'+mode+'.log')).write_text(p.stdout)
    assert p.returncode==0 and not re.search(r'Error|ERROR|AddressSanitizer|runtime error',p.stdout),p.stdout[-6000:]
    return grids(p.stdout)
base=run(reference,1,'off')
assert len(base)==len(labels), ('baseline grid count',len(base),len(labels))
print('baseline grids',len(base),flush=True)
count=0;error=0.
for n in (1,4,8):
    for mode in ('force','on','off'):
        actual=run(launcher,n,mode)
        assert len(actual)==len(base),(n,mode,len(actual),len(base))
        for i,(a,b) in enumerate(zip(actual,base)):
            assert len(a)==len(b),(n,mode,i,len(a),len(b))
            for x,y in zip(a,b):
                if x is None or y is None:assert x is y,(n,mode,labels[i],x,y)
                elif not math.isfinite(x) or not math.isfinite(y):assert x==y or math.isnan(x) and math.isnan(y),(n,mode,labels[i],x,y)
                else:
                    error=max(error,abs(x-y));assert abs(x-y)<=1e-10+abs(y)*2e-12,(n,mode,labels[i],x,y)
                count+=1
        print('Spatial reductions PASS:',fmt,n,mode,'max_abs',error,flush=True)
(work/'result.json').write_text(json.dumps(dict(grids=len(base),values=count,max_abs=error,pointwise_expressions=expressions),indent=2)+'\n')
print('Spatial reductions complete:',fmt,'values=',count,'grids=',len(base),'max_abs=',error,flush=True)
