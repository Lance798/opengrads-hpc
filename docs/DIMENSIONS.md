# Dimension endpoints, sizes and animation steps

## Indexed dimensions

Each indexed SET dimension accepts numeric endpoints, `last`, and its own
size name: X/Y/Z/T/E use `nx/ny/nz/nt/ne`, respectively.

```text
set x 1 nx
set y 1 last
set z nz
set t 1 nt
set e 1 ne
set x 2 nx-1
set z (nz+1)/2
```

`last` and the size name refer to the full dimension of the default file,
not the current selection or the number of levels of a particular variable.
They are resolved when SET executes. Changing `set dfile` does not rewrite
an earlier selection. For a template, the descriptor's declared length is
used; missing data files are not searched to find an endpoint.

Integer expressions support `+`, `-`, `*`, `/`, unary signs and parentheses,
with the usual precedence. Each endpoint must be one token without spaces.
All intermediate results must fit a signed 32-bit integer, and every division
must be exact: `nx/2` fails for an odd NX. Division by zero, overflow, unknown
names, mismatched axis names (`set x 1 ny`) and extra arguments fail without
changing the dimension environment. Original numeric endpoints, including
fractional indices, scientific notation, and out-of-domain selections, keep
their existing coordinate interpretation. These names are local to SET;
data variables and DEFINE names such as `nx` remain available.

## Longitude, latitude and vertical coordinates

```text
set lon first last
set lat first last
set lev last
set lev first last
```

Here `first` and `last` mean the physical coordinate at grid index 1 and
the final index of the default file. The existing grid-to-coordinate mapping
is used, including irregular levels and decreasing coordinates. `last` does
not mean the largest coordinate, and longitude is not extended by one grid
spacing to close a cyclic domain. For levels 1000,850,500,200, `set lev last`
selects 200, while `set z nz-1` selects 500.

These commands keep accepting ordinary numeric coordinates. They do not
accept `nx/ny/nz` or keyword arithmetic such as `last-1`; use the indexed SET
command to move by layers or grid cells. Physical endpoint keywords require
a gridded file; ordinary numeric station-data bounds retain their behavior.
As with numeric bounds, a descending X/Y selection may be rejected by DISPLAY.

## Full file dimension sizes

The data expression function `dimsize(x|y|z|t|e <,file#>)` returns a valid
scalar containing the full descriptor dimension length. Omitting `file#`
uses the default file. An explicit file number is a positive decimal integer
with the same ordinal as `q files` / `set dfile`; it is not an expression.

```text
d dimsize(x,1)*dimsize(y,1)
d qv/dimsize(z)
define npoints=dimsize(x,1)*dimsize(y,1)
```

For `.gs` scripts, the equivalent built-in returns a numeric string that can
be used in script arithmetic. Quote the axis to avoid resolving a script
variable with the same name:

```text
nx = dimsize('x',1)
ny = dimsize('y',1)
npoints = nx * ny
say npoints
'set x 1 'nx
```

The lookup reads metadata only, independent of the selected range, without
changing the default file, dimension environment, or script `rc`/`result`.
It works with open gridded descriptors across binary, GRIB, NetCDF, HDF5,
and BP5 readers; single-element axes return 1. Station files have T/E sizes
but no X/Y/Z grid size. Invalid axes, absent files and invalid file numbers
raise errors rather than returning an undefined value. No file is opened
automatically. Expressions containing `dimsize` use the existing serial
evaluator fallback rather than the time-worker/spatial expression planners.

## Animation step

```text
set x 1 nx
set y 1 ny
set z 1
set t 1 last
set loopdim t
set loopincr 6
d qv
```

`set loopincr N` takes a positive decimal integer, default 1. The built-in
DISPLAY animation advances N grid indices on `set loopdim` (T by default),
including when animation is triggered automatically by three varying axes.
The range 1..20 with step 6 draws 1,7,13,19. The upper endpoint is not appended
unless it falls on the step; labels use the last sampled frame. A step larger
than the range draws only its first frame.

`c` / `clear` resets the step to 1; `cc`, `x`, `clear canvas`, `clear norset`,
and `swap` retain it. `reset` and `reinit` restore 1. The step persists across
DISPLAY and dimension changes. Invalid SET values leave it unchanged.

The step affects only which animation frames are evaluated and drawn. It
does not change file dimensions, DEFINE sampling, reductions such as AVE,
non-animation time-series data, frame playback delay, or a `.gs` script's own
iteration counter. To skip frames in a script-controlled loop, advance that
counter explicitly.
