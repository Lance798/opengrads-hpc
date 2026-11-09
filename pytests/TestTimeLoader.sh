#!/usr/bin/env bash
# Linux time workers retain an explicit loader and its library search path.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
  printf 'SKIP: explicit ELF loader regression requires Linux\n'; exit 0
fi
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
if ! grep -Eq '^#define USEHDF5 1$' "$build_root/src/config.h" 2>/dev/null &&
   [[ ! -r "$build_root/../loader-name.txt" ]]; then
  printf 'SKIP: explicit time-worker test requires HDF5\n'; exit 0
fi
# EL8 supplies HDF5 headers/libraries without a pkg-config file. Reuse the
# completed configure probes for fixture compilation in that environment.
if [[ -z "${GA_TEST_HDF5_LIBS:-}" ]] && ! pkg-config --exists hdf5; then
  makefile="$build_root/src/Makefile"
  export GA_TEST_HDF5_CFLAGS="$(sed -n 's/^HDF5_CFLAGS *= *//p' "$makefile")"
  export GA_TEST_HDF5_LIBS="$(sed -n 's/^HDF5_LDFLAGS *= *//p' "$makefile") $(sed -n 's/^HDF5_LIBS *= *//p' "$makefile")"
fi
loader="$(readelf -l "$build_root/src/grads" | sed -n 's/.*interpreter: \(.*\)]/\1/p')"
[[ -x "$loader" ]] || { printf 'ELF interpreter unavailable: %s\n' "$loader" >&2; exit 1; }
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-time-loader.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
export OPENGRADS_UPDATE_CHECK=0
export GA_TEST_TARGET_LAUNCHER="${OPENGRADS_LAUNCHER:-}"
if [[ -n "$GA_TEST_TARGET_LAUNCHER" ]]; then
  [[ -r "$(dirname -- "$GA_TEST_TARGET_LAUNCHER")/loader-name.txt" ]] || {
    printf 'Packaged launcher has no bundled loader metadata\n' >&2; exit 1;
  }
  export GA_TEST_REAL_GETCONF="$(command -v getconf)" GA_TEST_LOADER_LOG="$test_root/loader-used"
  mkdir -p "$test_root/bin"
  cat > "$test_root/bin/getconf" <<'GETCONF'
#!/usr/bin/env bash
if [[ "${1:-}" == GNU_LIBC_VERSION ]]; then
  printf 'used\n' >> "$GA_TEST_LOADER_LOG"
  printf 'glibc 0.0\n'
else
  exec "$GA_TEST_REAL_GETCONF" "$@"
fi
GETCONF
  chmod +x "$test_root/bin/getconf"
  export PATH="$test_root/bin:$PATH"
fi
export GA_TEST_LOADER="$loader" GA_TEST_BUILD_ROOT="$build_root" GA_TEST_REPO="$repo_root"
cat > "$test_root/launch" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
if [[ -n "$GA_TEST_TARGET_LAUNCHER" ]]; then
  exec "$GA_TEST_TARGET_LAUNCHER" "$@"
fi
export GA_TIME_LOADER="$GA_TEST_LOADER"
export GA_TIME_LIBRARY_PATH="$(dirname -- "$GA_TEST_LOADER")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export GA_ROOT="$GA_TEST_BUILD_ROOT/src/.libs"
export GAUDPT="$GA_TEST_REPO/etc/udpt-local"
export GADDIR="$GA_TEST_REPO/cola/data"
export GASCRP="$GA_TEST_REPO/lib/scripts"
exec "$GA_TIME_LOADER" --library-path "$GA_TIME_LIBRARY_PATH" "$GA_TEST_BUILD_ROOT/src/grads" "$@"
WRAPPER
chmod +x "$test_root/launch"
export OPENGRADS_LAUNCHER="$test_root/launch"
bash "$repo_root/pytests/TestTimeWorkers.sh"
bash "$repo_root/pytests/TestTimeExpressionReductions.sh"
bash "$repo_root/pytests/TestTimeDefinedInputs.sh"
if [[ -n "$GA_TEST_TARGET_LAUNCHER" ]]; then test -s "$GA_TEST_LOADER_LOG"; fi
printf 'Explicit loader regression passed: output/input workers, values, graphics, progress and failure recovery\n'
