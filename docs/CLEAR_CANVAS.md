# Clear Canvas

`clear canvas` (or `cc`, Clear Canvas) clears the whole picture and lets the
next display reuse your explicit plot settings. The plot, title, annotations, and color bar all
disappear. Open files, defined variables, dimension selections, color
definitions, map projection, plotting area, and axis options remain available.

```text
open simulation.ctl
set gxout shaded
set clevs 280 290 300
set ccols 4 3 2 6
set t 1
d th
cc
set t 2
d th
```

The second plot uses the same levels and colors. Repeat `cc`, select another
time or height, and display again without repeating the style commands.
Both `clear canvas` and `cc` work as commands and in scripts. Neither accepts
additional arguments. Tab completion includes `cc` and `clear canvas`.

## Settings and automatic scaling

GrADS normally consumes some settings after each display. Clear Canvas keeps
the explicit settings from the last successful graphical display so `cc` can
restore them:

- `clevs`, `ccols`, `cint`, `cmin`, and `cmax`;
- `ccolor`, `cstyle`, and `cmark`;
- `rbrange`, `black`, `arrscl`, and `fill`.

Other live settings, such as `cthick`, `rbcols`, `rgb`, `gxout`, `parea`,
`mproj`, `vrange`, axis labels, and antialiasing, remain in effect. Plot-specific
state, including shading query results, coordinate transforms, overlay counts,
and contour-label masks, is cleared. `q shades` therefore reports `None`
until a new shaded plot is drawn, and a color bar must be drawn again.

Only explicit choices are remembered. With no `clevs` or `cint` specified,
each new plot still calculates its own levels from the data. Automatically
selected line colors and vector magnitudes are likewise recalculated.
An automatically chosen line-axis range is recalculated on a new canvas;
an explicit `set vrange` is retained.

A `set` after plotting takes precedence over the remembered style, including
commands that disable an option, such as `set black off` or an empty
`set clevs`. Setting a new `cint` after plotting also takes precedence over
the previous plot's fixed `clevs`. Non-graphical `display` output (`gxout print`
or `stat`) does not replace the remembered graphical style.

## Compared with other clears

| Command | Effect |
| --- | --- |
| `c` / `clear` | Clear the picture and reset the usual per-frame user options. |
| `clear norset` | Clear the picture while keeping options that are still live; does not restore options already consumed by `display`. |
| `clear canvas` / `cc` | Clear the picture, keep live options, and restore explicit one-display settings for reuse. |

The existing commands are unchanged: two displays without `cc` still use the
normal one-display option lifecycle. Ordinary clears, `reset`, `reinit`, and
double-buffer resets discard the remembered style, so `cc` cannot resurrect
settings from before those resets.

When undo is enabled, `cc` is a step: `undo` restores the cleared picture and
its remembered style. A script is still one undo step. The existing restriction
on undo in double-buffer mode also applies to `cc`.

## Regression test

```bash
OPENGRADS_BUILD_ROOT=/path/to/build ./pytests/TestClearCanvas.sh
```

The test needs Python 3. It checks explicit and automatic color scales,
later overrides and disabled options, repeated clears, the existing clears,
undo, axis ranges, and antialiasing. With the Cairo printing plug-in it also
compares rendered PNGs to explicitly configured reference plots and verifies
that the cleared canvas is empty.
