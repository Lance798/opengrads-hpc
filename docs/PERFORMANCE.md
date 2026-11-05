# Calculation performance

opengrads-hpc can use OpenMP for common in-memory grid calculations. The default
is **4 calculation threads**. OpenMP is detected automatically by
`configure`; no extra user action is needed with a supported GCC, Clang, or
vendor compiler.

## Height-coordinate column integrals

[`zint`](ZINT.md) reduces unmodified BP5 inputs by writer block and HDF5 inputs
by physical chunk, with a per-level multiplier for a simple field/profile
product such as `qv*rhobar`. General expressions use the evaluator once per
intersecting Z level. All paths use the same midpoint-derived thicknesses and
skip missing values without changing other cells' weights.

The native readers distribute horizontal tiles to calculation threads after
each read completes. Each tile retains ownership of its columns through the
levels, so the vertical addition order is unchanged. Bounds, horizontal
offsets and missing-value thresholds are reused instead of recalculated for
every native value. Finite layer/bound intersections use explicit comparisons;
this does not enable fast-math. Small chunks remain serial.

Area-average halos at non-periodic file boundaries no longer force native
BP5/HDF5 integrals back to level-by-level reads. Only the intersection with
the file is requested; the existing output bounds, padding and area weights
remain in effect.

On Taiwan VVM's compressed BP5 output, 4096 columns at T=73 matched independent
native-data layer sums. Three process invocations of `zint(qv*rhobar)` and
`zint((qv*rhobar)+0)` had median wall times of 0.187 s and 1.425 s including
startup, about 7.6 times faster for this 64 x 64 region. The direct reader
selected one writer-block region containing 282624 native values. For compressed
`sea_grass_mountain` HDF5 at T=6, a 64 x 64 region selected 198 chunks and 405504
native values; corresponding times were 0.152 s and 0.166 s. The measured speedup
depends on the dataset, compression layout, cache and storage state. These are
observations on this host, not general throughput guarantees.

## Spatial average slabs

Vertical `ave`, `mean`, `sum`, and `sumg` of a bare BP5/HDF5 variable
can read several levels together. Horizontal tiles bound values and masks to
128 MiB, plus the backend's native buffer. A tile of columns is reduced in
level order; changing the OpenMP thread count does not reorder a column's
additions. Partial `-b` bounds and nonuniform/decreasing Z coordinates retain
the evaluator's weights. Unsupported expressions and file layouts use the
existing evaluator. Small NetCDF and binary vertical means retain their
faster plane path.

HDF5 bulk selections also cover X/Z and Y/Z sections, including reversed axes,
packed values, broadcast profiles, templates and missing boundary cells.
Canonical XYZ volumes are read directly into the result buffer. This reduces
API calls, but compressed chunk layout and cache size still determine the
amount of decoding; fewer calls alone do not guarantee faster profiles.

On 5 November 2026, warm-cache three-trial medians on this host were:

| Vertical `ave(u,z=1,z=69)` | Threads | Before (s) | Slabs (s) |
|---|---:|---:|---:|
| Taiwan VVM BP5, 512 x 512 | 1 | 3.273 | 0.237 |
| Taiwan VVM BP5, 512 x 512 | 8 | 3.278 | 0.169 |
| Sea/grass/mountain HDF5, 1024 x 64 | 1 | 0.114 | 0.110 |
| Sea/grass/mountain HDF5, 1024 x 64 | 8 | 0.092 | 0.094 |

These include reads and computation, exclude startup and plotting, and use
CPUs 0--7. HDF5's small difference is within observed run-to-run variability;
this workload remains dominated by gzip decoding. BP5's improvement combines
less repeated decoding with parallel column reduction.

Real-data vertical reductions, partial bounds, and X/Z and Y/Z profiles were
compared against the pre-change executable at 1 and 8 threads: finite values
were identical and masks matched for BP5, HDF5, NetCDF and binary inputs.
`TestReductionIO.sh` additionally checks every vertical reduction against
`field+0` (the evaluator), including templates, reversed axes, compression,
and the existing non-finite-value behavior of each reader.

### Defined Z profiles

`define p=ave(field,x=...,x=...)` (or a Y average) can retain Z through one
section evaluation when the per-level result is scalar. BP5, HDF5, NetCDF
and binary files use this path. Simple spatial sections are fetched before
the first two positions are evaluated, and supply their metadata as well as
values. This avoids reading and decoding the first positions twice.

For `define p=aave(field,x=...,x=...,y=...,y=...)`, BP5 and binary files can
stream horizontal tiles containing all requested levels, sharing coordinate
weights across Z. Only in-file cells are read; the original fractional bounds
and missing-value weights still apply. Levels are independent OpenMP tasks,
with a fixed row/cell addition order at every thread count. HDF5 and NetCDF
keep their faster established plane path. General expressions, cyclic
longitude requests, and unsupported layouts retain the evaluator.

On the same Taiwan VVM BP5 data, a 69-level defined X-mean profile fell from
6.16 s to about 0.090 s (one thread). A direct display of that BP5 profile
takes about 0.046 s. HDF5 X/Z and Y/Z mean profiles take about 0.085 s and
0.042 s respectively with the default cache, versus 0.168 s and 0.130 s
before these changes. A 512 x 512 area-mean profile fell from
10.14 s to 0.364 s (one thread) or 0.314 s (eight). These improvements primarily
remove repeated decompression; scaling with additional calculation threads
is smaller. Real-data profile, fractional-bound and multiple-time comparisons
against the pre-change executable had matching masks and maximum BP5 absolute
difference `1.07e-14`. The synthetic regression also covers reversed axes,
templates and independent evaluator references for each profile.

## Indexed vertical slices

[`zslice`](ZSLICE.md) reads a different Z index at each horizontal point.
Unmodified BP5 fields group selections by writer block, which avoids repeated
decompression of a block for each requested model level. HDF5 groups sparse
element reads by chunk; NetCDF and ordinary binary fields merge adjacent
same-level points into row segments.

On the compressed Taiwan VVMex output, a 64 x 64 `th` slice at T=73 produced
4096 identical values through `zslice(th,topo)` and the expression fallback
`zslice(th+0,topo)`. Three process invocations had median wall times of 0.181 s
and 0.734 s, respectively, including startup: about 4.1 times faster for this
region on this host. The direct selection read one writer block region,
containing 131072 native values. This is an observed comparison for that
dataset and storage state; different block layouts and index maps can change
the result substantially.

The gzip-compressed `sea_grass_mountain` HDF5 output was also tested over its
1024 x 64 domain at T=6. Indexed reads selected 65536 points from 22 chunks;
both indexed and per-level evaluation took about 0.078 s including startup
and produced the same statistics. The HDF5 layout and its existing cache made
the elapsed times similar on this host; chunk grouping is not a universal
speed multiplier.

## Choosing the CPU count

Choose the setting that fits the machine and workload:

```text
ga-> q threads
Calculation threads = 4 (OpenMP enabled)
ga-> set threads 8
Calculation threads set to 8
```

The same choice can be made at startup:

```bash
./opengrads -j 8
GA_NUM_THREADS=8 ./opengrads
```

Precedence is `-j N`, then `GA_NUM_THREADS`, then the default of 4. A script
may change the count at any time with `set threads N`. Use `set threads 1` for
a serial calculation or for output-comparison testing.

Do not automatically select every logical CPU on a shared node. Start with 4,
then benchmark 2, 4, 8, and so on against the real command script. Memory
bandwidth often limits grid arithmetic before all cores are useful.

## How long it will take

A calculation that runs for more than a second shows a progress line on the
terminal while it works: what is running, how far along it is, the
calculation threads at work (`set threads`), how long it has taken, and about
how long is left.

```text
ga-> d ave(t,t=1,t=2920)
Averaging.  dim = 3, start = 1, end = 2920
ave [██████████████░░░░░░░░░░░░░░░░░░░░░░░░░░]  35%  1022/2920  4 threads  0:41, about 1:16 left
```

It follows `ave`, `mean`, `sum`, `sumg`, `min`, `max`, `minloc`, `maxloc`,
`tloop`, `eloop`, `gint`, and `define`. A calculation inside another counts
as part of its step, so `define x = ave(t,t-30,t+30)` over a year of days
shows one line, `define > ave`, that moves as each average does. The line is
taken away before anything else is printed and when the command ends, so the
output reads as before, and it never appears when the output goes to a file
or a pipe, or inside a script's `result`. In iTerm2 (outside tmux) the
progress also shows in iTerm2's own progress bar. Ctrl-C stops the
calculation as usual.

`GA_PROGRESS=off` turns the line off; a number sets how many seconds a
calculation runs before it appears (default 1).

## Accelerated calculations

The implementation parallelizes independent cells in the hot, thread-safe
parts of expression evaluation, including:

- grid/scalar arithmetic, comparisons, powers, masks, `mag`, and `atan2`;
- `sqrt`, trigonometric functions, `abs`, `exp`, `log`, and `log10`;
- `mean`, `ave`, `sum`, `sumg`, `min`, `max`, `minloc`, and `maxloc`;
- `amean`, `aave`, `asum`, and `atot` area reductions;
- `gint`, `vint`, `const`, `cdiff`, and `smth9`.

Small grids remain serial because creating a thread team costs more than it
saves. The current crossover is 32,768 cells. File reads, expression parsing,
coordinate conversion setup, and graphics remain serial because the legacy
core keeps mutable global state in those paths.

HDF5 horizontal maps and lines use a single hyperslab selection, including
templated files, reversed axes, profile broadcasting, native missing attributes
and scale/offset unpacking. Requests crossing a non-periodic boundary read
only valid cells and pad the rest as missing. The temporary selection is
limited to 256 MiB; unsupported shapes retain the row reader. NetCDF already
has a bulk reader; fixed-T/E horizontal requests in templated files can use it
too. Ordinary binary stream files can read a full-width, contiguous horizontal
slab at once, retaining their existing byte-swap, type conversion and missing
tests. Reversed-Y, sequential, templated and other special binary layouts keep
their established reader.

Area reductions use row-wise SIMD sums, avoiding an integer division at each
cell. This calculation improvement applies to BP5, HDF5, NetCDF, binary and
defined grids alike. Addition grouping can change the last floating-point
bits, as with other parallel reductions.

HDF5 numeric attributes are opened on the dataset itself and accept scalar
or single-element dataspaces. This also corrects the older relative-path and
scalar-attribute failures that could leave `_FillValue`, `scale_factor` and
`add_offset` unused. Multi-element attributes are rejected before reading into
a scalar buffer; the existing warning/default behavior applies.

## Time averages of BP5 data

`ave`, `mean`, `sum`, `sumg`, `min`, `max`, `minloc`, and `maxloc` over time
evaluate their expression once per time step, and each step used to be a
separate read. When the expression is a plain variable of a BP5 dataset that
is the default file (`ave(th,t=1,t=1441)`, not `ave(th*2,...)` or a defined
variable), the steps are instead read many at a time: up to 256 steps, or
64 MB, go to ADIOS2 as one batch, which its reader threads serve in parallel,
and they are folded into the result on the calculation threads. Each grid
point still takes its steps in order through the same accumulation code, so
the result matches the step-by-step path; the BP5 regression compares the two
for exact equality at one and at four threads. Steps outside the file and
unsupported expressions/formats go step by step as before.

A sum of two equally shaped, unmodified fields of the default BP5 file also
uses this path, for example
`ave(precip_liq_surf+precip_ice_surf,t=2,t=145)`. The 64 MiB calculation-buffer
budget is shared between the two inputs. Their masks are intersected and the
addition is evaluated separately at each time before accumulation. It is not
rewritten as `ave(a)+ave(b)`, which would change the result when the two
fields have different missing samples. General expressions, defined grids
and other formats continue to use their existing evaluator paths.

BP5 reads of a vertical section (x-z or y-z) and of a profile are also one
read per step now, rather than one per level.

On a 96 × 96 × 300 dataset of 241 steps, written as 16 blocks per step the way
a 16-rank run writes it, with the data in the page cache and 4 threads:

| Request | Before | After |
| --- | --- | --- |
| x-z section, `ave(th,t=1,t=241)` | 5.5 s | 0.06 s |
| profile, `ave(th,t=1,t=241)` | 0.19 s | 0.01 s |
| x-y map, `ave(th,t=1,t=241)` | 0.04 s | 0.02 s |
| `define m = ave(th,t=1,t=241)` over all 300 levels | 13.6 s | 3.3 s |

The 3-D `define` evaluates the average one level at a time, so it remains
bounded by reading 300 separate planes per batch of steps; with one thread it
takes 4.0 s.

## Averages over x, y and z

An average over x, y or z also evaluates its expression once per position:
`ave(ave(th,x=1,x=256),y=1,y=256)` drawn as a profile read a column of `th`
for every point of the plane, 65,536 reads, and with BP5 each of them went
through the whole block the column lies in. Calculation threads do not help
with that, since the time goes into the reads. Now, when the expression is a
plain variable of the default file and the result varies in one dimension at
most (a horizontal-mean profile, a zonal-mean line, a single value), every
position is read at once, as one section of up to 256 MB, and averaged from
memory. Every file format takes this path. Weights (latitude, `-b` bounds),
wrap-around in longitude and positions outside the file are handled as
before, and `pytests/TestAveSection.sh` checks that each result matches, to
the last bit, the one taken a position at a time. Any other expression, or a
map or section as the result, still reads a position at a time.

On a BP5 dataset of 256 × 256 × 100, the profile
`d ave(ave(th,x=1,x=256),y=1,y=256)` with `set z 1 100` took 12.8 s at one
thread and at four; it now takes 0.6 s, start-up included.

For a time-mean profile, average over time first and over the plane after:

```text
ga-> define tm = ave(th,t=1,t=241)
ga-> set x 1
ga-> set y 1
ga-> d ave(ave(tm,x=1,x=96),y=1,y=96)
```

The `define` reads many times at once (see above), and the profile is then
taken from memory.

## Reproducibility of parallel reductions

Parallel reductions can differ from a one-thread result in the last few
floating-point bits because additions may be grouped differently. Missing
value masks and scientific semantics are unchanged. Use an appropriate
tolerance rather than requiring bit-for-bit equality for nontrivial sums.

## Build controls and verification

Force a serial build when required:

```bash
./configure --disable-openmp
```

To require OpenMP and fail configuration when it is unavailable:

```bash
./configure --enable-openmp
```

The configuration banner and `q config` report whether OpenMP was compiled
in. After building `grads` and `libgxdummy.la`, run:

```bash
OPENGRADS_BUILD_ROOT=/path/to/build ./pytests/TestOpenMP.sh
```

The test exercises a 256x256 grid, checks the four-thread default and runtime
controls, and compares common calculation output at one and four threads.

For reader and reduction changes, also run:

```bash
OPENGRADS_BUILD_ROOT="$PWD/build" \
OPENGRADS_ADIOS2_ROOT=/path/to/adios2 ./pytests/TestReductionIO.sh
```

This creates 256x256 grids with six times and native blocks/chunks large
enough to exercise column parallelism. It compares masks and double-precision
values at 1, 2, 4 and 8 threads across binary, NetCDF, compressed NetCDF4,
HDF5, BP5 and compressed BP5 where their fixture dependencies are available.
Cases include area-average halos, reversed axes, template file changes,
time-varying missing masks, partial-height integrals, boundary time weights,
packed HDF5 integers, profile broadcasting and missing NetCDF templates.
`OPENGRADS_TEST_HDF5_ROOT` and `OPENGRADS_TEST_NETCDF_ROOT` select nonstandard
fixture dependencies. Native ZINT and two-field time averages must match
their evaluator fallbacks exactly; cross-format area reductions allow
`1e-8 + 2e-12*abs(reference)` for changed addition grouping.

## Reproducible large-case benchmark

### November 2026 reduction checks

The current changes were checked against a clean rebuild of commit `0b076ad`,
using GCC 14.3 (`-O2 -fopenmp`), serial ADIOS2 2.11 and CPU HDF5 1.14.6.
The following are three-run medians on a shared Derecho host, bound to CPUs
0-7, with `OMP_PROC_BIND=close` and `OMP_PLACES=cores`. Timings include reads
and calculation, but exclude startup, dataset opening, correctness exports
and drawing. File caches were warm; these are not cold-storage throughput
or dedicated-node scaling guarantees.

| Workload | Grid / times | Threads | Before (s) | After (s) |
|---|---|---:|---:|---:|
| BP5 precipitation `ave(a+b)*86400` | 1024x1024, T=2:145 | 4 | 1.402 | 0.837 |
| BP5 CWV time mean | 512x512, 69 levels, T=73:76 | 8 | 1.732 | 0.282 |
| BP5 `zint(qv*rhobar)` | 1024x1024, 69 levels, T=73 | 8 | 1.728 | 0.276 |
| BP5 precipitation `aave(a+b)` | 1024x1024, T=73 | 8 | 0.395 | 0.016 |
| BP5 `aave(zint(qv*rhobar))` | 1024x1024, 69 levels, T=73 | 8 | 1.734 | 0.275 |
| HDF5 CWV time mean | 1024x64, 99 levels, T=6:9 | 8 | 1.480 | 1.147 |
| HDF5 wind read + area mean | U/V, 1024x64, 4 levels, T=6:7 | 4 | 0.0338 | 0.0320 |

BP5 is Taiwan VVM `taiwanvvm_1024_lc20220623_8192`; HDF5 is the compressed,
templated `sea_grass_mountain` output. The older BP5 nested-area timing uses
the numerically equivalent workaround of first defining the column map and
then averaging it, including both steps. The new timing uses the direct
nested expression. Wind timings similarly include loading the small U/V
Z/T cubes and then reducing them; they do not represent the old pathological
direct BP5 boundary request. BP5 wind reading remains about 3.1 s in both
versions: repeated vertical-plane reads still dominate that workflow.

At one thread the BP5 full-map integral fell from 1.737 to 0.611 s; at eight
threads it takes 0.276 s. Thus both the serial kernel and its parallelism
contribute to the improvement. For this precipitation case four threads
were faster than eight (0.837 versus 0.859 s). HDF5 gains are smaller because
chunk reads and decompression dominate. Its original chunk shape is
`(1,64,512)`; matching memory and file dataspace ranks avoids a regression
when reading complete horizontal planes.

Synthetic 256x256, six-time binary fixtures at four threads reduced
`ave(f+g)` from 0.00534 to 0.00202 s. A NetCDF template area average fell
from 0.00258 to 0.00115 s; a regular NetCDF CWV time mean fell from 0.01709
to 0.01418 s. These medians use ten repetitions per trial and three trials.
Ordinary NetCDF already has bulk reads; small NetCDF time averages and area
reductions did not consistently improve. These results do not substitute
for measurements on a user's own NetCDF/binary layout.

A mixed BP5 process retaining the precipitation/CWV maps and both small U/V
cubes peaked at 667.3 MiB RSS before and 672.9 MiB after (eight threads,
one process invocation each). The time-batch budget does not require
materializing the complete XYZ/T source cube; total process memory also
includes defined grids, reader metadata, native buffers and libraries.

Double-precision NetCDF exports were compared cell by cell before and after
at 1, 2, 4 and 8 threads, including full 1024x1024 precipitation/CWV maps,
partial columns, land masks, nested column means and Z/T U/V means. All
precipitation and CWV grid values were identical, and all missing masks
agreed. The largest real-data area-reduction absolute difference was
`9.50e-12` (maximum relative difference across the checks `4.26e-13`).
Independent ADIOS2/HDF5 reads also matched 4096 full and partial columns
per dataset exactly. Packed HDF5 inputs are checked against independent
unpacked values, because the previous scalar-attribute behavior was wrong.

`TestReductionIO.sh` compared 30,801,238 synthetic values; maximum absolute
and relative differences were `5.59e-9` and `1.55e-15` respectively, with
matching masks. The larger absolute difference occurs in large-magnitude
reductions. The changed calculation/readers passed ASan/UBSan checks (legacy
global-allocation leak reporting disabled). This host's verification covers
HDF5, NetCDF and stream binary data; HDF4 was not enabled.

### Mixed synthetic workload

Run the supplied benchmark after building `grads` and `libgxdummy.la`:

```bash
OPENGRADS_BUILD_ROOT="$PWD/build" ./benchmarks/BenchmarkOpenMP.sh
```

It creates a sparse 2048 x 2048 float dataset with six time steps, warms the
file cache, and runs six repetitions of a mixed workload: elementwise
transcendental arithmetic, `amean`, `aave`, `asum`, `mean`, and `sum`. It then
reports the median of three complete runs at one CPU and at the selected CPU
count. Override its defaults with `OPENGRADS_BENCH_THREADS`,
`OPENGRADS_BENCH_SIZE`, `OPENGRADS_BENCH_TIMES`,
`OPENGRADS_BENCH_REPEATS`, and `OPENGRADS_BENCH_TRIALS`.

On 22 August 2026, the default case produced this result on an aarch64 system
where `nproc` exposed five CPUs (heterogeneous Cortex-X925/Cortex-A725 host):

| Calculation CPUs | Median seconds | Speedup |
|---:|---:|---:|
| 1 (original/single CPU) | 9.31 | 1.00x |
| 4 | 5.05 | 1.84x |

This is an end-to-end result for one synthetic workload, not a universal
scaling promise. File access, parsing, allocation, and other legacy global
state remain serial, so pure arithmetic kernels can scale better while
I/O-heavy scripts can scale less.
