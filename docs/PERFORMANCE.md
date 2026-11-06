# Calculation performance

opengrads-hpc can use OpenMP for common in-memory grid calculations. The default
is **4 calculation threads**. OpenMP is detected automatically by
`configure`; no extra user action is needed with a supported GCC, Clang, or
vendor compiler.

The November 5 measurements below were collected on the shared Derecho host.
The November 6 RLL profile diagnosis uses the new Xeon machine and different
data; its timings are recorded separately below.

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

The spatial/profile/time comparisons below use `6cc0735` as their preceding
implementation unless noted. Reads and computation are included; startup,
plotting and (for defined arrays) preload are excluded. Overall improvement
from changing the reader is distinct from scaling with calculation threads.

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

### Averages of defined arrays

A bare defined variable passed to an area reduction uses a private read-only
view, with the source's row stride and clipped global coordinates. The view
never enters the expression evaluator. All-missing regions, cropped offsets,
fixed Z/T/E broadcasts and the source mask keep their existing meaning;
climatological arrays and unsupported shapes retain the evaluator.

Defined area-mean profiles share X/Y weights across Z and distribute complete
levels to OpenMP threads. No parser, allocator or file-library call runs on a
worker. Defined X/Z and Y/Z sections also use the spatial section path.

Three-trial warm-memory BP5-derived results (preload excluded):

| Defined calculation | 1 thread (s) | 4 threads (s) | 8 threads (s) |
|---|---:|---:|---:|
| `aave`, 1024 x 1024 plane | 0.00357 | 0.00093 | 0.00050 |
| `aave`, 512 x 512 x 69 profile | 0.06131 | 0.01614 | 0.00819 |

These show calculation scaling after data is resident; they do not include
the potentially much larger cost of initially defining the source cube.
Real BP5/HDF5/NetCDF/binary comparisons of cropped views, broadcasts and all
area reduction variants matched the pre-change executable exactly in the
tested cases. Regression tests also compare against `defined+0`, check source
values before/after averaging, and exercise simultaneous Z/T variation.

### Resident section calculations

A spatial section is folded in one OpenMP region, with one owner per output
point. Coordinate weights are computed once. Every point preserves its
position-by-position addition order and min/max location tie rule; reductions
never merge partial sums for the same point. On the tested 1024 x 69 defined
X/Z section, the X-mean command fell from about 0.95 ms to 0.47 ms at one
thread (0.40 ms at eight); reading/copying the defined section still dominates
this small calculation. All eight reduction selectors and fractional bounds
matched the pre-change executable exactly at one and eight threads on BP5,
HDF5, NetCDF and binary data.

### Bounded time batches

Long time means of bare NetCDF/HDF5 fields, including equally shaped `a+b`,
can fill a bounded batch directly from the existing bulk readers. BP5 keeps
its native step batching. All file-library calls run on the main thread;
workers fold each point's times in the original order. For two templated
fields, reads are interleaved within each time to reuse the open file.
The values/masks budget is 64 MiB across both fields. Time increments,
fractional `-b` weights and missing template times retain their meaning.

Short non-BP5 means (fewer than eight remaining times) and binary time means
retain the established path because batch-buffer overhead erased the gains.
A 256 x 256, 32-time NetCDF mean measured 0.0176/0.0161 s before and
0.0174/0.0138 s after at one/eight threads. HDF5 remained primarily limited
by decompression, with mixed small changes across thread counts; batching is
not a promise of faster I/O. The regression fixture has 12 times so it checks
active batches, template reopens, missing middle times, pairwise masks,
increments and boundary weights. Real-data time means matched the pre-change
executable exactly at one and eight threads in the checked cases.

### Area-mean time series

For a scalar area mean with fixed Z and varying T,
`define p=aave(field,x=...,x=...,y=...,y=...)` can read bounded time batches
and reduce independent times on calculation threads. Defined arrays share
coordinate weights and expose only private read-only views across T. File
readers remain serial; the first scalar already evaluated by `define` is
reused. Non-BP5 workspace is adjusted to the worker count, within 64 MiB.
Short/lower-thread NetCDF/HDF5 requests retain their established path; native
time batching there requires at least eight times and four calculation
threads. Unsupported expressions (including nested `aave(zint(...))`),
periodic requests and other layouts retain the evaluator.

For 256 x 256 binary data over 32 times, a warm three-trial median at four
threads fell from 0.0160 s to 0.0058 s (0.0183 to 0.0069 s at eight). Clipping
the non-periodic halo also allows complete binary planes to be read instead
of individual rows. BP5's tested short series remains limited by decoding;
more calculation threads alone did not improve its wall time. A 32-time
1024 x 64 HDF5 series measured 0.0776 to 0.0563 s at four threads, with smaller
or mixed changes at other counts.

Time-series verification covers partial cells, surface fields, defined-array
broadcasts, different ensemble members, missing template times and reopening
after an incompatible native grid. The latter also checks failed-define
ownership and dimension restoration. The final 12-time synthetic suite
compared over 56 million finite values at 1/2/4/8 threads with identical masks;
maximum absolute/relative differences were `1.49e-8` and `3.24e-15` (the large
absolute difference is in large-magnitude totals). ASan/UBSan passed for the
changed reader, reduction and define units, including failure recovery;
legacy global-allocation leak reporting was disabled.

### Binary boundary slabs

An ordinary stream-binary XY request spanning the complete X axis can now
read its in-file rectangle once and pad missing boundary rows/columns. Values
and masks use at most 64 MiB of extra workspace. Templates and byte swapping
use the existing row reader's positioning/conversion rules; sequential
records, reversed Y and periodic X extensions keep the established path.
No scientific arithmetic changes in this reader.

For a 256 x 256 binary plane, the file area mean measured about 0.000371 s
at one thread and 0.000277 s at four; the earlier implementation measured
about 0.000602 s at one thread. The 32-time binary series subsequently
measured 0.00554 s at four threads. These small synthetic timings do not
predict throughput for every binary layout or storage system.

`TestBinaryHalo.sh` independently checks 524332 finite values, native/swapped
byte order, missing boundary masks, out-of-file Y requests, nonuniform-height
integrals and weighted Z/T profiles at one/eight threads. Grid reads and
integrals are exact; maximum absolute profile difference from the analytic
reference was `3.64e-12`. Both this test and the complete cross-format suite
passed ASan/UBSan after the binary reader change.

Final checks on the actual datasets included:

| Command/workflow | Previous 1 thread (s) | Current 1 thread (s) | Current 8 threads (s) |
|---|---:|---:|---:|
| BP5 defined area profile, 1024 x 1024 x 69 | 13.816 | 0.672 | 0.484 |
| HDF5 X-mean profile, 1024 x 69 | 0.170 | 0.0868 | 0.0863 |
| HDF5 area profile, 1024 x 64 x 69 | 0.135 | 0.134 | 0.115 |

The HDF5 rows were remeasured with both executables in the final session;
area-profile throughput is essentially unchanged, while the X/Z section
avoids duplicate decoding. The preceding BP5 measurement is from the
original audit on the same data. Thread scaling is strongest for resident
area reductions; native compressed reads remain a limiting factor.

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

### November 6 RLL profile diagnosis

Commit `92c934a` was rebuilt on an Intel Xeon Platinum 8481C host (two sockets,
104 physical cores, 208 logical CPUs), with GCC 13.4, HDF5 2.2.0, NetCDF 4.10.1
and serial ADIOS2 2.11.0. `TestOpenMP.sh`, `TestReductionIO.sh` and
`TestBinaryHalo.sh` passed in that order. The reduction test compared
56,284,434 finite values with matching masks; the analytic binary test compared
524,332 values with maximum absolute difference `2.73e-12`.

The new input is
`/data/Aaron/VVMex_run/output/rll_tropical_0.25_sam/visualization/vvm_plot.ctl`:
1536 x 384 x 44, one HDF5 file per hour. The `u` field is float32 with
`(1,128,512)` chunks, shuffle, gzip level 1 and Fletcher32. The requested
workflow is:

```text
set lon 0
set lat 0
set z 1 44
set t 1 2400
define ua=ave(u,lon=0,lon=90)
define ua2=aave(u,lon=0,lon=90,lat=-10,lat=10)
```

For diagnosis, T=2397:2400 was primed once and measured twice per thread count,
in forward/reverse thread-count order, with `OMP_PROC_BIND=spread` and
`OMP_PLACES=cores`. Median seconds below include the DEFINE's reads and
calculation, excluding process startup and correctness output. They are short
warm-cache diagnostics, **not full T=1:2400 measurements**.

| Calculation threads | `ua` (s) | `ua2` (s) |
|---:|---:|---:|
| 1 | 0.2310 | 0.2592 |
| 4 | 0.2321 | 0.2558 |
| 16 | 0.2292 | 0.2566 |
| 32 | 0.2312 | 0.2786 |
| 64 | 0.2289 | 0.3075 |
| 128 | 0.2366 | 0.3757 |

The HDF5 calls account for about 99% of `ua` and 91% of one-thread `ua2` wall
time. `ua` already reads a complete X/Z section per time (five HDF5 reads for
four times, including the initial shape evaluation). Its small resident
sections do not reach the OpenMP size threshold. HDF5 `ua2` retains the plane
path: 176 reads for four times and 44 levels. Its roughly 34,000-cell planes
barely cross the parallel threshold and provide only 88 reduction rows.
Large thread teams therefore add overhead to a small calculation while the
reader remains serial. At 128 threads, `ua2` consumed about 47.35 CPU seconds
in 0.376 wall seconds. `OMP_WAIT_POLICY=PASSIVE` reduced that CPU cost to 2.84
seconds, but did not improve wall time (0.408 seconds).

An independent-process prototype split T=2273:2400 into contiguous intervals,
using one calculation thread per process. A full serial pass primed the input
and supplied the reference. The following are one measured pass per worker
count, including process startup and correctness output; they are separate
from the DEFINE-only timings above.

| Time workers (processes) | `ua` (s) | `ua2` (s) |
|---:|---:|---:|
| 1 | 7.341 | 8.285 |
| 4 | 1.946 | 2.249 |
| 16 | 0.546 | 0.598 |
| 32 | 0.324 | 0.348 |
| 64 | 0.234 | 0.252 |
| 128 | 0.247 | 0.257 |

Each result contains 5632 values; all values and masks matched the serial
reference exactly. The process prototype demonstrates useful concurrency
across time files, reaching about 31--33x at 64 workers in this short case.
It is **not an implemented `set threads` feature** or evidence of the same
speedup over all 2400 times. The plateau also argues against assuming that
128 workers will outperform 64.

The next implementation should isolate reader state and schedule independent
time blocks. The current evaluator and `gaio.c` globals cannot be called from
arbitrary OpenMP workers. This HDF5 build enables thread safety but disables
concurrency support, so separate handles alone do not establish concurrent
HDF5 decoding. Independent reader processes are one option; another is a
bounded producer of compressed chunks with parallel decoding outside HDF5,
preserving filters, checksum validation, packing, masks and fallback behavior.
Within one time, column/row calculations and level reductions can use threads,
but substantial speedup here requires parallel decoding as well. Batching all
Z levels may reduce calls and thread-team overhead; it does not by itself
remove gzip work. If both profiles are computed together per time, their
overlapping `u` chunks also offer an opportunity to share decoding.

Raw logs, diagnostic scripts and JSON samples are kept locally under
`tmp/new-machine-baseline/`. Original Derecho backups remain under
`tmp/tmp/ave-scenarios/` and `tmp/tmp/average-stages/`.

### 256-time RLL scaling follow-up

The same Xeon host and executable were measured over **T=2145:2400 (256
times)**, using 1, 4, 8, 16, 32, 64 and 128 calculation threads or independent
time processes. Each setting has three measured trials; tables show medians
in seconds. A serial pass primed each case/method and supplied a double-
precision reference. The six originally requested counts alternate forward
and reverse order between trials. The subsequently requested 8-CPU setting
was measured separately against the same references after the main sweep.
The input resides on XFS; operating-system caches were warm. These are not
cold-storage measurements or a full T=1:2400 run.

`ua` and `ua2` are the commands above, retaining 44 Z levels and all 256 times.
The added conditional profile is:

```text
define qcavg=aave(maskout(qc,(qc>1e-5)-0.5),lon=0,lon=90,lat=-10,lat=10)
```

Only cells with `qc>1e-5` contribute values and area weights. Of the 11264
Z/T outputs, 6256 are finite and 5008 are undefined because no cells qualify.
This is a conditional mean, with masked cells excluded from the denominator.

The two integral cases use `set lon 0 90`, `set lat -10 10`, `set z 1`,
the same 256-time range, and `define zu=zint(u)` or
`define cwv=zint(qv*rhobar)`. `zint` integrates all 44 source levels; fixing Z
prevents DEFINE from computing the same column integral for every Z output.
Each result contains 386 x 88 x 256 values. No drawing is timed.

Current **single-process OpenMP**, timing DEFINE reads/calculation, excluding
startup and correctness exports:

| Calculation threads | `ua` | `ua2` | Conditional `qc` | `zint(u)` | CWV `zint` |
|---:|---:|---:|---:|---:|---:|
| 1 | 14.364 | 16.268 | 13.726 | 27.763 | 26.973 |
| 4 | 14.322 | 16.251 | 11.998 | 27.801 | 26.877 |
| 8 | 14.459 | 16.245 | 11.573 | 27.718 | 27.028 |
| 16 | 14.318 | 16.397 | 11.702 | 27.711 | 26.951 |
| 32 | 14.387 | 17.541 | 12.766 | 27.623 | 26.857 |
| 64 | 14.308 | 19.665 | 14.946 | 27.612 | 26.801 |
| 128 | 14.314 | 23.895 | 18.833 | 27.692 | 26.813 |

Independent **time-process prototype**, including process startup and
double-precision NetCDF exports, excluding subsequent comparison. Every
process has one calculation thread and a disjoint contiguous T interval:

| Time processes (1 thread each) | `ua` | `ua2` | Conditional `qc` | `zint(u)` | CWV `zint` |
|---:|---:|---:|---:|---:|---:|
| 1 | 14.447 | 16.457 | 13.801 | 27.678 | 26.578 |
| 4 | 3.775 | 4.279 | 3.576 | 7.335 | 7.235 |
| 8 | 1.976 | 2.275 | 1.877 | 3.779 | 3.729 |
| 16 | 1.077 | 1.179 | 0.978 | 2.077 | 1.931 |
| 32 | 0.640 | 0.693 | 0.592 | 1.189 | 1.162 |
| 64 | 0.428 | 0.471 | 0.376 | 0.971 | 0.969 |
| 128 | 0.403 | 0.410 | 0.406 | 0.699 | 0.656 |

The two tables use different timing scopes. Speedups must use the matching
one-thread or one-process baseline. The process table demonstrates a possible
execution strategy; it is not functionality currently selected by
`set threads`. Original input files were never modified.

Across the 210 measured runs, 731,656,800 finite values were
compared. Masks matched throughout; maximum absolute/relative differences
were `6.39488e-14` and `2.09299e-13`. Independent time processes
matched their serial references exactly. OpenMP area sums can differ in the
last bits. All comparisons used double-precision exports and the existing
`1e-8 + 2e-12*abs(reference)` criterion.

The raw probe's `ru_maxrss` can retain the Python parent's earlier high-water
mark through fork/exec, so those RSS values are not used to estimate actual
worker memory. The 8-count supplement instead reads the executed worker's
`/proc/self/status` VmHWM. A sum of per-process peaks is not a measurement of
simultaneous total memory or proportional shared-library usage.

#### Optimization priorities for 32--128 CPUs

1. **Parallelize independent times, including reads and decompression.**
   Build a bounded time execution plan for these file-backed profiles and
   integrals. The existing process prototype is the measured starting point;
   persistent workers and direct result transfer can remove per-command
   startup and intermediate NetCDF overhead. Keep workers' file/evaluator
   state isolated and results in fixed T/Z order. Arbitrary concurrent
   `gaexpr`/`gaio.c` calls remain unsafe.
2. **Treat the requested CPU count as a total budget.** Most of these time
   workers should use one calculation thread, with a small inner team only
   for sufficiently large resident work. Cap teams by useful rows/tiles and
   minimum work per thread; avoid multiplying an outer worker count by the
   full requested inner count. The measurements support useful 16/32/64
   time concurrency, with workload-dependent further benefit at 128. This
   machine has 104 physical cores, so 128 also uses SMT.
3. **Remove repeated work in common expressions.** A narrow fused
   `aave(maskout(field,condition),...)` path can read the field once, test the
   condition and accumulate valid area weights without intermediate grids.
   Here the evaluator makes 22528 HDF5 calls for the conditional profile,
   versus 11264 for a bare area profile; cache hits mean this is not proof
   of twice the decompression. For `zint(qv*rhobar)`, bulk-reading the density
   profile could replace 44 scalar profile reads per time with one. Preserve
   strict thresholds, missing masks, packing and all-masked behavior.
   These `ua`, `ua2` and `zint(u)` requests also intersect the same physical
   `u` chunks. An explicit multi-output time plan or a bounded resident
   source cache could share their decoding; arbitrary future commands cannot
   be assumed when evaluating the first DEFINE.
4. **Prototype thread-parallel decoding for few-time, large-volume cases.**
   Reader-local contexts are necessary but this HDF5 build's lack of
   concurrency also matters. An explicit raw-chunk read/decode pipeline
   would need bounded buffers, filter-mask handling, shuffle/deflate and
   Fletcher32 validation, type conversion and the established fallback for
   unsupported layouts. Measure producer throughput before assuming it
   can feed 128 decoder threads. Batching reads alone does not parallelize
   gzip decoding.

A follow-up production implementation should be checked over 1024 or all
2400 times, with throughput and CPU cost as well as latency, bounded memory,
template changes, failure recovery and Ctrl-C cleanup. Warm-cache scaling
should remain separate from storage throughput. On this host, testing near
the 104 physical-core count can help distinguish SMT effects from scheduler
or reader limits.

Local artifacts are `tmp/new-machine-baseline/suite256/summary.csv`,
`summary.json`, `scaling.png` and `scaling.pdf`. The figure shows three-trial
medians with observed min/max bars, not confidence intervals. Diagnostic
drivers are `benchmark256.py` and `summarize256.py` in the parent directory;
the added 8-count records are in `suite256-eight/`.

#### Isolated HDF5 memory-rank experiment

A diagnostic preload shim, with the production executable unchanged, replaced
the one-dimensional memory dataspace of each dense rectangular chunk read
with a dataspace whose rank/extents match the file selection. HDF5 still
performed native type conversion, decompression and checksum validation.
Over the same 256 times, with one calculation thread and three measured trials:

| Integral | Existing (s) | Matched rank (s) | Speedup |
|---|---:|---:|---:|
| `zint(u)` | 27.763 | 17.597 | 1.58x |
| `zint(qv*rhobar)` | 26.973 | 16.538 | 1.63x |

All 52,174,848 compared values and their masks matched
the baseline exactly. Each run remapped 22528 chunk reads; read-call and
selected-value counts were unchanged. The CWV density profile's 11264 scalar
reads were retained. This is evidence for a small reader improvement separate
from time concurrency: use `H5Screate_simple(rank,count,NULL)` for the chunk
memory dataspace in `gah5zint`, then rerun the cross-format/ZINT regressions
before integrating it. These timings describe the original isolated experiment.

Stage 1 subsequently applied this memory-dataspace change to `gah5zint`.
`TestOpenMP.sh`, `TestReductionIO.sh`, `TestBinaryHalo.sh` and `TestZInt.sh`
passed after rebuilding. The production executable was compared with the
original 256-time double-precision references at 1 and 128 threads for both
integrals: all 34,783,232 values and masks matched exactly. The one-thread
`zint(u)` verification took 17.658 s, consistent with the isolated experiment.
The arithmetic, native filters, chunk selection and workspace budget are
unchanged. Stage logs are under `tmp/acceleration-stages/stage1/`.

The diagnostic is `tmp/new-machine-baseline/h5_rank_probe.c`; records and a
separate `hdf5-rank.png`/PDF are in `tmp/new-machine-baseline/suite256-rank/`.

#### Display is part of the optimization target

The 256-time scaling figure above is complete for DEFINE; it does not time
interactive graphics. Additional single-run diagnostics used `gxdummy` for
graph construction and Cairo for 1200 x 800 PNG output. They cover both Z/T
profiles and fixed-time XY fields, with four calculation threads. They do
not measure X11/terminal transport or the viewer's rendering latency.

Direct Z/T display needs its own execution path. With `set lon 0`,
`set lat 0`, `set z 1 44`, `set t 2399 2400`, a direct
`d ave(u,lon=0,lon=90)` did not finish within a 10-second process budget.
The existing section fast path excludes two varying result dimensions.
Direct `d aave(...)` and `d aave(maskout(...),...)` reject varying Z/T in
the current implementation. These are different from DEFINE, which fixes
Z/T for the inner evaluation and assembles the requested result.

For a defined 44 x 256 Z/T result over T=2145:2400:

| Result | Read + compute DEFINE (s) | Cached shaded graph (ms) | Cairo PNG (ms) |
|---|---:|---:|---:|
| `ua` | 14.335 | 2.41 | 28.84 |
| `ua2` | 16.360 | 2.05 | 28.92 |
| `qcavg` | 11.994 | 2.86 | 35.24 |

For XY fields at T=2400, lon=0:90 and lat=-10:10 (`maskedqc` uses Z=10):

| Field | Direct statistics (ms) | Direct shaded graph (ms) | Cached shaded graph (ms) | PNG (ms) |
|---|---:|---:|---:|---:|
| `cwv` | 111.58 | 110.22 | 7.66 | 39.03 |
| `maskedqc` | 3.26 | 3.35 | 3.05 | 29.86 |

`maskedqc` is `maskout(qc,(qc>1e-5)-0.5)`. Direct/cached outputs matched;
the full Z/T grids and the XY CWV field also matched the earlier double-
precision references within their reduction tolerance. Subsequent direct XY
evaluations can reuse the open dataset's decompressed chunks, particularly
for the small single-level masked field; these are diagnostic stage timings,
not independent cold-read trials.

The shared optimization target is therefore an evaluation plan used by both
DEFINE and DISPLAY. It should assemble transient Z/T results with the same
bounded time workers, mask semantics and metadata, then hand the finished grid
to the existing main-thread graphics path. Direct averages should not require
users to materialize a permanent defined variable to get the accelerated
reader. Keep graphical state and Cairo/X11/terminal calls on the main thread.
For sequences of XY frames, bounded computation/prefetch and ordered display
can overlap; single-frame PNG and interactive transport costs need their own
measurement after data preparation is faster. Tiny masked XY fields already
spend more time on PNG output than on calculation.

Artifacts are under `tmp/new-machine-baseline/display/`: `display-stages.png`
and PDF, timing records, and example `ua.png`, `ua2.png`, `qcavg.png`,
`cwv.png`, `maskedqc.png`. These display diagnostics are separate from the
three-trial seven-count scaling sweep and the isolated HDF5 rank experiment.

#### Stage 2: integrated time workers for DEFINE and DISPLAY

The standalone process experiment above has now been integrated as a bounded,
persistent pool of headless GrADS processes. Each worker owns one complete
time, including HDF5 reads/decompression and the spatial calculation, and uses
one calculation thread. The parent assembles values and masks in their original
T/Z order and alone owns graphics and the progress bar. Workers start with
`posix_spawn`, so no GrADS parser or file-library state is shared between
processes. They communicate directly over sockets; intermediate files are not
part of this execution path.

`set threads N` is a total CPU budget for this path. The command chooses its
worker count before dispatch, capped by N, the number of times, 128 workers and
a memory estimate. It does not change from 64x1 to 32x2 halfway through a
calculation. Workers currently always use one thread. The pool is reused across
eligible DEFINE/DISPLAY commands and released on a changed CPU budget, close,
reset, reinit, quit, cancellation or worker failure. Child CPU affinity is
restored to the allocation represented by the parent's OpenMP places, rather
than inheriting just the calling thread's single-core binding.

This first integration targets read-only expressions over the default HDF5
grid descriptor: top-level `ave`, `mean`, `aave` or `zint`, with a restricted
set of nested arithmetic/built-ins, including `maskout`. Defined inputs,
file-qualified expressions, plug-ins and unsupported syntax retain the
ordinary evaluator. Parallel DEFINE needs at least 32 output times, one
ensemble member and a sufficiently large source domain. Each time result is
capped at 128 MiB. The worker estimate includes buffers and two dataset caches,
with an aggregate ceiling of the smaller of 8 GiB and one quarter of currently
available system memory. This is a workspace estimate, not an OS-enforced
resident-memory limit; the existing parent result allocation remains separate.
Set `GA_TIME_WORKERS=off` to use serial time execution, or `GA_TIME_TRACE=1`
to report the selected workers and their PIDs. Other formats and non-POSIX
builds keep their existing paths.

Direct Z/T DISPLAY now assembles a temporary result with the same per-time
evaluator; a permanent DEFINE is unnecessary. Short requests can use serial
assembly. The original main-thread graphics code consumes that finished grid.
The initial direct two-time checks for `ave(u)`, `aave(u)` and conditional
`aave(qc)` all complete in approximately 0.11--0.13 s and agree with their
defined references. Cached graph/PNG timings remain a separate measurement.
Fixed-time XY calculations keep their existing evaluator.

This stage parallelizes *output times*. An expression such as
`mean(u,t=1,t=256)` that reduces many input times into a single output still
uses the existing time-batch reader; parallelizing those input reads is the
stage described below. Likewise, a single-time large-domain calculation cannot use
multiple time workers.

`TestTimeWorkers.sh` checks regular/template HDF5, reversed axes, fractional
spatial bounds, masks, surface broadcasting, ZINT, direct DISPLAY, identical
rendered PNGs, descriptor symlinks and pool reuse. Its 28444 printed reference
samples match exactly, including missing masks. PTY tests verify monotonic
worker progress, iTerm2 clearing, progress-off mode, Ctrl-C, killed-worker
recovery and child cleanup. The existing OpenMP, ReductionIO, BinaryHalo,
ZInt, AveSection, Progress, Undo, DefineNames, Saturation and ClearCanvas
regressions also pass (the optional original-P3 source comparison is skipped
without its external source/compiler settings).

The integrated 256-time sweep uses the same T=2145:2400 double-precision
references as the original diagnosis. All 365933568 compared values (365828400
finite) and masks match exactly across 105 measurements: five cases, seven
CPU budgets and three trials. Medians in seconds, with a fresh pool per trial:

| Case | 1 CPU | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `ua` | 14.314 | 3.671 | 1.975 | 1.192 | 0.841 | 0.808 | 1.218 |
| `ua2` | 16.194 | 4.129 | 2.177 | 1.272 | 0.899 | 0.839 | 1.223 |
| `qcavg` | 13.970 | 3.540 | 1.863 | 1.081 | 0.804 | 0.767 | 1.219 |
| `zu` | 17.501 | 4.496 | 2.385 | 1.413 | 1.007 | 0.972 | 1.360 |
| `cwv` | 16.567 | 4.302 | 2.285 | 1.377 | 0.986 | 0.958 | 1.334 |

These timers include pool startup/configuration and computation, but exclude
result export. They describe warm-cache execution and cannot establish cold
storage throughput. The probe's CPU, HDF5-call and VmHWM counters cover the
parent only after integration; do not interpret them as total worker CPU,
total reads or aggregate memory. The elapsed-time scaling remains directly
measured. Records, summary CSV/JSON and the new figure are in
`tmp/new-machine-baseline/stage2-production/scaling.png` (also PDF).

Separate 256-time trials issue four commands in the same process and verify
that all four report identical worker PIDs. For `ua2`, three-trial medians
after the first command are approximately 0.441 s at 64 workers and 0.253 s
at 128; for `cwv`, they are 0.524 s and 0.394 s. The exported results and masks
match the original references exactly. Thus the 128-worker first-command
regression above does not imply that the additional workers are useless:
pool reuse amortizes their startup. These targeted records are in
`tmp/new-machine-baseline/stage2-reuse/`; they are separate from the 105-run
fresh-pool sweep.

An independent build instruments the modified orchestration units and the
shared readers/reducers with AddressSanitizer and UndefinedBehaviorSanitizer.
The complete TimeWorkers test passes with no reported memory/UB error
(legacy leak reporting disabled). A dynamically linked PIE sanitizer build
had an intermittent failure during runtime initialization; the validation
uses a non-PIE executable with static ASan runtime, without changing production
build flags. Logs are in `tmp/acceleration-stages/stage2/asan-test-static.log`.

#### Stage 3: ordered input-time reads for mean and ave

Benchmark scope clarification: compound operations should use the requested
spatial-first `ave(aave(...),t=...)` and `mean(amean(...),t=...)`, also with
`maskout(qc,(qc>1e-5)-0.5)` inside the spatial average. The reverse-order
`aave(mean(...))` measurements below are an additional diagnostic, not a
substitute for those operations or evidence of their scaling. When valid
spatial coverage changes with time, the two orders can have different
denominators and results; the optimizer must preserve the expression's order.
An additional benchmark driver is
`tmp/new-machine-baseline/spatial_time_stage4.py`, with the matching
`ave/aave` and `mean/amean` pairs. These compound spatial-first reductions
have not yet completed a production scaling sweep. The user subsequently
deferred these infrequently used compound operations; they are not the next
optimization target. Keep the existing diagnostics and return priority to
common time-series profiles, conditional area means, ZINT and DISPLAY.

The same process pool now serves the bounded HDF5 reader used by time
reductions. Native file variables and supported two-variable sums can read
and decompress independent input times concurrently even when there is only
one output time, for example `mean(u,t=1,t=256)` or `ave(u+v,t=1,t=256)`.
This applies to both DEFINE and DISPLAY. Arbitrary expressions and resident
defined arrays keep their existing evaluator/calculation paths.

The parent still folds every input time in ascending order for each grid
point. Original weights, valid-sample denominators, paired-field masks,
min/max tie rules, increments and fractional `-b` boundaries are unchanged;
workers never return a mean of means. A large pool stays alive when a final
batch needs fewer workers; only that subset is active. This is bounded batch
scheduling, not a change to a hybrid worker/thread arrangement during the
calculation. Each active worker continues to use one thread.

Eligible HDF5 time batches use 8 MiB of parent workspace per requested CPU,
with a 64 MiB floor, 1 GiB ceiling, and an available-memory cap; paired fields
share the budget. Each batch still contains at most 256 times. This replaces
the fixed 64 MiB limit that otherwise leaves too few large planes in flight
for 32--128 workers. The worker-memory estimate and 128 MiB per-time result
limit remain in force. Tiny batches, unsupported grids and other formats
retain their existing readers. While a batch is being read, the progress bar
refreshes elapsed time/active workers; completed reduction steps advance only
after those values have actually been folded.

`TestTimeMeanWorkers.sh` checks 320 input times across two batches, including
the original time order, all eight reduction selectors, increments, weighted
boundaries, paired masks, surface broadcasts, missing template files,
reversed axes, packed native integers, descriptor symlinks, DEFINE and
fallback. Its 569899 finite reference samples match exactly, including masks
and non-finite classifications. PTY checks cover progress, iTerm2 clearing,
off mode, cancellation, killed-reader recovery and cleanup. Both the normal
and ASan/UBSan builds pass. The Stage 2 TimeWorkers test and all eleven
OpenMP/reduction/I/O/progress/graphics regression suites also pass.

The longer Stage 2 follow-up additionally compared 1024 times, T=1377:2400.
Every exported result and mask matched exactly. The first serial read of the
new time range was a cache-priming pass, so a separate warm serial pass is
used for comparison: 64.134 s for `ua2` and 66.450 s for CWV. Pool-reuse
medians at 64/128 workers are 1.555/0.967 s for `ua2` and 1.769/1.230 s for
CWV. The first 128-worker command takes 1.805 s and 2.148 s, respectively.
The longer runs amortize startup better than 256-time runs. The separate
figure, records and warm serial checks are in
`tmp/new-machine-baseline/stage2-reuse/pool-reuse.png` (also PDF).

The input-time sweep checks four real-data cases at all seven CPU budgets,
with one first command and three measured pool-reuse commands per process.
All 18418512 exported values and masks agree with the frozen Stage 2
executable; maximum absolute difference is `2.49e-14`, in the outer spatial
area reduction. Native time-mean/ave maps match exactly. Warm pool-reuse
medians in seconds (the one-CPU column uses the ordinary serial reader):

| Case | 1 CPU | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Regional `mean(u)` | 0.451 | 0.131 | 0.084 | 0.060 | 0.060 | 0.063 | 0.066 |
| Regional `ave(u)` | 0.460 | 0.134 | 0.076 | 0.058 | 0.062 | 0.060 | 0.065 |
| Global `mean(u)` | 2.139 | 0.956 | 0.792 | 0.596 | 0.540 | 0.614 | 0.796 |
| `aave(mean(u))`, 44 levels | 18.957 | 5.590 | 3.206 | 2.394 | 2.632 | 2.688 | 2.972 |

Here the time range is T=2145:2400. Regional maps cover lon=0:90,
lat=-10:10 at Z=22; the global map is 1536 x 384 at the same level. The
last case time-averages each cell before applying the spatial area mean at
all 44 levels; missing-data semantics prohibit substituting the average of
independent area means in general. First-command times include startup and
are plotted separately. Export is excluded from all evaluation timers.
Artifacts are `tmp/new-machine-baseline/stage3-production/scaling.png`,
`speedup.png`, PDFs and summary/records JSON.

This stage improves input-time reductions, but its 64/128-worker results
do not yet beat 16/32 consistently. Large maps transfer every input grid
back to the parent for the ordered fold. There is also excessive OpenMP
team overhead: the regional fold has only 34 tiles, while small 33968-cell
operations can still request 128 threads. A subsequent stage should cap
calculation teams by actual work and useful tiles, then profile bulk result
transfer before changing accumulation semantics. These limits are separate
from the strong output-time profile scaling in Stage 2.

#### Stage 4: standalone AMEAN workers and DISPLAY

Standalone `amean(...)` now shares the isolated output-time workers and
transient Z/T DISPLAY assembly with `aave`. Its latitude weights remain the
existing linear-coordinate weights; this does not replace AMEAN with an
area-weighted AAVE. Defined inputs and unsupported expressions retain their
ordinary evaluator. The deferred compound time/spatial reductions above
remain outside this stage.

The expanded TimeWorkers regression includes fractional bounds, conditional
masks, surface broadcasts, Z-only and T-only display, and identical AMEAN
DEFINE/DISPLAY PNGs. It passes regular/template/reversed HDF5, progress,
cancellation, killed-worker recovery and cleanup. Its 34528 finite samples
agree within the established tolerance (maximum absolute difference
`1.78e-11` in the single-time parallel spatial reduction). ASan/UBSan and
TimeMeanWorkers, OpenMP, ReductionIO, BinaryHalo, ZInt and AveSection pass.

The 44-level, 256-time RLL sweep compares 315392 DEFINE/DISPLAY values with
the frozen Stage 3 executable's serial AMEAN reference; all values and masks
match exactly. Warm-cache times in seconds:

| CPU budget | 1 | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| DEFINE, fresh pool, median of 3 | 16.279 | 4.095 | 2.166 | 1.248 | 0.918 | 0.868 | 1.245 |
| Direct DISPLAY, reused pool, median of 3 | 16.150 | 3.992 | 2.073 | 1.110 | 0.640 | 0.433 | 0.286 |

First-command startup is measured separately in the figure. DISPLAY timers
evaluate statistics; rendering/PNG correctness is verified separately.
Artifacts are `tmp/new-machine-baseline/stage4-amean-production/scaling.png`
and PDF, summary/records JSON and logs. The discarded work-aware-team and
passive-wait experiments are retained under the earlier `stage4-*` diagnostic
directories; those changes did not improve latency and were reverted before
this AMEAN stage.

#### Stage 5: reuse native inputs in threshold MASKOUT

The common `maskout(f,(f>cut)-0.5)` and `>=` forms now reuse the native
field already read for MASKOUT's first argument. The fused mask pass keeps
strict/inclusive comparison boundaries, source values and original masks;
the existing spatial reducer still computes the same weights and denominator.
GrADS comparisons return +/-1, so the 0.5 shift retains precisely the true
cells. Other mask syntax, different fields, defined inputs and dimension
qualifiers retain the ordinary evaluator. `GA_MASK_TRACE=1` reports reuse.

`TestMaskThreshold.sh` checks native/template/reversed binary, NetCDF3,
NetCDF4, HDF5 and BP5 data, exact and neighboring thresholds, all-masked
cases, invalid inputs, scalar grids, source values and fallback. Its 2639494
samples match the independent `f+0` evaluator exactly, including masks and
non-finite classifications; native and fallback PNGs are byte-identical.
An HDF5 call-count probe confirms 2 reads become 1. ASan/UBSan and the
TimeWorkers, TimeMeanWorkers, OpenMP, ReductionIO, BinaryHalo, ZInt and
AveSection regressions pass.

For the 44-level RLL `aave(maskout(qc,(qc>1e-5)-0.5),...)`, all 315392
compared DEFINE/DISPLAY values and masks match the original references
exactly. Fresh-pool DEFINE medians (three trials) and reused-pool direct
DISPLAY medians (three trials), over T=2145:2400:

| CPU budget | 1 | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| DEFINE before fusion (s) | 13.970 | 3.540 | 1.863 | 1.081 | 0.804 | 0.767 | 1.219 |
| DEFINE after fusion (s) | 10.324 | 2.659 | 1.410 | 0.870 | 0.679 | 0.724 | 1.193 |
| DISPLAY after fusion, reused pool (s) | 10.353 | 2.574 | 1.308 | 0.684 | 0.438 | 0.278 | 0.189 |

The serial improvement is approximately 35%; fresh high-worker-count runs
remain dominated by startup. Parent-only probe counters must not be treated
as total pool reads or CPU. The figure and full records are in
`tmp/new-machine-baseline/stage5-mask-production/scaling.png` and PDF,
summary/records JSON and logs. DISPLAY timers evaluate statistics; PNG
correctness is checked separately.

#### Stage 6: bulk HDF5 density profiles for ZINT

The native `zint(qv*rhobar)` path now reads an eligible HDF5 density profile
as one Z vector instead of 44 scalar evaluations. Coordinate agreement,
unpacking, masks and Z order retain their existing meaning. Other profile
backends and unsupported layouts keep the scalar path. The integration
arithmetic and chunk reader are unchanged.

ZInt regressions now include partially missing and wholly invalid density
profiles (NaN/Inf/undef), in native and evaluator products, with templates,
reverse axes and separate profile descriptors. ZInt, TimeWorkers, OpenMP,
ReductionIO and BinaryHalo pass; the ZInt suite also passes ASan/UBSan.
Across 21 real-data measurements at all seven CPU budgets, 182611968 values
and their masks match the original CWV references exactly. Serial HDF5 calls
over 256 times fall from 33792 to 22784; selected values remain 382626816.

Overall one-CPU latency is essentially unchanged: 16.539 s versus 16.567 s
before this stage. This is a reduction of small-call overhead, not evidence
of a material end-to-end speedup. Compressed chunk decoding remains the
larger single-time target. Records, summary JSON and the call-count/latency
figure are in `tmp/new-machine-baseline/stage6-density-production/`
(`density-reads.png` and PDF).

#### Stage 7: parallel compressed HDF5 chunks within one time

Native HDF5 ZINT maps can now decode independent compressed chunks with
OpenMP. This covers `zint(u)` and native field/profile products such as
`zint(qv*rhobar)`, for both DEFINE and direct DISPLAY. HDF5 metadata and raw
chunk reads remain on the calling thread; workers run private zlib decoding
and gather selected values. A separate column-owned fold preserves the
native reader's exact chunk and vertical addition order, masks, unpacking,
density factors and layer weights.

The path requires HDF5 2.0 or later, OpenMP, the native HDF5 connector,
native-endian float/double data, a 2-D X/Y result, and canonical Z/Y/X or
T/Z/Y/X chunks (one time per chunk). It accepts ordered shuffle/deflate/
Fletcher32 subsets with deflate present and honors each chunk's skipped-filter
mask. Checksums remain verified, including the legacy digest compatibility
of HDF5's [Fletcher32 filter](https://github.com/HDFGroup/hdf5/blob/develop/src/H5Zfletcher32.c).
Unsupported filters, integer/endian conversion, sparse unallocated chunks,
partial-chunk filter options, other layouts, and small or cache-fitting
selections retain the native HDF5 reader. `GA_H5_DECODE=off` explicitly
selects that reader. Isolated time workers still use one thread each, so
this stage does not multiply the total CPU budget across times.

Decode batches contain at most 128 chunks and use at most 256 MiB of raw,
scratch and selected-value buffers, including a zlib workspace reserve.
Chunk metadata has a separate 16 MiB limit. No global 3-D cube is allocated.
Allocation/filter metadata is checked before any accumulation; decoding or
checksum errors discard the incomplete result. Progress advances only after
a batch is folded, and cancellation preserves an existing DEFINE value.

`TestH5Decode.sh` compares 9022578 finite values exactly, with matching masks,
against the native reader. Fixtures cover float/double, four dimensions,
packing, reversed axes, partial chunks and Z bounds, cropped regions/halos,
all eight skipped-filter combinations, legacy checksums and fallback layouts.
Corrupt checksums and deflate streams fail safely, subsequent reads recover,
and PNGs are byte-identical. PTY checks exercise monotonic progress, iTerm2
clearing, off mode and cancellation after partial accumulation. The suite
also passes ASan/UBSan. All fourteen decode/ZINT/time-worker/mask/OpenMP/
reduction/I/O/section/progress/graphics regressions pass. Separate compile
checks disable OpenMP, HDF5 and the new API block; these check feature guards,
not compilation against an independently installed older HDF5 release.

The real-data sweep uses T=2400, a 386 x 88 regional map and a 1536 x 384
global map, with 44 levels. It contains 224 timed evaluations across seven
CPU budgets, DEFINE and DISPLAY, with a first command and three repeats
per process. All 43665440 compared output values and masks match the frozen
Stage 6 serial executable exactly. DISPLAY evaluation medians in milliseconds:

| Case | 1 CPU | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Regional `zint(u)` | 64.61 | 32.30 | 22.35 | 18.80 | 18.14 | 14.71 | 20.35 |
| Regional CWV | 71.24 | 31.10 | 20.66 | 16.53 | 16.91 | 13.99 | 9.92 |
| Global `zint(u)` | 446.03 | 163.68 | 163.45 | 120.24 | 104.32 | 95.17 | 106.80 |
| Global CWV | 438.13 | 157.70 | 157.57 | 96.20 | 99.93 | 89.14 | 101.41 |

These are warm OS-cache measurements, including reads and calculation,
excluding startup, export, ASCII verification and rendering. Rendering is
verified separately. DEFINE results and first-command overhead are plotted
alongside repeated DISPLAY times in
`tmp/new-machine-baseline/stage7-single-production/scaling.png` (also PDF),
with records, summary and metadata JSON. The region intersects 88 chunks;
the globe intersects 396. Serial raw reads, memory traffic, available chunks
and SMT limit scaling: 64 threads improve these global maps by about 4.7--4.9x,
but 128 do not consistently improve them. This host has 104 physical cores.
HDF5 probe counters count `H5Dread`, not `H5Dread_chunk2`; a zero read count
on the new path does not mean no I/O. Single-process CPU counters include
all OpenMP threads.

A final T=2145:2400 check of both integrals at 1, 16, 64 and 128 CPUs
compares another 69566464 values and masks exactly against the original
256-time references. This confirms coexistence with the one-thread time
workers; it is a one-trial correctness check, not a replacement scaling
sweep. Records are in `tmp/new-machine-baseline/stage7-times-check/`.

A separate configuration-only `OMP_WAIT_POLICY=PASSIVE` pilot checks
24951680 additional values exactly. For global CWV at 128 threads, repeated
DISPLAY CPU time falls from 4.84 to 1.59 CPU seconds while elapsed time is
approximately unchanged (101.4 vs 99.8 ms). This may reduce waiting cost,
but does not establish a universal latency improvement; no default changes.
To reproduce with the local development launcher, use
`./dev exec env OMP_WAIT_POLICY=PASSIVE ./opengrads`.
Records are in `tmp/new-machine-baseline/stage7-passive-pilot/`.

The next priorities are reducing serial chunk-read/fold overhead for a
single time, then profiling and reducing the large input-time grid transfer
to the parent. Any overlap/shared-memory implementation must retain bounded
memory, ordered accumulation, cancellation and crash recovery. Hybrid
workers/threads remain a future scheduling experiment; there is no mid-command
switch from 64 x 1 to 32 x 2. The uncommon compound spatial/time averages
remain deferred as requested.

#### Stage 8a: available-memory estimates with warm file caches

Linux worker and input-batch budgets now use `/proc/meminfo`'s MemAvailable
estimate, which includes reclaimable cache. Other hosts, or Linux without
that field, retain the free-page fallback. The quarter-available-memory
limit, 8 GiB worker estimate ceiling, 1 GiB input-batch ceiling and existing
per-result limits are unchanged.

During the new-expression regression, the host had about 1.8 TiB available
but only about 10 GiB completely free. The preceding free-page estimate
therefore reduced a requested 128-reader pool to roughly 75 workers, with
small command-to-command changes. This was an estimate affected by cached
files, not a numerical failure or evidence that 128 CPUs were unavailable.

TimeMeanWorkers now also injects 1 GiB and 128 MiB available-memory reports
through a test-only preload shim. Worker counts shrink under the first and
the serial reader handles the second; all values/masks remain unchanged.
The suite's 578083 finite samples match exactly, including the original
128/62-reader batch reuse, progress, cancellation and recovery checks.
TimeWorkers and OpenMP also pass. Artifacts are in
`tmp/acceleration-stages/stage8-memory/`.

#### Stage 8b: spatial reductions with a time-series result

The spatial-section planner now rejects remaining T/E axes before calling
spatial coordinate converters. Those axes use their existing position-at-a-time
evaluator instead. The earlier planner called a null `ab2gr[3]` when a
spatial reduction left a time series, causing a native DISPLAY crash even
with Z fixed. The frozen Stage 7 executable reproduces the defect; ASan
identified the null callback during the new-expression validation.

The intended vertical-sum usage fixes the external Z dimension:

```text
set z 1
set t 1 32
display 2*sum(f,z=1,z=4)
```

The function arguments select the four input levels; the command's varying
axis is T. AveSection adds all eight reduction selectors over X, Y and Z
with T varying, including arithmetic around the reduction. Its 39 cases
match the independent `field+0` evaluator exactly. OpenMP, ReductionIO and
BinaryHalo pass. Stage logs are in
`tmp/acceleration-stages/stage8-section/`.

#### Stage 8c: general output-time expressions for DEFINE and DISPLAY

Time-worker eligibility now examines the complete expression without
requiring an outer AVE/MEAN/AAVE/AMEAN/ZINT function. Native HDF5 fields,
arithmetic, parentheses, comparisons/logical operators and the supported
read-only built-ins can share the existing isolated pool. Examples include
`u+v`, `mag(u,v)`, `pow(u,2)`, `sin(u/10)+cos(v/10)` and
`maskout(qc,(qc>1e-5)-0.5)*1000`. Every worker evaluates the original complete
expression at its assigned time; there is no reassociation or partial-sum
merge. DEFINE retains the original T/Z/grid storage order.

Direct T and Z/T DISPLAY at fixed X/Y uses the same pool. Short generic
series, one-CPU/disabled execution and insufficient worker memory keep the
ordinary evaluator. New compound expressions that reduce a displayed axis,
or would bypass native fixed-Z/T restrictions of area reductions/ZINT,
retain the native path. Existing outer profile paths are preserved. X/Y
time animations are outside this stage. Defined inputs, file/dimension
qualifiers, plug-ins and unsupported functions retain their existing paths.

[TIME_WORKERS.md](TIME_WORKERS.md) documents usage, memory limits and all
66 registered built-in names: 24 are on the worker allowlist and 42 remain
on their existing paths pending additional validation. Trigonometry includes
SIN/COS/TAN/ASIN/ACOS/ATAN2; the unregistered ATAN allowlist entry was removed.
MASKOUT and CONST participate without changing missing-value rules. IF and
the remaining functions were audited but not enabled speculatively.

`TestTimeExpressions.sh` compares 349077 finite samples exactly, including
masks and invalid-value classifications. It covers regular/template/reversed
HDF5, packed values, cropped planes, surface/profile/scalar broadcasts,
arithmetic, masks, all-missing results, zero denominators, invalid powers,
out-of-domain inverse trigonometry, NaN/Inf, fallback and T/Z ordering.
DEFINE/direct DISPLAY PNGs also match the native serial PNGs byte-for-byte.
PTY checks verify monotonic progress, iTerm2 clearing, off mode, cancellation,
killed-worker recovery, cleanup and preservation of an old DEFINE value.
The suite passes ASan/UBSan, including the expression evaluator. Feature-guard
compile checks disable OpenMP and POSIX orchestration; these are not runtime
tests on other platforms. All fifteen expression/OpenMP/reduction/I/O/ZINT/
worker/mask/section/progress/Undo/DEFINE/saturation/canvas suites pass. The
optional external original-P3 reference remains unavailable, as in prior runs.

The RLL production sweep measures T=2145:2400 at all seven CPU budgets,
with one first command and three repeats per process: 168 timed evaluations.
Every DEFINE export and the separately printed direct DISPLAY grid agree
with frozen Stage 7 one-CPU references: 974324736 compared output positions,
identical masks, maximum absolute difference zero. Warm repeated-command
medians in seconds:

| Case | 1 CPU | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| DEFINE `(u+v)*0.5`, regional X/Y | 0.863 | 0.230 | 0.121 | 0.078 | 0.061 | 0.061 | 0.069 |
| DEFINE `mag(u,v)`, regional X/Y | 0.884 | 0.239 | 0.134 | 0.081 | 0.064 | 0.062 | 0.073 |
| DEFINE `pow(u,2)`, regional X/Y | 0.660 | 0.182 | 0.097 | 0.063 | 0.060 | 0.062 | 0.066 |
| DEFINE threshold QC mask, regional X/Y | 0.245 | 0.080 | 0.054 | 0.051 | 0.054 | 0.058 | 0.063 |
| DEFINE trigonometry, point Z/T | 14.450 | 3.681 | 1.889 | 1.024 | 0.571 | 0.389 | 0.230 |
| DISPLAY trigonometry, point Z/T | 14.475 | 3.632 | 1.851 | 0.996 | 0.558 | 0.389 | 0.259 |

The regional maps are 386 x 88 at Z=22. Each command returns 8695808 grid
positions (about 74.6 MiB of values/masks); only 9036 positions pass the QC
threshold in that mask case. The point profile fixes lon=45, lat=0 and
retains all 44 levels. Timers include reads/calculation, exclude application startup,
exports, ASCII verification and rendering; first-command worker startup is
included in its own timer and plotted separately. At 128 CPUs that first
command takes about 1.1--1.2 s in these cases. Repeated commands reuse the
same worker PIDs. All requested worker counts, including 128, were observed.

Results support 64/128-worker benefit for the read/decode-heavy point profile
(DISPLAY about 37x/56x faster than one CPU), while full-grid transfer and
storage leave the regional maps near a 16/32-worker plateau. A short fresh
128-worker map can be slower than serial; reuse and workload size matter.
No automatic mid-command hybrid worker/thread switching was added. The
probe's CPU/RSS/HDF counters cover only the parent, not aggregate pool cost.

Records, metadata, summary JSON and figures are in
`tmp/new-machine-baseline/stage8-expressions-production/`:
`scaling.png`/PDF show first versus repeated latency, and `speedup.png`/PDF
show repeated speedup. Drivers are `expressions_stage8.py` and
`plot_expressions_stage8.py` in the parent directory. Functional, sanitizer,
compatibility and build logs are under
`tmp/acceleration-stages/stage8-expressions/`.

#### Stage 9: QVSAT and ESAT output-time workers

ESAT and QVSAT now participate in the shared output-time expression evaluator.
This enables concurrent reads, decoding and saturation calculations for native
HDF5 inputs, both directly and inside supported outer AMEAN/AAVE profiles.
The existing saturation kernels, formulas, operation order, pressure guards
and default/explicit phase behavior are unchanged. Workers still use one
calculation thread each; the 128-worker and memory limits remain unchanged.

The requested expression is evaluated without algebraic changes:

```text
set x 1
set y 1
set z 1 44
set t 1 256
define c=qvsat(th*pibar,100000*pow(pibar,1004/287))
define ev=esat(th*pibar)
define cm=amean(qvsat(th*pibar,100000*pow(pibar,1004/287)),lon=0,lon=90,lat=-10,lat=10)
define em=amean(esat(th*pibar),lon=0,lon=90,lat=-10,lat=10)
```

The AMEAN cases compute saturation at each cell before averaging, rather than
moving the nonlinear function outside the mean. Default-file native PIBAR
profiles retain their scalar/axis broadcasts and template-time reads. Defined
temperature/pressure inputs and unsupported expressions retain their original
evaluation paths. Single-time kernels keep their existing OpenMP behavior.

TimeExpressions adds cold/warm branches, omitted/explicit auto, numeric and
grid phases, zero/invalid temperatures and pressures, profile/surface broadcasts,
the theta/Exner composition, masks and saturation inside AMEAN. All 610901
finite reference samples match exactly, with identical masks and non-finite
classifications. Saturation DEFINE/direct DISPLAY PNGs match serial PNGs
byte-for-byte. Progress, off/iTerm2 mode, Ctrl-C, killed-worker recovery,
cleanup and preservation of an old DEFINE also pass with saturation included
in the active expression. The expanded suite passes ASan/UBSan; all fifteen
expression/saturation/worker/reduction/I/O/progress/graphics regressions pass.
OpenMP/POSIX-disabled compile checks pass. The optional original-P3 external
reference remains unavailable; the ordinary independent saturation reference
and all earlier saturation branches/backends pass.

Usage is documented in [SATURATION.md](SATURATION.md). The function audit in
[TIME_WORKERS.md](TIME_WORKERS.md) now lists 26 worker-eligible built-ins and
40 functions retained on their existing paths. Functional/build/sanitizer
logs are under `tmp/acceleration-stages/stage9-saturation/`.

The production sweep uses the requested X=1, Y=1, Z=1:44, T=1:256 for
the point profiles. AMEAN retains the same output Z/T environment and reads
lon=0:90, lat=-10:10 (386 x 88 input cells per level). Each case measures a
first DEFINE and three repeats in one process at all seven CPU budgets:
112 timed evaluations. Each DEFINE export, plus direct DISPLAY checks at
1/16/64/128 CPUs, matches the frozen `115e9ef` one-CPU reference exactly:
1441792 output positions, matching masks and maximum absolute difference zero.
All four reference profiles contain 11264 finite values.

Warm repeated-command medians in seconds:

| Case | 1 CPU | 4 | 8 | 16 | 32 | 64 | 128 |
|---|---:|---:|---:|---:|---:|---:|---:|
| QVSAT point profile | 8.819 | 2.243 | 1.111 | 0.552 | 0.294 | 0.196 | 0.126 |
| ESAT point profile | 8.316 | 1.997 | 1.052 | 0.525 | 0.284 | 0.192 | 0.137 |
| AMEAN(QVSAT), regional profile | 33.835 | 8.544 | 4.408 | 2.420 | 1.347 | 0.918 | 0.632 |
| AMEAN(ESAT), regional profile | 20.661 | 5.033 | 2.599 | 1.442 | 0.837 | 0.558 | 0.380 |

These include reads and calculation with warm OS caches, excluding application
startup, exports and the separately verified DISPLAY. Per-process worker
startup remains inside the first-command timer. Repeated 128-worker commands
are about 54--70x faster than one CPU in this sweep; the first 128-worker
command takes about 1.26--1.53 s and is slower than the first 64-worker
command here. Pool reuse and workload size remain important. Actual active
worker counts and reuse of the same PIDs are verified, including 128 workers.

Single-CPU probe data illustrates why whole-expression workers help: the last
QVSAT point evaluation spends 7.86 of 8.17 s in H5Dread, whereas the regional
AMEAN(QVSAT) spends 13.64 of 33.53 s there. The remaining time includes
calculation, averaging, parsing/allocation and other I/O metadata work.
These counters are meaningful for the serial process; the parallel probe
still covers only the parent and must not be interpreted as aggregate pool
I/O, CPU or memory.

Records, metadata and summary JSON are in
`tmp/new-machine-baseline/stage9-saturation-production/`, alongside
`scaling.png`/PDF (first versus repeated latency) and `speedup.png`/PDF.
The driver is `tmp/new-machine-baseline/saturation_stage9.py`. This is a
separate T=1:256 saturation sweep, rather than a rerun of the earlier
T=2145:2400 arithmetic workload.

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
