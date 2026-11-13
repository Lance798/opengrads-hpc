# Time workers and general expressions

`set threads N` is the total calculation CPU budget. Eligible NetCDF/HDF5/BP5 commands
can use up to N isolated worker processes, one calculation thread per worker,
to evaluate different output times. Workers execute the complete expression
with the existing evaluator. The main process retains result ordering,
progress, cancellation, the defined-variable registry and all graphics.

For example, after opening a NetCDF, HDF5 or BP5 descriptor:

```text
set threads 64
set lon 0 90
set lat -10 10
set z 22
set t 2145 2400
define speed=mag(u,v)
define energy=pow(u,2)+pow(v,2)
define cloudy=maskout(qc,(qc>1e-5)-0.5)*1000
```

For direct T or Z/T DISPLAY, fix longitude and latitude first:

```text
set lon 45
set lat 0
set z 1 44
display sin(u/10)+cos(v/10)
```

X/Y time animations retain their existing execution path. A single output
time cannot use output-time workers. A time reduction can instead dispatch
its input times, as described below; existing in-memory OpenMP and native
reader optimizations also apply. Worker startup can dominate short commands;
the pool is reused by subsequent eligible commands until reset, close or a
CPU-budget change. `GA_TIME_TRACE=1` reports the actual workers and their PIDs;
`GA_TIME_WORKERS=off` disables the pool.

General-expression output-time workers currently require at least 32 times,
a default NetCDF/HDF5/BP5 descriptor, supported native file inputs, syntax and a result
shape that agrees at every time. DEFINE additionally retains its existing
minimum dataset-work threshold. Bounded static DEFINE inputs can accompany
native fields, including explicit file-qualified operands such as `qv.3*rhobar.4`.
Time/ensemble-varying or oversized definitions, dimension qualifiers, plug-ins
and unsupported functions keep their existing evaluator.
Constants alone do not start workers. New compound DISPLAY expressions that reduce a
displayed Z/T axis retain the ordinary evaluator; the established outer
AVE/MEAN/AAVE/AMEAN/ASUM/ZINT profile paths retain their per-time behavior.

ASUM now also supports direct T/Z-T profile DISPLAY at fixed X/Y, including
fractional bounds, masks and cyclic longitude regions. DEFINE and DISPLAY
use the same per-time evaluator; the existing grid weights and denominator-free
sum are preserved. For example:

```text
set x 1
set y 1
set z 1 44
set t 1 last
define c=asum(qvsat(th*pibar,100000*pow(pibar,1004/287)),lon=0,lon=180,lat=-20,lat=20)
display c
display asum(qvsat(th*pibar,100000*pow(pibar,1004/287)),lon=0,lon=180,lat=-20,lat=20)
```

ATOT can use workers in DEFINE, but its direct DISPLAY still requires the
native fixed-axis environment. Wrapped area reductions or ZINT in DISPLAY retain their
native restrictions; this stage does not turn an invalid native environment
into a valid one only at higher CPU counts. Existing outer AAVE/AMEAN/ZINT
profile DISPLAY remains supported.

Pass diagnostic environment controls to the launcher as:

```bash
GA_TIME_TRACE=1 ./opengrads -l
GA_TIME_WORKERS=off ./opengrads -l
```

Linux archives that need their bundled glibc restart time workers through the
same loader and private library search path as the parent. Normal builds keep
their ordinary executable restart. The release build and Linux packager run
`TestTimeLoader.sh`, including the packaged launcher's old-host branch, to
check both input/output workers, numerical/graphics agreement and failure
recovery. This does not export the bundled libc through `LD_LIBRARY_PATH` to
ordinary shell subprocesses.

Each per-time result is limited to 128 MiB (values plus masks). Worker
estimates cap concurrency using available memory and an 8 GiB ceiling. These
caps govern worker workspace; DEFINE must still store its complete final
array. A global 1536 x 384 plane over 256 times requires about 1.27 GiB for
that array alone, using double values and byte masks. More workers do not
remove the cost of transferring and storing full grids.

## SUM and input-time expression workers

A time reduction can use workers even when its output has only one time.
SUM now evaluates supported expressions independently at their input times,
then folds grids in ascending time order using the original weights, missing
masks and accumulator. This works in DEFINE and direct DISPLAY. The shared
input producer also benefits AVE, MEAN, SUMG, MIN, MAX, MINLOC and MAXLOC.
These selectors also belong to the whole-expression allowlist; reducing a
displayed axis still retains the native output shape.

For a single XY map summed over 256 input times:

```text
set threads 64
set lon 0 360
set lat -20 20
set z 1
set t 1
define c=sum(qvsat(th*pibar,100000*pow(pibar,1004/287)),t=1,t=256)
display c
display sum(u*v,t=1,t=256)
```

The expression is general: supported arithmetic, trigonometry, masks and
saturation functions use the same mechanism. Varying input Z profiles,
absolute spatial reductions and cyclic longitude retain the ordinary evaluator
inside each worker. Keeping the outer T fixed produces one time-summed map;
`set t 1 256` instead requests repeated output-time evaluations of that sum.

The first two input times retain the original initialization. Eligible NetCDF/HDF5/BP5
reductions with at least 32 requested input times then use batches of up to
256 grids, bounded by the existing 64 MiB--1 GiB target and available memory.
The active worker count also depends on batch length and workspace estimates;
`set threads 128` may therefore use fewer than 128 workers for large maps.
Pool workers each use one calculation thread. There is no adaptive switch
between worker/thread combinations during the command.

Time/ensemble-varying or oversized defined fields, dimension-qualified
inputs, unsupported sources/functions, nested explicit time bounds, relative spatial bounds and varying
ensemble axes keep their earlier paths. Short or memory-limited batches fall
back to ordinary evaluation. `GA_TIME_TRACE=1` additionally prints
`Time input expression: ...` when whole input expressions are dispatched.
Progress remains owned by the parent; cancellation or failed workers discard
incomplete results and preserve an existing DEFINE.

Large XY results must transfer every per-time grid to the parent and fold them
there. Scaling can saturate well before 128 workers; output profiles that
reduce spatially inside each worker return far less data.

## Cross-file expressions

File qualifiers use the shared expression path for all 47 supported built-ins,
in DEFINE, eligible T/Z-T DISPLAY and input-time reductions. For example, in a
fresh session:

```text
open /path/to/vvm.ctl
open /path/to/vvm_profiles.ctl
set dfile 1
set threads 8
set x 1 512
set y 1 512
set z 1
set t 1 32
define cwv=zint(qv*rhobar.2)
```

Use `q files` to check the actual numbers in an existing session. Opening a
later file does not change the default file. If these descriptors are files
3 and 4, use `set dfile 3` and `rhobar.4`; `.2` continues to mean file 2.

Each worker reopens the default file and only the referenced native files,
using each file's original OPEN/SDFOPEN/XDFOPEN/BPOPEN method. Explicit CTLs
retain their axes and aliases even when several CTLs describe the same data
path. Internal compact file numbers are private to the worker; the parent
expression and session file list retain their original meaning. The ordinary
evaluator maps each source's world coordinates and time axis and checks grid
compatibility. Sources are never paired by equal time indices. A profile's
name does not make it static: native missing times remain missing. To reuse
a single reference time intentionally, first create a bounded static DEFINE
profile as shown below.

The default descriptor must meet the existing NetCDF/HDF5/BP5 eligibility
rules. Referenced files may also be ordinary binary grids. Each command is
limited to 32 native files including the default. Indexed, projected,
climatological or unsupported sources, differing calendars, variable dimension
qualifiers such as `qv.3(t=2)`, and oversized source tables keep the ordinary
path. Worker memory estimates include every reopened source's cache and BP5
reader allowance. Startup or evaluation failure shuts down the pool, discards
the incomplete output and preserves an existing DEFINE. Cancellation and
progress follow the same rules as single-file expressions.

`GA_TIME_TRACE=1` adds `Time file inputs: count=... default=...` when more than
one descriptor is reopened. `TestTimeFileInputs.sh` compares all 47 functions
at 1/2/4 cores across HDF5, classic NetCDF, compressed NetCDF-4 and BP5. It also
checks ordinal remapping, changed defaults, equal data paths with distinct
axes/clocks, native binary profiles, mixed open methods, source limits,
DEFINE shadowing, cancellation and killed-worker recovery.

## Circular X/Y shifts

`roll(expr, integer_shift, x|y)` moves values and their missing mask within the
evaluated grid; coordinates stay unchanged. DEFINE workers retain each XY
plane, including output Z/T series. Input-time workers retain their XY/XZ/YZ
shape. Singleton X/Y axes are unchanged, so ordinary T/ZT profile DISPLAY
also keeps its native behavior. Existing outer spatial reductions evaluate
ROLL within the grid they establish. Native X/T or Y/T DISPLAY continues
through the ordinary evaluator. See [ROLL.md](ROLL.md) for shift direction,
selected-domain boundaries and nested calls.

```text
set x 1 512
set y 1 512
set z 1
set t 1 32
define west=roll(qv,1,x)
set t 1
define accumulated=sum(roll(qv,-1,x)*qv,t=1,t=32)
```

`TestRoll.sh` verifies independent permutations, bit-preserving copies and
optional NumPy comparisons. `TestTimeRoll.sh` checks serial versus 2/4 workers
on HDF5, NetCDF/NetCDF-4 and BP5, including file-qualified inputs, static
snapshots, templates, missing times, parent fallback, cancellation and failure
recovery. Parent profile fallback retains original file numbers when no
workers start; compact worker ordinals are published only for worker jobs.

## Built-in function audit

The `rtnprs` registry in `cola/src/gafunc.c` contains 67 built-in names.
The time-worker allowlist contains 47 of them. Eligibility depends on the
complete expression and command shape; it does not imply that every argument
or dimension combination is parallelized.

| Worker functions | Names |
|---|---|
| Arithmetic | `abs`, `sqrt`, `exp`, `log`, `log10`, `pow`, `mag` |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` |
| Conditions and masks | `if`, `maskout`, `const` |
| Thermodynamic diagnostics | `esat`, `qvsat`, `tvrh2q`, `tvrh2t` |
| Averages and sums | `ave`, `mean`, `sum`, `sumg`, `aave`, `amean`, `asum`, `asumg`, `atot` |
| Dimension extrema | `min`, `max`, `minloc`, `maxloc` |
| Area extrema | `amin`, `amax`, `aminlocx`, `aminlocy`, `amaxlocx`, `amaxlocy` |
| Spatial neighbors | `hdivg`, `hcurl`, `smth9`, `cdiff`, `roll` |
| Vertical operations | `zint`, `vint`, `zslice` |

The twenty additional names use the existing evaluator in each process.
DEFINE can evaluate an XY plane independently at every output time, including
horizontal derivatives, smoothing, column integrals and indexed Z slices:

```text
set threads 8
set x 1 512
set y 1 512
set z 1
set t 1 32
define speed=if(u>0,u,-u)
define smooth=smth9(qv)
define dx=cdiff(qv,x)
define column=zint(qv)
```

Input-time reductions retain their spatial grid inside each worker, including
varying Z for `sum(cdiff(qv,z),t=1,t=32)`. `cdiff` accepts only X/Y/Z and their
`p`/`m` one-sided forms in workers. T/E derivatives retain the native evaluator
because they need neighboring times or ensembles. Direct DISPLAY containing
`smth9` or `cdiff` also retains the native evaluator: smoothing a T/Z-T result
or differentiating a varying Z axis must not become scalar-per-time work.
This restriction does not prevent DEFINE or input-time expression workers.

Area extrema and ASUMG retain the same native fixed-Z/T/E DISPLAY restrictions
as ATOT. VINT/ZSLICE and horizontal diagnostics keep their native dimension
requirements; the new allowlist does not make an invalid DISPLAY valid only
at higher thread counts. MIN/MAX/MINLOC/MAXLOC can use direct T DISPLAY when
it preserves the native result shape. Existing outer AAVE/AMEAN/ASUM/ZINT
profile paths keep their established per-time behavior.

Ordinary arithmetic, comparisons and `&`/`&&`/`|`/`||` can combine these
functions. GrADS comparisons return +1/-1. IF evaluates both branches and
retains the original mask propagation; it is not a lazy conditional. Its
scalar and one-dimensional conditions now own independent temporary storage,
fixing an existing aliasing/freeing error before enabling worker use. Pressure
units, convergence guards, vertical addition order and first-hit extrema tie
rules remain those of the native functions. There is no registered `atan()`;
use `atan2`.

The remaining 20 names retain their earlier paths:

| Functions outside the allowlist | Main consideration |
|---|---|
| `skip`, `grarea` | Axis sampling phase and input masks |
| `gint`, `fndlvl` | Integration axes and vertical search shape |
| `scorr`, `sregr`, `tcorr`, `tregr`, `tmave` | Paired samples, time windows and accumulation order |
| `tloop`, `eloop` | Loop ownership and nested scheduling |
| `gr2stn`, `oacres`, `oabin`, `stnave`, `stnmin`, `stnmax`, `coll2gr`, `s2g1d`, `lterp` | Station/collection state and target coordinates |

`TestIf.sh` checks independent analytic scalar/X/Z/XY values and branch-error
recovery. `TestTimeSpatialFunctions.sh` checks all twenty additions against the
one-thread evaluator at 2 and 4 calculation cores, including regular/template
and reversed HDF5/NetCDF, raw/compressed BP5, masks, ties, spatial-Z input
reductions, legal direct DISPLAY, native fallbacks, pool reuse and cleanup.
Its BP5 mode also checks CTL/BPOPEN progress, cancellation, killed-worker
recovery and preservation of an old DEFINE. Run the formats as:

```bash
bash pytests/TestIf.sh
bash pytests/TestTimeSpatialFunctions.sh
GA_TEST_TIME_FORMAT=nc bash pytests/TestTimeSpatialFunctions.sh
GA_TEST_TIME_FORMAT=nc4 bash pytests/TestTimeSpatialFunctions.sh
GA_TEST_TIME_FORMAT=bp5 bash pytests/TestTimeSpatialFunctions.sh
```

## BP5 expression workers

BP5 expressions use the same pool, masks, ordering, static-definition snapshots
and memory limits. Explicit BP5 CTLs reopen with their original descriptor,
preserving aliases and coordinate choices. BPOPEN workers rediscover the
original dataset, rather than trying to reopen its deleted temporary descriptor.
Each worker limits ADIOS2 data and metadata reader threads to one. Normal
parent reader settings remain unchanged. Reader buffers are included in the
worker memory estimate; DEFINE still owns the full final array.

Unqualified bare BP5 variables and native input-step batching retain their existing bulk
reader path. Complete expressions such as `qv*qv`, `if(qv>1e-5,qv*1000,0)` and
`zint(qv)` can instead evaluate independent output times. A time reduction of
a compound expression can use input-time workers; a reduction of an unmodified
native field keeps its optimized ADIOS2 batch reads. Worker startup and private
metadata can dominate small requests, so eligibility alone is not a speedup
guarantee. Use `GA_TIME_TRACE=1` and compare `GA_TIME_WORKERS=off` when measuring.
Restart GrADS after rebuilding: the worker protocol version includes the BP5
restart contract and rejects an incompatible parent/worker binary.

## NetCDF support and template activation

Classic NetCDF and compressed NetCDF-4 use the same isolated processes,
function allowlist, scheduling, memory caps and recovery as HDF5. Workers
reopen their own dataset using the original `open`, `sdfopen` or `xdfopen`
method. NetCDF handles and library state are never shared between workers.

`OPTIONS template` with time substitutions such as `%tm6.nc` is supported;
each worker resolves its assigned time to a file. Templates are not required:
a single NC file containing multiple times can use the same workers. Merely
opening a template does not start processes. A command must request eligible
time work, use at least two calculation threads and satisfy the existing
work/memory thresholds. Output-time DEFINE/DISPLAY requires at least 32
times; DEFINE also requires the existing minimum dataset-work estimate.
Native input-time reads retain their batching/work thresholds, while whole
input expressions require at least 32 requested times. These rules are shared
with HDF5. Short commands and `GA_TIME_WORKERS=off` keep ordinary evaluation.

All 47 functions listed above are supported for both formats under the same
expression/environment restrictions. This does not extend worker eligibility
to every GrADS built-in. Static session-defined inputs such as
`define rho=rho.2` can accompany native fields in workers. Native file-qualified
operands use the shared cross-file path above; plug-ins keep the ordinary evaluator.
For the user's reference-density CWV series:

```text
open /path/to/thermodynamic.ctl
open /path/to/bar.ctl
set x 1
set y 1
set z 1 75
set t 1
define rho=rho.2
set z 1
set t 1 last
define c=aave(zint(rho*qv),x=1,x=384,y=1,y=384)
display c
```

This expression now sends the saved density profile to each worker.
Expressions built entirely from
native fields in the default NC descriptor, such as
`aave(zint(qv),x=1,x=384,y=1,y=384)`, can use workers; integrating QV alone
does not calculate CWV. Native NC profiles with absent X/Y/T axes are
broadcast from their actual shape, including missing masks and unpacking.

`TestNetCDFTimeWorkers.sh` runs eight time-worker suites, including
`TestTimeFileInputs.sh`, against
both classic and compressed NetCDF-4, including regular/template/reversed
files, all supported functions, packed data, missing template times, progress,
cancellation, failed workers, pool reuse, memory caps and identical graphics.
The expression suite also checks SDFOPEN/XDFOPEN restart and analytic profile
broadcasts. Point the tests at the build and NetCDF development installation:

```bash
OPENGRADS_BUILD_ROOT=/path/to/build \
OPENGRADS_TEST_NETCDF_ROOT=/path/to/netcdf \
  bash pytests/TestNetCDFTimeWorkers.sh
OPENGRADS_BUILD_ROOT=/path/to/build \
  bash pytests/TestNetCDFTimeWorkersReal.sh \
  /path/to/icluster_f10_b030/gs_ctl_files/thermodynamic.ctl
```

The real-data test reads the original files without modifying them. It checks
all 26 allowed functions, all eight input-time reduction selectors, full
384x384 ZINT/area reduction across 75 levels and 32 times, all 361 template
times with `set t 1 last`, the user's original defined-density CWV expression,
defined PIBAR/PBAR saturation diagnostics, and static XY/XYZ masks against
one-thread evaluation.

On the supplied `icluster_f10_b030` template dataset, 54,843 compared values
had identical masks and zero maximum absolute difference. One local
run of the full-area ZINT/area DEFINE plus DISPLAY case took 8.55 seconds with
one thread and 3.43 seconds with four workers. This is a validation sample,
not a general scaling guarantee; storage and cache state affect timings.

`TestHDF5TimeWorkersReal.sh /path/to/rcemip_45l_large/vvm.ctl` additionally
checks the provided 45-level VVMex HDF5 template with 1 and 4 calculation
cores. It compares native expressions and static scalar/profile/XY/XYZ inputs,
including saturation, wind speed, column integrals and all eight input-time
reduction selectors. Coverage uses the first 32 times, cropped horizontal
areas, Z/T profiles and all levels for column integrals. Earlier validation at
1/4/8/16 cores compared 2,976 values at each parallel CPU count with identical
masks and zero maximum absolute difference from the one-core evaluator.

## Static DEFINE inputs

Time workers automatically accept stored scalars, Z profiles, XY maps and XYZ
cubes whose T and E dimensions both have length one. The expression must also
reference a native field of the default NetCDF/HDF5/BP5 descriptor and satisfy the
existing function, shape, time-count and work thresholds. No `set global` is
needed. A definition repeated over several times is still time-varying storage
for eligibility purposes, even if its values happen to be equal.

Workers receive the saved values, missing masks, dimensions, index offsets,
calendar and coordinate arrays. They do not reopen the source of a definition
or rerun its original formula. Thus a profile originally copied from a binary
`bar.ctl` can accompany native NC/HDF5 data; the profile's source file may be
closed afterwards. Saturation formulas and the order of ZINT, masks and area
reductions remain unchanged.

For reference profiles, fix X/Y/T/E while defining them, then select the desired
output times. After the two OPEN commands in the CWV example above:

```text
set x 1
set y 1
set t 1
set e 1
set z 1 75
define rho=rho.2
define pibar=pibar.2
define pbar=pbar.2
set z 1
set t 1 last
set threads 4
define c=aave(zint(qv*rho),x=1,x=384,y=1,y=384)
```

At the same fixed output Z, `qvsat(th*pibar,pbar)` and `esat(th*pibar)` can
use those saved profiles too. Static masks can be defined over a spatial
region at one time and then applied with `maskout(qv,landmask)` or
`maskout(qv,cubemask)` at subsequent times. Keep the desired Z range when
creating a 3-D mask, and fix Z when creating a 2-D mask.

Every command captures fresh copies of only its referenced definitions, with
repeated names deduplicated. Re-DEFINE, UNDEFINE and SET DEFVAL changes are
therefore visible on the next command even when the worker PIDs are reused.
Each worker owns its copies; normal REINIT cleanup also handles partial
receives. Snapshot transmission is nonblocking in the parent and observes
cancellation and worker failures, preserving an old DEFINE on an aborted
replacement.

The command's combined snapshot payload (values, byte masks and coordinate
metadata) is limited to 128 MiB and 32 distinct definitions. Spatial dimensions
and stored nonlinear coordinate axes are limited to 65,536 entries each.
Snapshot memory and definition metadata are included in the existing per-worker
memory estimate and 8 GiB/available-memory concurrency ceiling. A 384x384x75
stored field uses about 95 MiB for double values and byte masks per worker.
Large masks can reduce worker count and add transfer time; every command
copies its inputs afresh. Time/ensemble-varying, climatological, unsupported
or over-budget definitions keep ordinary evaluation without truncation.
Purely defined/constant expressions do not start an I/O worker pool.

`GA_TIME_TRACE=1` additionally reports `Time defined inputs: count=... bytes=...`.
Restart GrADS after rebuilding: the worker configuration protocol is versioned
so incompatible parent/worker binaries fail promptly rather than misreading a
payload. `GA_TIME_WORKERS=off` retains ordinary evaluation for comparisons.

`TestTimeDefinedInputs.sh` checks scalar/Z/XY/XYZ inputs, nonlinear/linear and
cropped coordinates, masks, all eight input-time selectors, source-file close,
fresh copies after edits, native-name shadowing, pool reuse, large masks,
budget/dependency limits and identical graphics. Linux tests additionally
interrupt a deliberately delayed large transfer, verify preservation of an old
DEFINE and recovery, and check that workers are reaped. The established
TimeWorkers/TimeExpressionReductions terminal suites also exercise profile and
scalar inputs during calculation cancellation and killed-worker recovery.
The expression-reduction pool-shrink regression uses four workers followed by
a two-time tail batch. The native-reader regression uses cache-memory estimates
to reduce the active pool from four to two workers and then restore the same
four PIDs, including partial batches. Validation stays within four calculation
cores.

## Existing HDF5 regression coverage

`TestTimeExpressions.sh` compares DEFINE and direct T/Z-T DISPLAY with the
ordinary one-thread evaluator. It covers regular/template/reversed HDF5,
packed values, cropped planes, Z/T ordering, scalar/surface/profile
broadcasts, arithmetic, trigonometry, masks, division by zero, invalid powers,
out-of-domain inverse trigonometry and NaN/Inf classifications. It also
checks pool reuse, fallback, identical PNGs, progress, cancellation,
killed-worker recovery and preservation of an old DEFINE after failure.
`TestTimeExpressionReductions.sh` additionally compares whole input expressions,
all eight time-reduction selectors, fractional weights, increments, masks,
nonlinear diagnostics, varying Z, spatial reductions and cyclic longitude.
It checks worker reuse, memory caps, packed/template/reverse data, numerical
and graphics agreement, progress, cancellation and killed-worker recovery.

The earlier TimeWorkers and TimeMeanWorkers suites continue to exercise
the reduction-specific paths. Performance results are recorded separately
in [PERFORMANCE.md](PERFORMANCE.md).

The production build and combined regression command used for this stage:

```bash
./dev build
./dev test TimeLoader TimeExpressionReductions TimeExpressions OpenMP ReductionIO BinaryHalo H5Decode ZInt \
  TimeWorkers TimeMeanWorkers MaskThreshold AveSection Progress Undo \
  DefineNames Saturation ClearCanvas
```

Compound expressions that reduce a displayed axis retain their native output
shape; constructing a repeated output-time grid for them remains deferred.
Supported inner time reductions may use input-time workers without changing
that shape. For a vertical sum producing a time series, fix
Z outside the expression and specify the integration range in the function:

```text
set z 1
set t 1 32
display 2*sum(f,z=1,z=4)
```

Validation exposed a preceding native-reader bug: the spatial-section planner
tried to use a spatial coordinate callback for a remaining T axis. It now
retains the position-at-a-time evaluator for T/E outputs. The repair and its
regression coverage are recorded separately in PERFORMANCE.md.
