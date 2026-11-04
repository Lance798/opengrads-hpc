# Height-coordinate column integrals

`zint(expression[,bottom[,top]])` integrates a cellwise-constant expression
along the file's vertical coordinate and returns a horizontal field. It works
in `display` (`d`), `define`, and other expressions.

```text
d zint(qv*rhobar)
define cwv = zint(qv*rhobar)
d zint(qv*rhobar,0,5000)
d zint(qv*rhobar,auto,5000)
d zint(qv*rhobar,bottom_map,top_map)
```

For column water vapor (CWV), supply water vapor and a compatible density in
the expression. With `qv` in kg/kg, `rhobar` in kg/m3, and height in metres,
`zint(qv*rhobar)` has units kg/m2, numerically equal to precipitable water in
mm. The function applies no density, gravity, pressure conversion, or unit
conversion itself. The user is responsible for the definition and units of
the supplied quantities; `rhobar` yields a reference-density diagnostic.

## Bounds and thickness

- Omitted `bottom`, or `auto`, means **0 in the height coordinate**.
- Omitted `top`, or `auto`, means the inferred upper edge of the highest cell.
- Each bound can be a number or a matching horizontal grid expression. Bounds
  are height values, not Z indices or heights relative to local terrain.
- Layers come from the source file's Z axis, independently of `set z`.
  Unmodified file-qualified variables use their own file's axis. Compound
  expressions and defined cubes use the default file's axis. Recognized
  field/profile products require coincident vertical axes.
- Inner edges are the arithmetic midpoints between adjacent layer centres.
  Each outer edge extends half the nearest centre spacing beyond its centre.
  At least two finite, strictly monotonic centres are required. Both ascending
  and descending axes and the `zrev`/`yrev` options are supported.
- The effective thickness is the intersection of each cell with the requested
  interval. Values are constant within each cell; no linear interpolation is
  applied. Data are never extrapolated outside the inferred outer edges.
- Bounds are evaluated once at the first source Z level. Supply horizontal
  fields or scalars; a 3-D field is not a varying vertical boundary.

For centres at 50, 150, 400, and 1000 m, the inferred edges are 0, 100, 275,
700, and 1300 m. A constant expression of 1 integrates to 1300 with defaults,
and to 750 between 125 and 875 m.

For centres `z[k]` and inferred edges `edge[k]`, the calculation is:

```text
thickness[k,x,y] = max(0, min(upper_edge[k],top[x,y])
                        - max(lower_edge[k],bottom[x,y]))
result[x,y] = sum(expression[k,x,y] * thickness[k,x,y])
```

Midpoint-derived thicknesses need not equal a model's native thicknesses on a
stretched grid. Supplying native layer interfaces or thickness profiles is
not part of this first implementation.

## Missing values and dimensions

The reduction follows `nansum`-style missing-data rules: undefined, NaN, and
infinite input values contribute nothing. Their thicknesses are not assigned
to other cells, and no normalization or gap filling is done. An entirely
missing column, a zero-width interval, or an interval outside all available
cells returns **0**. Consequently, a result can describe a partial column.

A missing/nonfinite bound or `bottom > top` returns undefined at that point.
Horizontal bound grids must match the output grid; scalars broadcast. X/Y
maps, horizontal lines, and single points work. Z is removed from the result;
T and E are fixed per evaluation, with normal GrADS display loops, `tloop`,
and `eloop` available for other times and members.

Terrain is not inferred automatically. The default 0 m is the coordinate's
zero, and can lie below mountainous terrain. Supply a suitable bottom field
when needed. `zslice(lev,topo)` returns the first atmospheric cell's **centre**,
not its lower face. Staggering, terrain masking, and physical-variable
conversions remain the user's responsibility.

## Taiwan VVM example

The main and profile descriptors can be opened together:

```text
open /path/to/taiwanvvm_1024_20120819/vvm.ctl
open /path/to/taiwanvvm_1024_20120819/vvm_profiles.ctl
set dfile 1
set t 73
set gxout shaded
d zint(qv.1*rhobar.2)
define cwv = zint(qv.1*rhobar.2,0,5000)
```

A simple product of a file field and a vertical profile supports a profile in
its own 1x1 descriptor. Its Z coordinates must coincide with the field's; the
profile is sampled once per level and broadcast horizontally. This applies
to either multiplication order. General compound expressions continue to use
ordinary GrADS grid-compatibility rules. For those, expose the profile in a
matching descriptor with no varying native X/Y dimensions, for example:

```text
grads_profiles/rhobar=>rhobar 69 z,0,0 Reference density
```

This line describes Taiwan VVM's native `(z,1,1)` profile, with fixed zero-based
native indices along its two singleton dimensions. It can be added to a copy
of the main descriptor, with its `VARS` count adjusted, to allow ordinary
expressions such as `zint((qv*rhobar)+0)`.

The `sea_grass_mountain` HDF5 descriptor already exposes `rhobar` with `z`
dimensions, so `zint(qv*rhobar)` works directly.

## Readers, performance, and validation

Unmodified BP5 variables, including compressed variables, are reduced one
intersecting writer-block selection at a time. HDF5 variables are reduced by
physical chunk, or bounded virtual tiles for contiguous datasets. Each
intersecting chunk is requested once. This includes a simple product with a
vertical profile, which uses a small per-level factor buffer. Neither reader
first assembles the full global 3-D field. Temporary space includes the
horizontal result/bounds, the vertical coordinates/factor, and one selected
block or chunk.

Other expressions, defined cubes, and readers such as NetCDF and ordinary
binary use the ordinary evaluator once per intersecting model level, keeping
only the current horizontal plane. Their reduction uses the same overlap and
missing-data rules. Compression layout and the existing file caches determine
the speed difference. `GA_ZINT_TRACE=1` prints the chosen path and native read
counts; it is off by default.

```bash
OPENGRADS_BUILD_ROOT=/path/to/build \
OPENGRADS_ADIOS2_ROOT=/path/to/adios2 \
  ./pytests/TestZInt.sh
```

The regression uses analytic nonuniform layers, float/double inputs, compressed
BP5 and HDF5, two times, templates, profiles in shared/separate descriptors,
scalar/2-D bounds, partial and empty intervals, missing columns, reversed and
descending axes, defined cubes, file qualifiers, and display/define PNG equality.
NetCDF and binary paths are also checked. Fixture dependencies can be selected
with `OPENGRADS_TEST_HDF5_ROOT` and `OPENGRADS_TEST_NETCDF_ROOT`.

Real-data validation independently read native `qv`, `rhobar`, and coordinates
using ADIOS2/HDF5 and computed the layer sums outside GrADS. Both the compressed
Taiwan BP5 output at T=73 and compressed `sea_grass_mountain` HDF5 output at T=6
matched all 4096 tested column values and partial-height integrals, within
1e-10 kg/m2. HDF5 time-file switching and the original Taiwan profile descriptor
were checked too. This validates the agreed midpoint diagnostic, without
asserting equivalence to the model's native geometric thicknesses.
