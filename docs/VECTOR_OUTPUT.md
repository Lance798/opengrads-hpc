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

Transparent PS/EPS pages leave unpainted areas for the importing application
or paper to supply. PostScript does not have PDF/SVG's native partial-alpha
compositing; the existing Cairo image fallbacks for partial transparency
remain in use. A full vector page does not guarantee every transparent
effect within a PS/EPS file is an editable vector object.

`pytests/TestVectorExport.sh` checks page bounds, uniform margins, custom and
transparent backgrounds, data-color isolation and recovery after failed
exports with Python 3.11 and no additional Python packages. Both release
builders run it after compiling the Cairo plug-ins.
