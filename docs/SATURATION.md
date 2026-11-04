# Saturation vapor pressure and mixing ratio

The built-in functions `es(T[,phase=auto])` and `qvs(T,p[,phase=auto])` use the saturation
formulation of VVM P3's `polysvp1` and `qv_sat`. They work in `display` (`d`),
`define`, and other expressions, including `zslice` and `zint`.

| Argument/result | Meaning and units |
| --- | --- |
| `T` | Actual air temperature in K |
| `p` | Air pressure in Pa |
| `phase` | Optional `auto`, number, logical expression, or compatible grid; default `auto` |
| `es` result | Saturation vapor pressure in Pa |
| `qvs` result | Saturation mixing ratio in kg/kg, relative to dry-air mass |

For already prepared temperature and pressure fields `tk` and `p_pa`:

```text
d es(tk)
d es(tk,auto)
d es(tk,0)
d qvs(tk,p_pa)
d qvs(tk,p_pa,auto)
define qs = qvs(tk,p_pa)
```

The functions do not convert potential temperature, Exner pressure, hPa, or
Celsius. Supply actual temperature and pressure explicitly. For VVM, actual
temperature can be prepared as `th*pibar` when those fields have compatible
grids. Pressure conversion from Exner must use the simulation's own reference
pressure and exponent; `qvs` accepts pressure itself. Density and the actual
water-vapor mixing ratio are not required inputs.

## Phase and formulas

Omitting phase, or passing the literal `auto`, selects **ice strictly below
273.15 K and liquid at 273.15 K and above**, following P3's ice-selection
branch. The numeric modes retain their existing P3 behavior:

| Phase | T < 273.15 K | T >= 273.15 K |
| --- | --- | --- |
| Omitted / `auto` | Ice | Liquid |
| `0` | Liquid | Liquid |
| `1` | Ice | Liquid |

Thus `auto` and `1` give the same result; neither forces the ice polynomial
above freezing. Explicit `0` permits supercooled liquid saturation. In
particular, exactly 273.15 K uses the liquid coefficients in every mode.

```text
d qvs(tk,p_pa)
d qvs(tk,p_pa,auto)
d qvs(tk,p_pa,0)
```

The `[phase=auto]` notation documents an optional argument and its default;
do not type the brackets or `phase=` in the command. Use the positional token
`auto`, or omit that argument entirely.

Existing logical expressions and compatible phase grids remain supported.
A positive value selects the P3 ice branch; zero or a negative value selects
liquid. GrADS comparisons return 1 for true and -1 for false. A missing phase
propagates as missing, including at warm points. `auto` is a reserved literal
in the phase position and is evaluated from each temperature point.

The Flatau et al. (1992, table 4) polynomials are evaluated in double precision.
The original Goff-Gratch branches are retained for liquid below 202 K and ice
below 195.8 K. The mixing ratio is computed exactly with P3's pressure guard:

```text
qvs = 0.622 * es / max(0.001, p - es)
```

The result is a mixing ratio, not specific humidity. The denominator floor
is retained for positive pressures at or below `es`, and can produce very large
values there. Undefined/nonfinite inputs, T <= 0 K, p <= 0 Pa, negative or
nonfinite computed vapor pressure, and nonfinite results produce undefined
output. Finite phase values are interpreted by their sign, so a fractional
positive value also selects ice. No data-derived unit guessing or extra
saturation-law temperature clipping is applied.

## Dimensions and missing data

`es` preserves the temperature grid unless a varying phase expands a scalar
or lower-dimensional input. `qvs` similarly expands its operands to the
compatible grid with more varying axes. The rules follow ordinary GrADS grid
arithmetic:

- Numbers broadcast across a grid.
- Shared axes must have matching coordinates and sizes. Equal array sizes alone
  do not establish compatibility.
- A one-axis result can expand along a missing axis when its axis belongs to
  the larger two-axis grid. For example, a Z profile can expand along X in an
  X-Z section. Both X and Y expansions are supported.
- Two one-axis results on different axes are not an implicit outer product.
- GrADS evaluates a selected plane at a time. Defining a volume loops over the
  other dimension, so a compatible `p(z)` profile can be used with `T(x,y,z)`.
  Normal time/ensemble display loops and `tloop`/`eloop` can be used too.
- Missing temperature, pressure, or phase propagates point by point; values are
  not filled or reweighted. Inputs must be grid expressions or numbers.

The function checks grids without interpolating or resampling them. A file
whose descriptor declares X=1, Y=1 is a one-cell horizontal domain. It is not
automatically extended over a larger domain. To use a native vertical profile
with a full field, expose it on a compatible descriptor with no varying native
horizontal axes. For Taiwan VVM's `(z,1,1)` profile, a copy of the main descriptor
can include:

```text
grads_profiles/pibar=>pibar 69 z,0,0 Reference Exner profile
```

Adjust that copy's `VARS` count. This permits `th*pibar` in ordinary expressions.
The fixed native indices are zero-based. The `sea_grass_mountain` HDF5 descriptor
already declares `pibar` as `z`, so it expands correctly along horizontal rows.

## Verification

```bash
OPENGRADS_BUILD_ROOT=/path/to/build \
OPENGRADS_ADIOS2_ROOT=/path/to/adios2 \
  ./pytests/TestSaturation.sh
```

The test creates analytic float/double fixtures for binary, compressed NetCDF,
compressed HDF5 (including time templates), BP5, and compressed BP5, when those
backends are available. It checks omitted/explicit `auto`, phase comparisons and missing phase fields,
scalar and X/Y/Z expansion, reversed axes, two times, nonuniform heights,
volume definitions, incompatible coordinates, invalid arguments, pressure
floor behavior, and display/define PNG equality. The OpenMP regression also
compares these functions byte for byte with one and four threads on 256x256
fields.

When the original P3 source and GNU Fortran are available, the test extracts
`polysvp1` and `qv_sat` into a temporary reference module and compiles both
single- and double-precision versions. Set `OPENGRADS_P3_SOURCE` to the source
path and `FC` to the compiler if needed. This reference is used only in testing;
there is no new Fortran runtime dependency in GrADS. The ordinary regression
also uses an independently evaluated power-series reference.

Double-precision results agree with the original functions compiled in double
precision. They need not be bit-identical to single-precision Fortran: the cold
polynomial has cancellation, and the sampled single-precision reference showed
about a 0.34% relative difference near 200 K. Both P3 branch thresholds and the
freezing transition are covered by tests.

Real-data validation used Taiwan VVM compressed BP5 at T=73 and
`sea_grass_mountain` compressed HDF5 at T=6. For each dataset, five vertical
levels with 4096 points each were compared against the original double-precision
Fortran, including liquid, ice, conditional phase, and defined results. Native
ADIOS2/HDF5 reads supplied the reference data independently of GrADS. Temperature
was converted with `th*pibar`; pressure used the Exner definition and constants
confirmed in each run's saved source/configuration. All 172032 compared values
per dataset agreed within the test tolerance, with zero observed numerical
difference in these cases.
