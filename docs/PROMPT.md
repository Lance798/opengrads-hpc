# The prompt

At the `ga->` prompt, arrows can recall history matching the input prefix,
Tab completes what can come next, a command that fails says how it is used,
and file names may start with `~`.

## Default color and history

`opengrads` starts with color enabled (`OPENGRADS_COLOR=1`); no environment
prefix is needed. Set `OPENGRADS_COLOR=0` to disable it. The same defaults
apply to the development and release-test launchers.

An interactive terminal session automatically loads `$HOME/.grads.log` and
saves its command history on normal exit, including Ctrl-D. Startup keeps
the most recent 256 entries. Previous sessions can be recalled with the up
arrow or Ctrl-P and inspected with `history`. Set `OPENGRADS_HISTORY=0` to
disable automatic loading and saving. An explicit `-H path` takes priority
and selects that history file instead.

Piped input/output, `-E`, `-u`, and one-shot `-x` execution do not enable
history automatically. Explicit `-H` remains available when desired.
Writing history preserves earlier commands when a startup command exits
before entering the interactive prompt.

## History matching the input prefix

Type a prefix and press Up to recall the most recent history entry starting
with it. For example, `open ` recalls `open c.ctl`, then `open b.ctl`, then
`open a.ctl` as Up is repeated, skipping other commands. `set x ` and `d qv`
work the same way. The default match is case-sensitive and includes spaces.
The prefix is the text from the start of the line to the cursor on the first
arrow press; later text does not restrict the match.

Consecutive Up/Down presses retain that initial prefix. Down moves toward
newer matches, then restores the original draft and cursor, including any
text after the cursor. Up at the oldest match, or with no match, keeps the
current input. Readline skips consecutive identical matching lines during
prefix search. Recall only changes the input line; Enter executes it.

Editing or moving the cursor starts a new search on the next arrow press.
An empty prefix keeps ordinary history navigation. Ctrl-P/Ctrl-N keep their
mode-specific bindings (history in Emacs mode, menu completion in vi insertion
mode), and Ctrl-R keeps its existing search behavior.
Ctrl-C clears the input and search state without deleting history; Up on
the resulting blank line recalls the most recent command normally.

These defaults apply to the usual CSI and SS3 arrow sequences in Emacs and
vi insertion mode. System inputrc defaults do not disable this behavior.
Explicit personal `.inputrc` / `INPUTRC` bindings take precedence, including
bindings back to `previous-history` and `next-history`. The named functions
`opengrads-history-backward` and `opengrads-history-forward` can also be bound
explicitly. For example, to retain ordinary arrows:

```text
$if opengrads
"\e[A": previous-history
"\e[B": next-history
"\eOA": previous-history
"\eOB": next-history
$endif
```

History persistence and `OPENGRADS_HISTORY` retain their existing behavior.
Builds without Readline, `-E`, IPC and piped input do not use arrow search.

## Tab completion

Tab completes the word under the cursor from what fits there and immediately
lists choices when there are several. It needs GNU Readline in the
build (`q config` lists `readline`; the release archives have it).

The first Tab shows every match for the current context, including ordinary
files beside commands and scripts. Repeated Tabs show the same complete
list; there is no suggestion stage or top-N cutoff. Completion computes the
common prefix from all matches, and a sole match completes on its own.
At Readline's confirmation threshold (100 by default), it asks before
displaying the list.

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

Function completion includes all built-ins, including `zslice`, `zint`,
`esat`, `qvsat`, and `grarea`. Complete them in an expression, for example
`d zs<Tab>`, `define column = zi<Tab>`, or `d zint(qvs<Tab>`. Completion
inserts the opening `(` so arguments can follow. The prompt regression
compares the Tab/usage catalog with the evaluator's built-in function list
to catch missing entries when functions are added. Run the same check
without building GrADS with `python3 pytests/check_function_catalog.py`;
see [the new-function checklist](ADDING_FUNCTIONS.md).

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

Lists are grouped by type and color, without headings or a summary legend.
Category rows are consecutive, with no empty lines between them, and the
prompt follows immediately after the candidates.
At the start of a line the order is **Commands, Scripts, Directories, Files**;
an empty line lists only the current directory's Scripts, Directories, and
Files. Expressions list **Variables, Functions, Dimensions**. Defined and
file variables share the Variables group; command keywords use Options and
open-file identifiers use File numbers. Names are sorted within each group.
All candidates remain available from the first Tab, including ordinary
files beside matching commands or scripts. Explicit user Tab bindings such
as `menu-complete` are preserved; immediate full-list display replaces the default completion binding
in Emacs and vi insertion mode. The named Readline function
`opengrads-complete` is also available to bind in `~/.inputrc`.

Commands and variables are green, scripts and functions magenta, directories
and dimensions blue, and ordinary files use the terminal's default color.
`OPENGRADS_COLOR=0` disables category colors. Large lists use Readline's
confirmation threshold;
column widths adapt to the terminal, and paging spans all categories.
At `--More--`, Space advances a page, Enter advances a line, and `q` stops
listing. Readline's `page-completions` setting is respected. Control bytes
in file names are escaped for display and never written as terminal controls.

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
