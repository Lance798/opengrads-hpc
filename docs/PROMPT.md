# The prompt

Three conveniences at the `ga->` prompt: Tab completes what can come next,
a command that fails says how it is used, and file names may start with `~`.

## Tab completion

Tab completes the word under the cursor from what fits there; a second Tab
lists the choices when there are several. It needs GNU Readline in the
build (`q config` lists `readline`; the release archives have it).

| Where | Tab offers |
|---|---|
| start of the line | commands (`display`, `set`, `q`, ...) |
| `set ` | the set options |
| `set gxout `, `set grads `, ... | that option's keywords: `on`/`off`, the kinds of `gxout`, the `mpdset` resolutions, the `sdfwrite` flags, ... |
| `set dfile `, `close `, `q file ` | the numbers of the open files |
| `q ` | the query options |
| `draw `, `clear `, `reset ` | their options |
| `d `, `define x = `, `x = ` | the default file's variables (another file's as `ts.2`), defined variables, and functions, with their `(`; inside a function's parentheses also the dimensions, `t=`, `lon=`, ... |
| `undefine `, `sdfwrite `, `q define ` | defined variables |
| `run ` | scripts (`.gs`) in the current directory and along `GASCRP` |
| `open `, `sdfopen `, `bpopen `, `printim `, ... | file names, `~` included |

```text
ga-> set gxout sh<Tab><Tab>
shade1   shade2   shade2b  shaded   shp
ga-> d ave(t<Tab><Tab>
t=       ta       tan(     tcorr(   time=    tloop(   tmave(   tregr(   ts ...
```

## Usage after an error

When a command typed at the prompt fails, its usage follows the error: a
line of syntax, as in the GrADS documentation index, and an example. A
name that is neither a known command, option, variable nor function gets
the nearest ones that are.

```text
ga-> d mean(ts)
Error from MEAN:  Too many or too few args
...
DISPLAY error:  Invalid expression
  Expression = mean(ts)
  Usage: mean(expr, dim1, dim2 <,tinc> <,-b>)
  e.g.   mean(ts, t=1, t=12)
ga-> set gxout
SET error:  Missing or invalid arguments for GXOUT option
  Usage: set gxout contour|shaded|grfill|line|bar|vector|stream|barb|grid|print|stat|...
  e.g.   set gxout shaded
ga-> d tss
...
  Did you mean: ts?
ga-> set gxot shaded
...
  Did you mean: gxout, wxopt?
```

This covers the functions, the set and query options, `draw`, and the
other commands. In an expression the usage is that of the function that
failed, or that was left open (`d mean(`). Commands run by a script print
what they always printed: scripts read a command's output in `result`,
and it stays as it was.

## `~` in file names

A file name may start with `~` for your home directory, as in a shell
(`~user` for another user's):

```text
ga-> open ~/data/model.ctl
ga-> sdfopen ~/run/out.nc
ga-> bpopen ~/run/output.bp
ga-> printim ~/plots/t2m.png
ga-> run ~/gs/plot.gs
```

This holds for the commands that take file names (`open`, `sdfopen`,
`xdfopen`, `bpopen`, `exec`, `gxprint`, `printim`, `print`, `enable print`,
`run`, `draw shp`, `q dbf`, `q shp`, and `set sdfwrite`, `fwrite`,
`geotiff`, `kml`, `shp`, `imprun`), for a script's `read`, `write` and
`close`, and in a descriptor for `DSET`, `INDEX`/`STNMAP` and `PDEF` files,
as `$HOME/...` already was. A script's arguments are passed on as typed.
