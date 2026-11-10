# VVM terrain obstruction heights

After opening a terrain dataset with the project's `opengrads` launcher, type:

```text
vvmtopo topo
```

The launcher includes `lib/scripts` in `GASCRP`, so no script path or `.gs` extension is needed. Use an executable with the
ROLL built-in; restart GrADS after upgrading the executable. `run vvmtopo topo` also works, including
inside another GrADS script. Release archives include the script library.
The current directory and user-supplied `GASCRP` directories retain priority.

```text
vvmtopo topo 1 1
vvmtopo topo 0 0
vvmtopo topo 1 0
```

The optional arguments control periodic X and Y boundaries independently;
each defaults to `1`. A `0` keeps the last column/row's own height at that
boundary. Periodic boundaries instead compare against the first column/row.

The command selects the full native X/Y domain of the default open file,
fixes Z=1 and T=1, and leaves that dimension environment active. Use
`set dfile N` first if another file should supply the grid. The first argument
is the terrain field, such as `topo` or `topo.2` on a compatible grid.

It creates or replaces four defined variables:

| Variable | Value |
| --- | --- |
| `toposrc` | Input terrain, with each zero replaced by one |
| `topou` | Maximum of this cell and its neighbor in the positive X direction |
| `topov` | Maximum of this cell and its neighbor in the positive Y direction |
| `topow` | The corrected `toposrc` field |

The input field and source dataset are preserved, unless the input uses one
of these reserved result names. Input must contain fully defined, finite
terrain values. These are obstruction envelopes for monotonically increasing
terrain masks, rather than full ITYPE masks.

Whole-grid IF and [ROLL](ROLL.md) operations handle periodic neighbors with
exact copies, including the boundary, without per-cell interpreter commands.
Closed boundaries use forward CDIFF with a zero increment at the final cell.
Grid calculations can use OpenMP via `set threads N`; the fixed single time
does not use time workers. The closed-edge CDIFF path reconstructs neighbors
with double subtraction/addition and is validated for the small integer VVM
terrain indices. Extreme magnitude differences on that path and
missing/nonfinite input are outside the script's contract.

The ROLL script was compared with the original `itype_topo.gs` on all three
supplied terrain datasets at four cores. Full U/V/W float32 outputs agreed.
Its analytic tests also cover 1/4/8 threads, all boundary modes and periodic
neighbors with widely separated finite magnitudes. A Taiwan 1024x1024 benchmark with periodic X/Y boundaries measured
12.02 s for the original script and 0.084 s for the preceding CDIFF script with
four cores (medians of 3 and 5 runs, respectively). Those measurements predate
the ROLL integration. Timing includes source
reads and definitions, excludes startup/export/graphics, and depends on host
and cache conditions. `pytests/TestVVMTopo.sh` independently verifies numeric
results, periodic defaults, explicit overrides and library lookup from a
different working directory.

See [ROLL performance validation](PERFORMANCE.md#roll-and-terrain-integration-2026-11-09)
for the current script timings and worker numerical checks.
