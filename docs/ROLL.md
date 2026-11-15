# Circular grid shifts

```text
roll(expression, integer_shift, x|y)
```

ROLL copies the evaluated expression's values and missing mask along X or Y,
wrapping at the ends. Coordinates, shape and other grid metadata stay in place.
It performs no interpolation or arithmetic on the copied values. Finite
double values, signed zero and valid nonfinite values retain their stored
representation; missing cells move with their mask.

```text
define shifted=roll(qv,1,x)
define nextrow=roll(qv,-1,y)
define both=roll(roll(qv,2,x),-3,y)
```

Positive shifts move values toward larger grid indices. For `[a,b,c,d]`, a
shift of `1` returns `[d,a,b,c]`; `-1` returns `[b,c,d,a]`. The destination at
index i reads the source at `(i-shift) mod n`. X/Y are GrADS dimension names;
for a NumPy array laid out as `[y,x]`, they correspond to axes 1/0.

The shift must be a signed or unsigned decimal integer literal. Fractional
numbers, scientific notation and shift expressions are rejected. Arbitrarily
large decimal integers are reduced modulo the selected axis length while
parsing, without machine-integer overflow. Zero shifts and multiples of the
axis length leave the result unchanged. A fixed/absent axis is treated as a
singleton and also leaves the result unchanged, including scalar expressions.

The circular domain is the **evaluated grid**, for example:

```text
set x 1 100
set y 1 100
set z 1
set t 1
define a=roll(qv,-1,x)
```

Here wrapping occurs within the selected 100 X cells. It does not read a
neighbor outside that selection. Functions surrounding ROLL may establish
their own evaluation extent, as spatial reductions do. Z/T/E shifts and
implicit flattened-array shifts are not part of this interface; specify X or
Y explicitly. Two-axis shifts use nested calls.

ROLL accepts grid expressions and numbers. Station expressions are rejected.
The source expression is evaluated normally, and its underlying file or
defined array is preserved. A nontrivial shift allocates one values/mask grid;
large grids use OpenMP row copies. Copying is bounded by memory bandwidth,
so higher core counts do not necessarily improve performance.

Eligible DEFINE and input-time reductions use the shared time workers,
including file-qualified operands and bounded static DEFINE snapshots:

```text
set threads 8
set x 1 512
set y 1 512
set z 1
set t 1 32
define neighbor=roll(qv,-1,x)
set t 1
define total=sum(roll(qv,-1,x)*qv,t=1,t=32)
```

Each worker owns its complete selected spatial grid and rolls within that
grid. Normal time-worker work/memory thresholds and DISPLAY restrictions
apply. Use `GA_TIME_TRACE=1` to verify dispatch and `GA_TIME_WORKERS=off` for
serial comparison. ROLL is also used by the periodic paths of
[`vvmtopo`](VVMTOPO.md), which operates at one fixed time.

`pytests/TestRoll.sh` checks analytic permutations, masks, coordinates,
selected domains, singleton axes, nested calls, very large shifts, invalid
arguments and bitwise copies at 1/4 threads. Set `GA_TEST_NUMPY_PYTHON` to a
Python executable with NumPy installed for an additional independent NumPy
oracle. `pytests/TestTimeRoll.sh` checks worker values and recovery with
HDF5, classic/compressed NetCDF and BP5 fixtures.
