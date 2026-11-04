# Vertical index slices

`zslice(expression,index_map)` takes one Z index per horizontal grid point,
returning a horizontal field. It works directly in `display` (`d`), in
`define`, and inside other expressions. For example, when `topo` contains
the GrADS index of the first atmospheric cell:

```text
open /path/to/vvm.ctl
set t 73
set z 1
set gxout shaded
d zslice(th,topo)

clear canvas
d zslice(lev,topo)

define usfc = zslice(u,topo)
define vsfc = zslice(v,topo)
clear canvas
set gxout vector
d usfc;vsfc
```

For a vector plot without defining intermediate variables:

```text
d zslice(u,topo);zslice(v,topo)
```

## Index and coordinate rules

- `index_map` is a horizontal expression or a constant, such as
  `zslice(th,16)`. Indices are integers in GrADS's one-based Z numbering,
  starting at 1. They are not heights in metres or pressure levels.
- Each point selects exactly that index. Missing indices, NaN/infinity,
  non-integers, zero/negative indices, out-of-range indices, and missing values
  at the selected level produce undefined output. The function does not round
  indices, search for a different valid level, or interpolate.
- The horizontal grids of the expression and index map must match; a constant
  index applies everywhere. X/Y maps, horizontal lines, and single points
  are supported. A varying Z dimension is removed from the result.
- T and E are fixed for each evaluation. Use GrADS's normal display loops,
  `tloop`, or `eloop` to evaluate other times or ensemble members.
- File-qualified fields work, for example `zslice(u.1,topo.2)`. Indices refer
  to the source field's file and vertical ordering. Compound expressions use
  the default file's Z axis, as other GrADS vertical operations do.
- `lev` is GrADS's coordinate variable: `zslice(lev,topo)` returns the selected
  level's world coordinate. With VVM's height axis this is the height of the
  atmospheric cell centre, not the terrain interface. A pressure-coordinate
  file returns pressure instead. Nonuniform and reversed axes are supported.
- Input/output variable units are retained in the numerical values. No
  vertical or horizontal interpolation, stagger correction, 10 m wind
  diagnostic, or density weighting is applied.

For the Taiwan VVMex output checked here, the point X=512, Y=512 has
`topo=16`, and `zslice(lev,topo)` returns 1550 m, matching `lev(z=16)`.

## Reading and performance

Unmodified file variables take the optimized reader path. A coordinate request
first supplies the horizontal metadata without reading a data plane.

| Source | Indexed read behavior |
| --- | --- |
| BP5, including Blosc2-compressed BP5 | Inspect writer-block metadata, skip blocks containing no requested cells, read one bounding selection of the requested Z range per intersecting block, then gather the selected values. |
| HDF5, including compressed and templated files | Group the requested elements by physical HDF5 chunk, then issue one sparse element read per touched chunk. File changes between time steps, reversed axes, packing, and missing values are handled. |
| NetCDF / NetCDF-4 and ordinary GrADS binary `.dat`, including templates | Merge adjacent X points requesting the same Z into one native row segment and read only those segments. Existing byte-order, axis-order, missing-value, and unpacking handling is reused. |
| Defined 3-D variables, compound expressions, wrapped/out-of-bounds requests, preprojected data, and other readers | Evaluate each distinct requested level once through the ordinary GrADS evaluator, then gather the requested points. |
| `lev` | Convert indices through the file's Z-coordinate definition; no atmospheric field is read. |

BP5 compression operates on writer blocks: reading a small part of a compressed
block can still require decompressing the entire block. Zslice groups requests
so it avoids asking for that same block separately for every level or surface
point. Temporary native storage holds one selected block region at a time;
the whole global 3-D field is not assembled first. Throughput still depends on
block/chunk layout, storage, and the shape of the index map. Row-segment reads
are not necessarily faster than a small number of dense reads for every
possible chunk layout.

HDF5 also compresses by chunk. Grouping the sparse requests prevents the same
chunk from being repeatedly requested as the index changes across rows, even
when the ordinary chunk cache would be too small to retain all surface levels.
The `sea_grass_mountain` output was checked with shuffle, DEFLATE level 1,
Fletcher32, and chunks of `1 x 64 x 512`; indexed values matched the evaluator,
including switching between its templated time files and sampling `rhobar`.

Pointwise operations can keep their input fields on the direct reader path:

```text
d mag(zslice(u,topo),zslice(v,topo))
```

`zslice(mag(u,v),topo)` also works, through the expression fallback. Spatial
operations such as derivatives or smoothing should remain inside `zslice`
when they are meant to act on each original model level.

Set `GA_ZSLICE_TRACE=1` when diagnosing I/O. It reports the number of BP5 blocks
and native values read, HDF5 chunks, or indexed row segments. This is disabled
by default.

## Verification

```bash
OPENGRADS_BUILD_ROOT=/path/to/build \
OPENGRADS_ADIOS2_ROOT=/path/to/adios2 \
  ./pytests/TestZSlice.sh
```

The regression creates analytic fixtures for ordinary binary, NetCDF,
compressed NetCDF-4, HDF5, BP5, and compressed BP5 where the build supports
them. It checks float/double values, nonuniform heights, time steps, profiles,
reversed Y/Z, missing data and invalid indices, time-file templates,
qualified names, expressions,
defined cubes, and direct display versus defined results. With Cairo, rendered
PNGs from direct and defined slices must be identical.

Fixture generation requires a C compiler, Python 3, and the development tools
for the tested formats. For dependencies in private prefixes, set
`OPENGRADS_TEST_NETCDF_ROOT` and `OPENGRADS_TEST_HDF5_ROOT` as needed. The
NetCDF/HDF5 GrADS backends must be enabled at build time to read those formats.
