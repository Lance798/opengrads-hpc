# The prompt

Three conveniences at the `ga->` prompt: Tab completes what can come next,
a command that fails says how it is used, and file names may start with `~`.

## Tab completion

Tab completes the word under the cursor from what fits there and immediately
lists choices when there are several. It needs GNU Readline in the
build (`q config` lists `readline`; the release archives have it).

At the start of a line, the first Tab suggests matching commands, `.gs`
scripts, and directories. The next consecutive Tab shows all matching
candidates, including ordinary files; later Tabs keep the full list visible.
The bottom legend says `Suggested 3/4` and `Tab: all matches` when some
matches are hidden, or `All 4` after expansion. Editing the line or moving
the cursor resets the suggestions. If there are no suggested types, the
first Tab falls back to all matches. The same rule applies to shell command
names after `!`.

Explicit paths (`./`, `../`, `~/`) and command arguments retain their full
context-specific list from the first Tab. There is no arbitrary top-N
cutoff. Completion always computes the common prefix from the full match
set: a sole suggestion does not automatically select itself when other
matches exist. Only a sole match in the full set completes on its own.
If the visible list exceeds Readline's confirmation threshold (100 by
default), it asks before displaying it.

| Where | Tab offers |
|---|---|
| empty line | files and directories in the current directory |
| start of the line, with a prefix | commands (`display`, `set`, `q`, ...), local file names, and `.gs` scripts along `GASCRP`; `run` is optional |
| `set ` | the set options |
| `set gxout `, `set grads `, ... | that option's keywords: `on`/`off`, the kinds of `gxout`, the `mpdset` resolutions, the `sdfwrite` flags, ... |
| `set dfile `, `close `, `q file ` | the numbers of the open files |
| `q ` | the query options |
| `draw `, `clear `, `reset ` | their options |
| `d `, `define x = `, `x = ` | the default file's variables (another file's as `ts.2`), defined variables, and functions, with their `(`; inside a function's parentheses also the dimensions, `t=`, `lon=`, ... |
| `undefine `, `sdfwrite `, `q define ` | defined variables |
| `run ` | scripts (`.gs`) in the current directory and along `GASCRP` |
| `open `, `sdfopen `, `bpopen `, `printim `, ... | file names, `~` included |
| `!` or `! ` followed by a command prefix | executable files along `PATH`, plus local file names |
| arguments after `!ls`, `! ls`, or another shell escape | file names and directories |
| arguments of a script or an unknown command | file names and directories |

Directories complete with a trailing `/`, so another Tab can continue into
them. Explicit paths such as `./`, `../`, and `~/` work at the start of a
line as well as after file commands. Hidden names appear when the file-name
prefix starts with `.`. With no matches, the input stays as typed; known
command options and expressions retain their own completions.

Candidates are sorted and duplicates removed. A command and a script with
the same base name remain separate: `clea<Tab><Tab>` offers both `clear`
and `clear.gs` when that script exists. Completion retains the `.gs`
extension; execution keeps its usual order (a built-in command takes
priority over an implicit script, and scripts are found locally before
`GASCRP`). Completing in the middle of a line preserves the later arguments.

Lists are grouped by type and color. A single legend below all candidates
names the nonempty categories and their counts; there are no group headings
between candidate rows.
At the start of a line the order is **Commands, Scripts, Directories, Files**;
an empty line lists only the current directory's Scripts, Directories, and
Files. Expressions list **Variables, Functions, Dimensions**. Defined and
file variables share the Variables group; command keywords use Options and
open-file identifiers use File numbers. Names are sorted within each group.
All candidates remain available in the expanded list, including ordinary
files beside matching commands or scripts; labels are never inserted into
the command line. Explicit user Tab bindings such as `menu-complete` are
preserved; the two-stage behavior replaces the default completion binding
in Emacs and vi insertion mode. The named Readline function
`opengrads-complete` is also available to bind in `~/.inputrc`.

Commands and variables are green, scripts and functions magenta, directories
and dimensions blue, and ordinary files use the terminal's default color.
The labels also distinguish categories when color is disabled with
`OPENGRADS_COLOR=0`. Large lists use Readline's confirmation threshold;
each category uses Readline's column layout and paging.

Shell escapes support command names, file arguments, and commands after
`|`, `&&`, or `;`. This is filename completion inside GrADS, not Bash's
programmable completion: it does not run a shell or completion scripts to
discover candidates, or supply command-specific flags.

```text
ga-> set gxout sh<Tab><Tab>
shade1   shade2   shade2b  shaded   shp
ga-> d ave(t<Tab><Tab>
t=       ta       tan(     tcorr(   time=    tloop(   tmave(   tregr(   ts ...
```

### Directory cache

Completion caches sorted directory contents for the current directory,
explicit paths, `GASCRP`, and `PATH`. Prefix searches use a binary lookup;
duplicate removal sorts once instead of comparing every pair of names.
The cache retains up to 32 directories and evicts the least recently used.
Directory metadata is checked at most once every two seconds; unchanged
directories are not reread. Matching file types and executable permissions
are also reused within that interval. A newly added or removed file or a
permission change may therefore take up to two seconds to appear (plus any
filesystem metadata caching). The first search still reads the directory.

Set `GA_COMPLETION_CACHE=0` before starting GrADS to disable caching and
refresh directory contents on every completion. `PATH` and `GASCRP` are
read for each completion, and relative paths are resolved against the
current working directory.

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
