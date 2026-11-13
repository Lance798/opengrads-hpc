# Adding a built-in function

Use this checklist for C expression functions such as `zslice`, `zint`, and
`qvsat`. Script functions and dynamically loaded UDFs use their own script
or extension registration; this catalog check covers the core built-ins.

## Required updates

| Update | Location | What to check |
|---|---|---|
| Implementation and registration | `cola/src/gafunc.c`, especially `rtnprs()` | Declare/implement the handler and register its lowercase public name. Validate argument counts before reading arguments; handle missing values, dimensions, cleanup, and errors. |
| Tab and usage hints | `cola/src/gahelp.c`, `funcs[]` | Add the same name, accurate argument syntax, and an example. Keep names in alphabetical order. This one entry feeds both expression completion and prompt error hints. |
| Numerical and interactive regression | `pytests/Test<Function>.sh` and `pytests/prompt_completion.py` | Check reference values, masks, supported dimensions, invalid arguments, and cleanup; verify completion after `d`, in DEFINE, and inside another function where applicable. |
| User documentation | A guide under `docs/`, linked from `README.md` | Document units, optional-argument defaults, domain/axis rules, missing-value behavior, supported inputs, examples, and limits. Update release notes for user-visible additions. |

There is no second function-name list to edit in `grads.c`: its completer
reads the shared `gahelp.c` catalog. A function registered only in the
evaluator can run correctly while remaining invisible to Tab and usage
hints, which is what the catalog check prevents.

Keep the core invariants from [ARCHITECTURE.md](ARCHITECTURE.md): values are
`gadouble`, validity is separate in `umask`, axis indexes are X/Y/Z/T/E =
0/1/2/3/4, and allocations/handles need cleanup on every exit path.

## Conditional updates

- **Time workers:** review `cola/src/gatime.c:time_expression()` and its
  function allowlist, plus [TIME_WORKERS.md](TIME_WORKERS.md). Adding a
  built-in does not automatically make it worker-safe. Claim worker support
  after checking serial/worker equivalence, masks, coordinates, file-qualified
  operands, DEFINE inputs, and the supported backends. Keep serial fallback
  available for expressions outside the proven allowlist. Update coverage
  counts and the relevant time-worker tests when support changes.
- **Spatial batching or direct reads:** review the eligibility lists in
  `cola/src/gafunc.c:spatial_plan()` and any affected reader paths. Validate
  optimized/fallback equivalence and measure performance before enabling a
  new accelerated path; see [SPATIAL_REDUCTIONS.md](SPATIAL_REDUCTIONS.md).
- **A new C source file or external dependency:** update
  `cola/src/Makefile.am` and, when needed, `cola/configure.ac`/`cola/m4/`.
  Follow the existing generated-file workflow. Check both the Linux and
  macOS release builds, including optional-feature-disabled configurations.
- **New regression scripts:** add them to both `release/build-release.sh`
  and `release/build-release-macos.sh`, with any required feature guards.
  Generated fixtures and output should respect `TMPDIR`. Set it to a
  directory under the repository's `tmp/` during local development.

Update release version metadata as part of the release process described in
[RELEASES.md](RELEASES.md).

## Quick check before building

From the repository root:

```bash
python3 pytests/check_function_catalog.py
```

This needs only Python 3's standard library. It does not start GrADS, write
files, or require runtime dependencies. It fails with the exact names of
missing Tab/usage entries, catalog names absent from the evaluator, or
duplicate entries. For example, registering `newfunc` without updating
`funcs[]` reports:

```text
Function catalog check failed:
Missing Tab/usage entries: newfunc
Update cola/src/gafunc.c:rtnprs and cola/src/gahelp.c:funcs[] together.
See docs/ADDING_FUNCTIONS.md for the new-function checklist.
```

GitHub Linux and macOS jobs run this check before compilation to catch
catalog drift early. `TestPrompt.sh` also runs it before the interactive
tests, including when Readline is disabled; both release builders invoke
that test script.

## Build and verify behavior

Restore your configured compiler and runtime environment as described in
[INSTALL.md](INSTALL.md), then rebuild and test. For the standard `build/`
tree:

```bash
mkdir -p tmp/function-tests
export TMPDIR="$PWD/tmp/function-tests"
export OPENGRADS_BUILD_ROOT="$PWD/build"
make -C "$OPENGRADS_BUILD_ROOT/src" -j4 grads
./pytests/TestPrompt.sh
```

Use the path of your actual configured build when different. Run the new
function's numerical regression as well; catalog/Tab checks do not establish
numerical correctness or performance. Report the build configuration, exact
commands, numerical comparisons, and any backend that was not tested.
