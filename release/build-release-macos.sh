#!/usr/bin/env bash
# Build, test, and package a native macOS opengrads-hpc release archive.

set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
  printf 'This release builder must run on macOS.\n' >&2
  exit 2
fi

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
work_root="${OPENGRADS_RELEASE_WORK_ROOT:-$repo_root/.release-work-macos}"
output_root="${OPENGRADS_RELEASE_OUTPUT_ROOT:-$repo_root/release-dist}"
build_root="$work_root/opengrads-build"
jobs="${OPENGRADS_BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
export OPENGRADS_RELEASE_WORK_ROOT="$work_root"

for formula in autoconf automake cairo cmake coreutils gcc libgeotiff hdf5 libomp \
               libtool libx11 libxext libxmu netcdf pkgconf readline udunits; do
  if ! brew list --versions "$formula" >/dev/null 2>&1; then
    printf 'Required Homebrew formula is not installed: %s\n' "$formula" >&2
    exit 1
  fi
done

gcc_bin="$(find "$(brew --prefix gcc)/bin" -maxdepth 1 -name 'gcc-[0-9]*' -type f \
  | sort -V | tail -n 1)"
if [[ -z "$gcc_bin" ]]; then
  printf 'Homebrew GCC compiler was not found.\n' >&2
  exit 1
fi
gcc_suffix="${gcc_bin##*-}"
export CC="$gcc_bin"
export CXX="$(dirname -- "$gcc_bin")/g++-$gcc_suffix"
export PATH="$(brew --prefix coreutils)/libexec/gnubin:$PATH"

"$repo_root/release/build-dependencies.sh" "$work_root"
deps_root="$work_root/deps"
adios2_root="$work_root/adios2"
adios_config="$adios2_root/include/adios2/common/ADIOSConfig.h"
if ! grep -Eq '^#define ADIOS2_HAVE_BLOSC2($|[[:space:]]+1)' "$adios_config"; then
  printf 'The release ADIOS2 build does not have Blosc2 support.\n' >&2
  exit 1
fi

brew_prefix="$(brew --prefix)"
export PKG_CONFIG_PATH="$adios2_root/lib/pkgconfig:$deps_root/lib/pkgconfig:$brew_prefix/lib/pkgconfig:$(brew --prefix cairo)/lib/pkgconfig:$(brew --prefix libgeotiff)/lib/pkgconfig:$(brew --prefix hdf5)/lib/pkgconfig:$(brew --prefix netcdf)/lib/pkgconfig:$(brew --prefix udunits)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# Homebrew symlinks every non-keg-only formula's headers and libraries into its
# own prefix, but Homebrew GCC does not search there by default. configure's
# NetCDF, HDF5, Cairo, GeoTIFF, and UDUNITS probes try an empty include
# directory first and then fall back to hardcoded /usr/local and /opt paths
# that do not match Homebrew's layout, so without this they all miss.
# Homebrew's readline is keg-only because it shadows the editline shim in the
# macOS SDK, so its include and lib directories go ahead of everything else.
readline_prefix="$(brew --prefix readline)"
export CPPFLAGS="-I$deps_root/include -I$readline_prefix/include -I$brew_prefix/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$adios2_root/lib -L$deps_root/lib -L$readline_prefix/lib -L$brew_prefix/lib${LDFLAGS:+ $LDFLAGS}"
export PKG_CONFIG_PATH="$readline_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"

rm -rf -- "$build_root"
mkdir -p "$build_root/src" "$build_root/lib" "$output_root"
cd "$build_root"
"$repo_root/cola/configure" \
  --enable-dyn-supplibs \
  --enable-openmp \
  --enable-sdfopen \
  --with-opengrads \
  --without-gadap \
  --with-adios2="$adios2_root"
# The X displays need Homebrew's X libraries (Cairo already depends on them)
# and, for the X11 display, the Xmu headers. They are loaded only when asked
# for, so the archive still runs without XQuartz. The terminal display
# (Term) draws with Cairo alone and shows the picture in iTerm2 or WezTerm.
make -C src --jobs "$jobs" grads libgxdummy.la libgxpCairo.la libgxdCairo.la libgxdX11.la \
  libgxdTerm.la

runtime_libraries="$adios2_root/lib:$deps_root/lib:$(brew --prefix gcc)/lib/gcc/current:$(brew --prefix libomp)/lib:$(brew --prefix netcdf)/lib"
export OPENGRADS_BUILD_ROOT="$build_root"
export OPENGRADS_ADIOS2_ROOT="$adios2_root"
export OPENGRADS_DEPS_ROOT="$deps_root"
export OPENGRADS_LAUNCHER="$repo_root/release/launch-local.sh"
export OPENGRADS_RELEASE_PLATFORM=Darwin
export OPENGRADS_RUNTIME_LIBRARY_PATH="$runtime_libraries"
"$repo_root/pytests/TestBP5.sh"
"$repo_root/pytests/TestBP5Compression.sh"
"$repo_root/pytests/TestSDFOpen.sh"
"$repo_root/pytests/TestOpenMP.sh"
"$repo_root/pytests/TestIf.sh"
"$repo_root/pytests/TestRoll.sh"
"$repo_root/pytests/TestVersion.sh"
"$repo_root/pytests/TestAveSection.sh"
"$repo_root/pytests/TestUndo.sh"
"$repo_root/pytests/TestPrompt.sh"
"$repo_root/pytests/TestGscript.sh"
"$repo_root/pytests/TestVVMTopo.sh"
"$repo_root/pytests/TestClearCanvas.sh"
"$repo_root/pytests/TestZSlice.sh"
"$repo_root/pytests/TestZInt.sh"
"$repo_root/pytests/TestSaturation.sh"
"$repo_root/pytests/TestDefineNames.sh"
GA_TEST_TIME_FORMAT=bp5 "$repo_root/pytests/TestTimeSpatialFunctions.sh"
GA_TEST_TIME_FORMAT=bp5 "$repo_root/pytests/TestTimeFileInputs.sh"
GA_TEST_TIME_FORMAT=bp5 "$repo_root/pytests/TestTimeRoll.sh"

"$repo_root/pytests/TestProgress.sh"
"$repo_root/pytests/TestAxisScale.sh"
"$repo_root/pytests/TestExtremeValues.sh"
# Its tmux checks run when tmux is installed (the release workflow installs it).
"$repo_root/pytests/TestTermDisplay.sh"

"$repo_root/release/package-macos.sh" "$build_root" "$output_root"

# The archive just made updates itself, to a copy of itself numbered higher.
OPENGRADS_UPDATE_ARCHIVE="$output_root/opengrads-hpc-$(<"$repo_root/release/VERSION")-macos-$(uname -m).tar.gz" \
  "$repo_root/pytests/TestUpdate.sh"
