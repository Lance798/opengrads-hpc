# Time workers and general expressions

`set threads N` is the total calculation CPU budget. Eligible HDF5 commands
can use up to N isolated worker processes, one calculation thread per worker,
to evaluate different output times. Workers execute the complete expression
with the existing evaluator. The main process retains result ordering,
progress, cancellation, the defined-variable registry and all graphics.

For example, after opening an HDF5 descriptor:

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
native fields of the default HDF5 descriptor, supported syntax and a result
shape that agrees at every time. DEFINE additionally retains its existing
minimum dataset-work threshold. Defined inputs, file/dimension qualifiers,
plug-ins and unsupported functions keep their existing evaluator. Constants
alone do not start workers. New compound DISPLAY expressions that reduce a
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

With the local development launcher, environment controls can be passed as:

```bash
./dev exec env GA_TIME_TRACE=1 ./opengrads -l
./dev exec env GA_TIME_WORKERS=off ./opengrads -l
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
input producer also benefits AVE, MEAN, SUMG, MIN, MAX, MINLOC and MAXLOC;
it does not add those last four names to the whole-expression allowlist.

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

The first two input times retain the original initialization. Eligible HDF5
reductions with at least 32 requested input times then use batches of up to
256 grids, bounded by the existing 64 MiB--1 GiB target and available memory.
The active worker count also depends on batch length and workspace estimates;
`set threads 128` may therefore use fewer than 128 workers for large maps.
Pool workers each use one calculation thread. There is no adaptive switch
between worker/thread combinations during the command.

Defined fields, non-default-file/dimension-qualified inputs, unsupported
functions, nested explicit time bounds, relative spatial bounds and varying
ensemble axes keep their earlier paths. Short or memory-limited batches fall
back to ordinary evaluation. `GA_TIME_TRACE=1` additionally prints
`Time input expression: ...` when whole input expressions are dispatched.
Progress remains owned by the parent; cancellation or failed workers discard
incomplete results and preserve an existing DEFINE.

Large XY results must transfer every per-time grid to the parent and fold them
there. Scaling can saturate well before 128 workers; output profiles that
reduce spatially inside each worker return far less data.

## Built-in function audit

The `rtnprs` registry in `cola/src/gafunc.c` contains 66 built-in names.
The current time-worker allowlist contains 26 of them. Eligibility depends on
the complete expression and command shape; this is not a guarantee that every
possible argument or dimension combination is parallelized.

| Current worker functions | Names |
|---|---|
| Arithmetic functions | `abs`, `sqrt`, `exp`, `log`, `log10`, `pow`, `mag` |
| Trigonometry | `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` |
| Mask/value handling | `maskout`, `const` |
| Saturation diagnostics | `esat`, `qvsat` |
| Averages and sums | `ave`, `mean`, `sum`, `sumg`, `aave`, `amean`, `asum`, `atot` |
| Height integral | `zint` |

Ordinary `+`, `-`, `*`, `/`, parentheses, unary minus, comparisons and
`&`/`&&`/`|`/`||` can combine these functions and native fields. Formulas,
operation order, division-by-zero handling and invalid-value masks remain
those of the existing evaluator. GrADS comparisons return +1/-1; the 0.5
shift in the MASKOUT example retains the true cells. There is no built-in
`atan()`; `atan2` is the registered function.

The remaining 40 names are not accepted inside worker expressions. Outer
MIN/MAX/MINLOC/MAXLOC can still use the shared input-time producer described
above; their existing accumulator and result handling stay in the parent.
The categories below describe the additional review needed, not a finding
that these functions are inherently unsafe or impossible to parallelize.

| Additional review | Names | Main consideration |
|---|---|---|
| Branches and pointwise diagnostics | `if`, `skip`, `tvrh2q`, `tvrh2t` | Scalar/axis broadcasts, masks, branch storage and numerical guards |
| Spatial neighbors | `hdivg`, `hcurl`, `smth9`, `cdiff` | Coordinate metrics, halo reads and boundaries |
| Additional spatial/statistical reductions | `asumg`, `grarea`, `min`, `max`, `minloc`, `maxloc`, `amin`, `amax`, `aminlocx`, `aminlocy`, `amaxlocx`, `amaxlocy` | Result dimensions, weights and location tie rules |
| Vertical operations | `vint`, `zslice`, `gint`, `fndlvl` | Full-column dependencies, interpolation and output shape |
| Correlation/regression/time masks | `scorr`, `tcorr`, `tmave`, `sregr`, `tregr` | Input spans, missing-sample pairing and accumulation order |
| Dimension loops | `tloop`, `eloop` | Nested scheduling and dimension ownership |
| Station, collection and interpolation | `gr2stn`, `oacres`, `oabin`, `stnave`, `stnmin`, `stnmax`, `coll2gr`, `s2g1d`, `lterp` | Station/collection session state, target grids and output coordinates |

The audit removed an unregistered `atan` entry from the worker allowlist;
this does not remove a callable built-in. No additional untested functions
are enabled by this stage.

## Validation and deferred cases

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
