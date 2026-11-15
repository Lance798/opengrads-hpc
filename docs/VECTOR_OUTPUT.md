# Vector graphics output

`gxprint` exports PDF, SVG, PS and EPS through the Cairo printing backend.
Each format follows the real GrADS canvas: a landscape canvas makes a
landscape page and a portrait canvas makes a portrait page. `set vpage` and
`set parea` continue to position plots within that page. PDF, PS and EPS
retain their page margin, fitting the picture uniformly and centering it
without stretching its X and Y scales independently. PS/EPS BoundingBox
includes the full canvas even when the page background is transparent.

```text
gxprint plot.pdf
gxprint plot.svg white
gxprint plot.eps black
gxprint plot.svg transparent
```

The default is the current canvas background. Explicit `white` or `black`
also overrides a custom background color. The background covers the full
page, including its margins, and remains separate from data with the same
color. The `transparent` option omits only the page background; black and
white data, text and explicitly drawn rectangles remain visible. It is
available with Cairo for vector output and PNG.

`-t color#` keeps its existing meaning: make that color transparent wherever
it is drawn. It can remove data or text as well as the background. With a
white output override, the logical foreground/background colors 0 and 1
are swapped for contrast, so use `transparent` for a background-only change.
Temporary export settings are reset after both successful and failed output.

Opaque `grfill` output combines adjacent same-color rectangles and writes
disconnected rectangles of a color as subpaths of one filled path per pass.
This removes shared interior edges while preserving the original geometry,
missing cells and drawing order between passes. PNG and screen drawing keep
their original cell-by-cell rendering. A pass is compacted only when its
converted rectangles are monotone and non-overlapping. Other projections,
patterns and negative-alpha masks keep their existing rendering. The rectangle
buffer is capped at 64 MiB; unsupported states or allocation limits replay
the accumulated cells in their original order. Cairo may use additional
memory while writing paths.

`grfill`, `shaded`/`shade2` and `shade2b` also unite compatible same-color
partitions before filling: shared interior edges are removed, remaining
edges are traced into outer and hole rings, and exactly collinear vertices
are removed. This uses the vertices Cairo actually stores, with no contour
smoothing, coordinate tolerance or boundary approximation. Missing areas
and the winding of hole rings are retained. Separate display passes are
never united. This is a boundary union of the non-overlapping partitions
these renderers produce, not a general Boolean operation on overlapping
arbitrary polygons.

Unite's working arrays are limited to 96 MiB; Cairo may allocate additional
path memory. Unsupported paths, degenerate topology or allocation limits
retain the original compound path. `OPENGRADS_VECTOR_UNITE=0` disables only
this boundary union, retaining the earlier object/path compaction.

Opaque `shaded`/`shade2` and `shade2b` combine consecutive same-color
polygons into compound paths. Contour shape and holes are retained; Unite
can remove cut edges and exactly collinear vertices. Pending paths are flushed at
262,144 vertices and at changes of drawing state. Legacy `shade1` keeps its
existing rendering because its polygons may overlap.

PDF/SVG also compact non-overlapping fills with positive partial alpha,
including `-alpha 125` colors. Each display pass remains a separate layer;
overlapping layers accumulate opacity in their original order. PS/EPS
partial-alpha fills retain their existing Cairo fallback. Contour lines,
dashes, widths and label masks are not combined with filled regions.

Hershey text uses one stroked path per visible character, with separate
subpaths for disconnected strokes. Character positions, rotation, size,
line width and clipping are retained. Native Cairo fonts keep their own
font/text representation. A partially transparent character is now painted
once, so crossing strokes within that character share its opacity instead
of becoming darker at their intersection. Negative-alpha mask rendering
keeps its existing behavior. PNG and screen text are unchanged.

Each complete coordinate gridline uses one stroked vector path, retaining
all software-dashed segments as subpaths. Projected curves and clipped gaps
retain their original vertices. Different gridlines remain separate so
their crossings keep the original layer order. Positive alpha is applied
once within each line; negative-alpha masks retain their previous rendering.
Screen and PNG gridlines keep their original drawing behavior.

`OPENGRADS_VECTOR_COMPACT=0` disables vector compaction for comparisons with
the original geometry. It does not change the new page/background behavior.

For one 1024 by 1024 test field, a white-background `grfill` SVG decreased
from 205.47 MiB to 4.74 MiB (1,048,576 filled cell paths to 12 color paths).
The corresponding PDF decreased from 4.37 MiB to 0.69 MiB. A second
alpha-125 terrain layer reduced the combined SVG from 423.87 MiB to
5.75 MiB. `shaded` retained all contour vertices while reducing that field's
SVG from 8.91 MiB to 7.91 MiB. Results depend on the field: fewer objects do
not guarantee a smaller compressed PDF for every sparse or irregular plot.

Transparent PS/EPS pages leave unpainted areas for the importing application
or paper to supply. PostScript does not have PDF/SVG's native partial-alpha
compositing; the existing Cairo image fallbacks for partial transparency
remain in use. A full vector page does not guarantee every transparent
effect within a PS/EPS file is an editable vector object.

For the bundled color script, use a palette such as
`color.gs 0 10 -kind black->white -gxout grfill -alpha 125`.
`shaded` is a drawing mode, not a palette name. Invalid palettes are checked
before changing plot settings. User scripts earlier in `GASCRP`, including
an existing `~/.grads/color.gs`, retain priority over the bundled copy.

`pytests/TestVectorExport.sh` checks page bounds, uniform margins, custom and
transparent backgrounds, data-color isolation and recovery after failed
exports, color coverage, holes, layer compositing and character strokes with
Python 3.11 and no additional Python packages. It also compiles a Cairo
fill oracle with the configured C compiler to check random partitions,
T-junctions, holes, point contacts and allocation-failure fallback. Both release
builders run it after compiling the Cairo plug-ins.
