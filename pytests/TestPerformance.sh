#!/usr/bin/env bash
# Last-command diagnostics must not change values, graphics or ordinary output.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-performance.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
format="${GA_TEST_TIME_FORMAT:-binary}"
if [[ "$format" != binary ]]; then
  if [[ "$format" == bp5 ]]; then
    config="${OPENGRADS_ADIOS2_ROOT:?BP5 tests need OPENGRADS_ADIOS2_ROOT}/bin/adios2-config"
    writer_define=-DZS_ADIOS
    read -r -a hflags <<< "$("$config" --serial --c-flags)"
    read -r -a hlibs <<< "$("$config" --serial --c-libs)"
  else
    source "$repo_root/pytests/time_worker_format.sh"
  fi
  "${CC:-cc}" -std=gnu99 -O2 "$writer_define" -DNX=17 -DNY=13 -DNT=32 \
    "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
  "$test_root/writer" "$test_root" zint
fi
python3 -u - "$repo_root" "$test_root" "$format" <<'PY'
import math, os, re, struct, subprocess, sys
from pathlib import Path
repo, root = map(Path, sys.argv[1:3]); fmt = sys.argv[3]
launcher = os.environ.get('OPENGRADS_LAUNCHER', str(repo/'opengrads'))
env = dict(os.environ, OPENGRADS_COLOR='0', OPENGRADS_UPDATE_CHECK='0', GA_PROGRESS='off')
if fmt == 'binary':
    nx = ny = 256; nz = 1
    with (root/'field.dat').open('wb') as stream:
        stream.truncate(nx*ny*32*2*4)
        stream.write(struct.pack('=f', -9999))
    dataset = 'field.dat'; dtype = ''; dims = '99'
else:
    nx,ny,nz = 17,13,4
    dataset = {'h5':'slice.h5','nc':'slice.nc','nc4':'slice_compressed.nc','bp5':'slice.bp'}[fmt]
    dtype = 'dtype '+('hdf5_grid' if fmt=='h5' else 'bp5' if fmt=='bp5' else 'netcdf')+'\n'
    dims = 'z,y,x' if fmt=='bp5' else 't,z,y,x'
ctl = root/'field.ctl'
ctl.write_text('dset ^'+dataset+'\n'+dtype+'undef -9999\n'+
    f'xdef {nx} linear 10 .1\nydef {ny} linear -30 .1\n'+
    ('zdef 1 linear 50 1\n' if nz==1 else 'zdef 4 levels 50 150 400 1000\n')+
    f'tdef 32 linear 00z01jan2000 1hr\nvars 2\nf {nz} {dims} Float\ng {nz} {dims} Double\nendvars\n')
def run(commands, extra=None, error=False, graphics=False):
    p = subprocess.run([launcher,'-blu','-d','gxdummy','-h','Cairo' if graphics else 'gxdummy'],
        input=commands+'\nquit\n', universal_newlines=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, env=dict(env, **(extra or {})), timeout=90)
    assert p.returncode==0, p.stdout[-4000:]
    if not error:
        assert not any(int(v)>0 for v in re.findall(r'<RC> (-?\d+) </RC>',p.stdout)),p.stdout[-4000:]
    return p.stdout
def reports(text): return re.findall(r'Performance report\n(.*?)End performance\n',text,re.S)
def seconds(report, label):
    value = float(re.search(re.escape(label)+r': ([0-9.]+) s',report)[1])
    assert math.isfinite(value) and value>=0, report
    return value
def check(report, command, workers=None):
    assert '  Command: '+command+'\n' in report, report
    wall = seconds(report,'Wall time')
    for phase in ('Evaluation, including reads and worker wait','Display/output calls','Pool setup, included in evaluation'):
        assert seconds(report,phase)<=wall+2e-6, report
    if workers is not None:
        assert f'Time workers, peak active: {workers}\n' in report,report
    assert 'Read/decompression time is not measured separately' in report,report
    assert 'Parent peak RSS, process lifetime:' in report or 'Parent peak RSS: unavailable' in report
setup = f'open {ctl}\nset threads 4\nset gxout stat\nset z 1\nset t 1\n'
config = run('q config')
openmp = bool(re.search(r'Config:.*\bopenmp\b',config))
if not openmp: setup = setup.replace('set threads 4','set threads 1')
if fmt == 'binary':
    text = run('q performance\n'+setup+'d f+1\nq performance\nset threads 1\nq config\nq performance\nq performance extra\nq performance',error=True)
    assert 'No completed analysis command recorded' in text,text
    found = reports(text); assert len(found)==3 and found[0]==found[1]==found[2],text
    check(found[0],'d f+1',0)
    assert 'CPU budget at command start: '+('4' if openmp else '1') in found[0]
    assert 'Input backends observed: binary' in found[0]
    assert 'contiguous binary slab reader' in found[0],found[0]
    if openmp: assert 'OpenMP kernel eligibility observed: yes' in found[0]
    assert 'QUERY PERFORMANCE error: no arguments expected' in text
    text = run(setup+'snapshot=f+2\nq performance\nd snapshot\nq performance')
    a,b = reports(text);check(a,'snapshot=f+2',0);check(b,'d snapshot',0)
    assert seconds(a,'Display/output calls')==0,a
    assert 'Input backends observed: DEFINE' in b and 'defined-array lookup' in b,b
    text = run(setup+'d missing_variable\nq performance',error=True)
    found=reports(text);assert found,text
    a = found[0];check(a,'d missing_variable',0);assert 'Status: error' in a,a
    assert seconds(a,'Display/output calls')==0,a
    script = root/'report.gs';script.write_text("function main(args)\n'd f+4'\n'q performance'\nsay result\n")
    text=run(setup+f'run {script}');found=reports(text);assert found,text
    a = found[0];check(a,'d f+4',0)
    # A read-only query must leave every exported value and missing mask intact.
    for query in (False,True):
        path = root/('queried.dat' if query else 'plain.dat')
        text = run(setup+f'set fwrite -cl {path}\nset gxout fwrite\nd f+1\n'+
            ('q performance\n' if query else '')+'disable fwrite')
        assert bool(reports(text))==query,text
    assert (root/'queried.dat').read_bytes()==(root/'plain.dat').read_bytes()
    assert (root/'plain.dat').stat().st_size==nx*ny*4
    # Cairo renders the same picture even when a query flushes the command path.
    if 'cairo' in config.lower():
        for query in (False,True):
            path = root/('queried.png' if query else 'plain.png')
            run(setup+'set timelab off\nset gxout shaded\nd f+lon\n'+
                ('q performance\n' if query else '')+f'printim {path} x640 y480',graphics=True)
        assert (root/'queried.png').read_bytes()==(root/'plain.png').read_bytes()
    # Native row reads and spatial section batching are observed separately.
    a = reports(run(setup+'set x 3\nset y 1 16\nd f+1\nq performance'))[0]
    assert 'row/segment reader' in a,a
    a = reports(run(setup+'set x 1\nset y 1 16\nd mean(f*f,x=1,x=16)\nq performance'))[0]
    assert 'Spatial section/volume batching observed: yes' in a,a
    print('Performance binary passed: snapshots, errors, implicit DEFINE, scripts, timing, backends, paths, byte-identical values and graphics; <=4 cores.')
else:
    setup = f'open {ctl}\nset threads {4 if openmp else 1}\nset x 5\nset y 3\nset z 2\nset t 1 32\nset gxout print\nset prnopts %.17g 32 1\n'
    body = 'd f+g\nq performance\nd f+g\nq performance'
    text = run(setup+body,{'GA_TIME_WORKERS':'auto'})
    a,b = reports(text);check(a,'d f+g',4 if openmp else 0);check(b,'d f+g',4 if openmp else 0)
    backend = {'h5':'HDF5','nc':'NetCDF','nc4':'NetCDF','bp5':'BP5'}[fmt]
    assert backend in re.search(r'Input backends observed: (.*)',a)[1],a
    if openmp:
        assert 'Worker batches: 1; new pools: 1; reused pools: 0' in a,a
        assert 'Worker batches: 1; new pools: 0; reused pools: 1' in b,b
        assert 'Completed worker time results: 32' in a and 'Completed worker time results: 32' in b
        assert 'isolated time workers' in a and 'workspace estimate:' in a,a
    disabled = run(setup+body,{'GA_TIME_WORKERS':'off'})
    for report in reports(disabled):
        check(report,'d f+g',0)
        assert 'time workers disabled by GA_TIME_WORKERS' in report,report
    def printed_values(text):
        grids=[];lines=iter(text.splitlines())
        for line in lines:
            m=re.match(r'Printing Grid -- (\d+) Values -- Undef = (\S+)',line)
            if not m: continue
            n=int(m[1]);values=[]
            while len(values)<n: values.extend(map(float,next(lines).split()))
            assert len(values)==n
            grids.append(values)
        return grids
    assert printed_values(text)==printed_values(disabled) and len(printed_values(text))==2
    horizontal=f'open {ctl}\nset threads {4 if openmp else 1}\nset x 1 {nx}\nset y 1 {ny}\nset z 1\nset t 1\nset gxout stat\n'
    if fmt in ('h5','bp5'):
        report=reports(run(horizontal+'d zint(f)\nq performance'))[0]
        assert backend+' native column integral' in report,report
        report=reports(run(horizontal+'d zslice(f,2)\nq performance'))[0]
        assert backend+' indexed vertical gather' in report,report
    if fmt=='bp5':
        report=reports(run(horizontal+'d mean(f,t=1,t=32)\nq performance'))[0]
        check(report,'d mean(f,t=1,t=32)',0)
        assert 'BP5 native time-step batching' in report,report
    print('Performance native passed:',fmt,'actual worker counts, fresh/reused pools, 32 completed results, disabled workers, timing and identical values; <=4 cores.')
PY
