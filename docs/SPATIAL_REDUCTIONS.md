# Fixed-time spatial reductions

Spatial reductions can batch reads and evaluate eligible expressions as complete
sections or bounded XYZ slabs. Existing GrADS syntax is unchanged:

```grads
set t 1
set x 1 1024
set y 1
set z 1 69
define ua=mean(u*u,y=1,y=1024)
```

This produces XZ: each `(x,z)` averages along Y, retaining all 69 levels. A Z
reduction instead produces XY. `ave` retains its coordinate/latitude weights;
`mean` retains its own weights. Neither becomes an unweighted mean of partial
means.

## Built-in coverage

The planner shares the ordinary expression evaluator and numerical reduction
kernels. It does not implement alternative versions of the math functions.

| Use | Existing built-ins |
| --- | --- |
| Axis reduction along X/Y/Z | `ave`, `mean`, `sum`, `sumg`, `min`, `max`, `minloc`, `maxloc` |
| Area reduction across a DEFINE Z profile | `aave`, `amean`, `asum`, `asumg`, `atot`, `amin`, `amax`, `aminlocx`, `aminlocy`, `amaxlocx`, `amaxlocy` |
| Eligible expressions inside those reductions | Arithmetic/comparisons, `abs`, `sqrt`, `exp`, `log`, `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `pow`, `mag`, `atan2`, `maskout`, `if`, `const`, `esat`, `qvsat` |

Missing masks, fractional bounds, nonuniform coordinates and first-hit extrema
locations retain the existing kernels' rules. Axis folds keep their sequential
addition order; area kernels retain their existing OpenMP rounding behavior. IF still
evaluates all three operands. Domain warnings may be grouped by section instead
of by individual slice.

Two-dimensional expression sections use existing readers for eligible grid
inputs; projected and indexed fields retain their existing paths. The
XYZ producer is enabled by default for eligible BP5/HDF5 reads. NetCDF retains
its existing per-level reader; staged XYZ copies did not improve the tested
NetCDF dataset. Small full-plane expression Z/area reductions below 262,144
points also keep their existing path. X/Y strip reductions can still batch at
smaller sizes, where reducing repeated small reads is useful.

The producer fills DEFINE across Z and displayed XZ/YZ axis reductions through
the same code. Z reductions extend the existing volume fold. Each batch budgets
at most 128 MiB for prefetched double values, masks and expression-slab output;
native reader buffers, ordinary evaluator temporaries and final results are
additional. It does not materialize the entire requested XYZ volume.

Native `.N` fields with matching spatial axes are deduplicated within a batch;
each source clock and ensemble coordinate is mapped separately. Defined inputs
remain read-only. Native singleton/profile axes, surface variables, differing
axes, unsupported modifiers or requests outside the safe batch shape can keep
the original evaluator. In particular, expanding a native singleton axis must
not silently change fixed-coordinate missing values into broadcasting.

Neighbor-dependent calls such as `roll`, `cdiff` and `smth9`, nested reductions,
and external functions retain their established evaluation order. The planner
accepts at most eight distinct prefetched fields and a bounded expression;
larger expressions fall back automatically.

## Threads and verification

Fixed T uses the existing OpenMP calculation threads, not time-worker processes.
Use `set threads 1`, `4` or `8` as usual. Eligible multi-time expressions can
use the same code inside existing isolated time workers, whose inner calculation
thread count stays one. No worker protocol or default thread setting changes.

`GA_SPATIAL_BATCH=off` disables the new planner while retaining preceding native
fast paths. `GA_SPATIAL_BATCH=force` exercises the otherwise size/backend-gated
paths for numerical regression tests; it does not bypass semantic safety checks.

```sh
pytests/TestSpatialReductions.sh
GA_TEST_TIME_FORMAT=h5 pytests/TestSpatialReductions.sh
GA_TEST_TIME_FORMAT=nc pytests/TestSpatialReductions.sh
GA_TEST_TIME_FORMAT=nc4 pytests/TestSpatialReductions.sh
GA_TEST_TIME_FORMAT=bp5 pytests/TestSpatialReductions.sh
```

The default fixture needs only Python. Other formats use the existing fixture
writer and corresponding development libraries. Each suite compares 1/4
threads in forced, default and disabled modes, including complete printed
doubles, masks/nonfinite values, bounds, locations, DEFINE, and safe fallback.
`GA_SPATIAL_REFERENCE` optionally supplies an older executable for an independent
baseline; by default the same executable with batching disabled is used.

See [performance evidence](PERFORMANCE.md) for measurements and their limits.
