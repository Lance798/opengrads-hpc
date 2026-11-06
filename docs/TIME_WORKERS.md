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
time cannot use output-time workers; existing in-memory OpenMP and native
reader optimizations still apply. Worker startup can dominate short commands;
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
AVE/MEAN/AAVE/AMEAN/ZINT profile paths retain their earlier behavior.

In particular, ASUM/ATOT can use workers in DEFINE, whose evaluator fixes
Z/T for each result. Their direct DISPLAY still requires the native fixed-axis
environment. Wrapped area reductions or ZINT in DISPLAY also retain their
native restrictions; this stage does not turn an invalid native environment
into a valid one only at higher CPU counts. Existing outer AAVE/AMEAN/ZINT
profile DISPLAY remains supported.

With the local development launcher, environment controls can be passed as:

```bash
./dev exec env GA_TIME_TRACE=1 ./opengrads -l
./dev exec env GA_TIME_WORKERS=off ./opengrads -l
```

Each per-time result is limited to 128 MiB (values plus masks). Worker
estimates cap concurrency using available memory and an 8 GiB ceiling. These
caps govern worker workspace; DEFINE must still store its complete final
array. A global 1536 x 384 plane over 256 times requires about 1.27 GiB for
that array alone, using double values and byte masks. More workers do not
remove the cost of transferring and storing full grids.

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

The remaining 40 names are deliberately left on their existing paths.
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
The earlier TimeWorkers and TimeMeanWorkers suites continue to exercise
the reduction-specific paths. Performance results are recorded separately
in [PERFORMANCE.md](PERFORMANCE.md).

The production build and combined regression command used for this stage:

```bash
./dev build
./dev test TimeExpressions OpenMP ReductionIO BinaryHalo H5Decode ZInt \
  TimeWorkers TimeMeanWorkers MaskThreshold AveSection Progress Undo \
  DefineNames Saturation ClearCanvas
```

Compound expressions that reduce a displayed axis need their own shape
plan and remain deferred. For a vertical sum producing a time series, fix
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
