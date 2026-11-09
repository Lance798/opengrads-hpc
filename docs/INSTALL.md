# Building from source

This guide builds opengrads-hpc — GrADS with the ADIOS2/BP5 reader and
OpenMP-threaded calculations — from source on 64-bit Linux. The supported
baseline is a serial, CPU-only ADIOS2 build; MPI, CUDA, and Kokkos are
deliberately disabled.

**Most people do not need this page.** Unpack a release archive and run
`./opengrads` — no compiler and no ADIOS2 installation required. See
[RELEASES.md](RELEASES.md). Build from source if you want to modify the code,
target a platform we do not publish, or avoid the redistribution question
described in [LICENSING.md](LICENSING.md).

## Prerequisites

A C and C++ compiler, `make`, `cmake`, `curl`, `tar`, and `sha256sum`, plus the
graphics and data-format development headers. On Ubuntu or Debian:

```bash
sudo apt install autoconf automake build-essential cmake curl gawk libtool \
  pkg-config libcairo2-dev libx11-dev libxext-dev libxmu-dev libice-dev \
  libsm-dev libgeotiff-dev libtiff-dev libhdf5-dev libjpeg-dev \
  libnetcdf-dev libudunits2-dev zlib1g-dev
```

Use the equivalent packages on other distributions. On a cluster, `module
avail` may provide the compiler and CMake. No root access is needed beyond
these packages.

## Project development environment (Linux, no Conda)

For ongoing development, use the private environment in this checkout. The
same commands run in `.github/workflows/build.yml` on every branch push and
pull request, on Ubuntu 22.04 and the RHEL 8 / glibc 2.28 family.

Install the host compiler, graphics, HDF5, curl, Expat and libevent development packages
once. On Ubuntu/Debian:

```bash
sudo apt install build-essential gfortran autoconf automake libtool cmake curl \
  tar patch gawk bison pkg-config python3 libcairo2-dev libx11-dev libxext-dev \
  libxmu-dev libice-dev libsm-dev libgeotiff-dev libtiff-dev libhdf5-dev \
  libjpeg-dev zlib1g-dev libcurl4-openssl-dev libexpat1-dev libevent-dev
```

The workflow lists equivalent RHEL-family packages. Then, from the checkout:

```bash
./dev/build.sh
./dev/test.sh
source dev/activate.sh
opengrads
```

This builds checksum-pinned ncurses, Readline, Blosc2, serial ADIOS2, NetCDF,
UDUNITS, libjpeg-turbo and tmux into `.release-work`, then builds GrADS and the graphics plug-ins
into `build`. No Conda environment or root install is used. Both directories
are ignored by Git. The host GCC/G++, CMake, HDF5, libevent and graphics libraries are
still prerequisites; their versions may differ between systems. This is an
isolated project dependency environment, not a container or a fully pinned OS.

Activation makes `opengrads`, `adios2-config` and `nc-config` use this checkout,
sets the build/test paths and the UDUNITS database, and replaces inherited
Intel/MPI/other-environment include and library search paths. Use a fresh
shell or deactivate another package environment first. Run
`opengrads_deactivate` to restore the previous shell variables.

To keep your current shell unchanged, prefix any command with `./dev/run`:

```bash
./dev/run ./opengrads
./dev/run make -C build/src -j4 grads
./dev/run ./pytests/TestBP5.sh
./dev/run ./pytests/TestSDFOpen.sh
```

Re-run `./dev/build.sh` after source changes; installed dependencies are reused
and compilation is incremental. Keep using these commands for this checkout.
Do not mix its objects with a build configured against a different dependency
prefix. For a compiler module outside `/usr/local/bin:/usr/bin:/bin`, set
`OPENGRADS_TOOLCHAIN_PATH` before activation; changing compilers requires a
fresh `build` directory and fresh dependency builds.

Interactive tmux commands keep the executable selected before activation, so
activation does not replace the client used with an existing server. The
private tmux is selected through `TEST_TMUX` for isolated terminal tests.
To explicitly use it interactively, run this from the checkout root outside tmux:

```bash
OPENGRADS_HOST_TMUX="$PWD/.release-work/deps/bin/tmux" \
  ./dev/run tmux -L opengrads-dev new-session
```

The private tmux is not included in release archives. With an old server that cannot
identify iTerm2 and no forwarded `LC_TERMINAL`, use
`GA_TERM_PROTOCOL=iterm2 ./dev/run ./opengrads -l -d Term`.
The acceleration tests require Python 3.8 or newer; on
RHEL 8 install `python38`. Activation selects it when the default `python3`
is older. Legacy `/tmp` test templates are redirected into the checkout
`tmp/` directory. Host HDF5 headers are exposed through a local pkg-config
file on RHEL installations that lack one. HDF5 2-specific raw decoding tests
are skipped when the host provides an earlier HDF5 release.

To make a runnable Linux release from this environment:

```bash
./dev/package.sh
cd "release-dist/opengrads-hpc-$(cat release/VERSION)-linux-$(uname -m)"
./opengrads
```

The packager builds its private patchelf tool, reuses the development build,
collects runtime libraries and the UDUNITS database, verifies packaged BP5
reads in a clean environment, and writes a `.tar.gz` and `.sha256` alongside
the runnable directory. Intermediate files use `tmp/` in the checkout, which
is ignored by Git. Activate the development environment only for work
in the source checkout; an unpacked release starts with `./opengrads`.

The development workflow builds, tests, packages and runs an unpacked Linux
archive on both distributions. Uploading binary artifacts retains the existing
`BINARY_REDISTRIBUTION_APPROVED` gate; diagnostics are uploaded independently.
The existing release workflow below still produces the platform release archives.

## One command

```bash
./release/build-release.sh
```

This downloads checksum-pinned dependencies into `.release-work`, builds
everything, runs the BP5, SDF, OpenMP, and undo regression tests, and writes a
relocatable archive to `release-dist`. It is the same script CI runs, so it is
the best-tested path.

Use the steps below instead if you want a development build you can rebuild
incrementally.

## Step by step

### 1. Build the pinned dependencies

```bash
export OPENGRADS_WORK="$HOME/opengrads-deps"
./release/build-dependencies.sh "$OPENGRADS_WORK"

deps_root="$OPENGRADS_WORK/deps"        # ncurses, readline, Blosc2
adios2_root="$OPENGRADS_WORK/adios2"    # serial ADIOS2
```

This builds ncurses, GNU Readline, Blosc2, and ADIOS2 from checksum-pinned upstream
releases recorded in [`release/versions.env`](../release/versions.env) on Linux.
On macOS it builds only Blosc2 and ADIOS2; Homebrew provides the terminal and
graphics dependencies. It is incremental: components already installed are
skipped, except that an older ADIOS2 build without Blosc2 is rebuilt to enable
compressed BP5 reads.

To reuse dependencies you already have, skip this and point `deps_root` and
`adios2_root` at your own prefixes. `$adios2_root/bin/adios2-config` must
exist, and ADIOS2 must have Blosc2 enabled for compressed output. The Linux
packager requires shared Blosc2 and its codec notices under
`$deps_root/share/licenses/blosc2`, which the bootstrap installs.

### 2. Configure and build

```bash
repo_root="$PWD"
build_root="$repo_root/build"
jobs="${OPENGRADS_BUILD_JOBS:-4}"
mkdir -p "$build_root/src" "$build_root/lib"

unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH SUPPLIBS
export PATH="$deps_root/bin:$PATH"
export CPPFLAGS="-I$deps_root/include"
export LDFLAGS="-L$deps_root/lib -Wl,-rpath,$deps_root/lib"
export PKG_CONFIG_PATH="$deps_root/lib/pkgconfig:$deps_root/share/pkgconfig"
export LD_LIBRARY_PATH="$adios2_root/lib:$adios2_root/lib64:$deps_root/lib"

cd "$build_root"
"$repo_root/cola/configure" \
  --enable-dyn-supplibs \
  --enable-openmp \
  --enable-sdfopen \
  --with-opengrads \
  --without-gadap \
  --with-adios2="$adios2_root"

make -C src --jobs "$jobs" \
  grads libgxdummy.la libgxdX11.la libgxdCairo.la libgxdTerm.la libgxpCairo.la
```

The `build_root/lib` directory must exist before configuring — libtool fails
without it. Clearing `SUPPLIBS` and the include/library environment variables
matters on systems with site-wide toolchains, which otherwise inject unrelated
headers.

The configuration summary should report:

```text
+ ADIOS2 BP5 enabled     + OPENMP enabled      + CAIRO enabled
+ NETCDF4 enabled        + READLINE enabled
```

`READLINE enabled` means line editing, history, and Tab completion are
available, provided by GNU Readline.

To build without BP5, pass `--without-adios2`. Note the flag defaults to
`auto`, so omitting it is not enough — a system ADIOS2 on `PATH` is still
detected.

### 3. Verify

```bash
cd "$repo_root"
export OPENGRADS_BUILD_ROOT="$build_root"
export OPENGRADS_ADIOS2_ROOT="$adios2_root"

./pytests/TestBP5.sh
./pytests/TestSDFOpen.sh
./pytests/TestOpenMP.sh
```

All three must pass. Then check the compiled configuration:

```bash
printf 'q config\nquit\n' | ./opengrads -bl -d gxdummy -h gxdummy
```

The startup line should contain `readline netcdf adios2-bp5 openmp`.

### 4. Run

```bash
./opengrads
```

With an X display the launcher selects the Cairo plug-ins, falling back to X11
and then to headless `gxdummy`. Without one, in a terminal that shows
pictures (iTerm2, WezTerm, kitty, Ghostty), it selects the
[terminal display](TERMINAL.md). Opening BP5 data needs no descriptor file:

```text
bpopen /path/to/output.bp
q vars
set gxout shaded
display variable_name
```

Point the launcher at a build or ADIOS2 installation elsewhere with
`OPENGRADS_BUILD_ROOT` and `OPENGRADS_ADIOS2_ROOT`.

## Rebuilding after an update

Restore the environment from step 2, then:

```bash
git pull --ff-only origin main
make -C "$build_root/src" clean
make -C "$build_root/src" --jobs "$jobs" \
  grads libgxdummy.la libgxdX11.la libgxdCairo.la libgxdTerm.la libgxpCairo.la
```

Re-run `configure` only if you changed dependency prefixes or configure flags.
Avoid `make -B`, which can trigger unnecessary Autotools regeneration.

## Troubleshooting

**Executable not found.** Check `ls -l "$build_root/src/grads"`. If it is
missing, repeat step 2; if it lives elsewhere, set `OPENGRADS_BUILD_ROOT`.

**No X window.** Check `echo "$DISPLAY"`. Remote sessions need `ssh -X` or
`-Y`. Headless mode needs no X. From iTerm2, WezTerm, kitty, Ghostty or a
sixel terminal, the terminal display shows plots without X; see
[TERMINAL.md](TERMINAL.md).

**ADIOS2 not detected.** Confirm `"$adios2_root/bin/adios2-config" --serial
--c-flags` works, then reconfigure with that exact prefix.

**ADIOS2 pulls in MPI or libfabric.** An existing installation may advertise
serial flags while its core was built with MPI. Check the library itself:

```bash
ldd "$adios2_root"/lib*/libadios2_core.so | grep -E 'libmpi|libfabric|not found'
```

Any output means it is unsuitable; rebuild with `build-dependencies.sh`. After
changing `adios2_root`, run `make -C "$build_root/src" clean` so the executable
is relinked.

**ADIOS2 fails with a duplicate `writev`.** Site-wide include variables make
one bundled ADIOS2 component pick up another's `config.h`. Clear them and
resume:

```bash
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH
./release/build-dependencies.sh "$OPENGRADS_WORK"
```

**Missing `GLIBCXX` or `CXXABI` versions.** A legacy OpenGrADS graphics bundle
may carry an old `libstdc++.so.6`. Override the runtime only if needed:

```bash
OPENGRADS_CXX_RUNTIME=/absolute/path/to/libstdc++.so.6 ./opengrads
```

## Next

For BP5 commands, supported shapes, and descriptor syntax, continue with
[BP5.md](BP5.md). For threading controls, see
[PERFORMANCE.md](PERFORMANCE.md).
