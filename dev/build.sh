#!/usr/bin/env bash
# Incremental source build shared by local development and GitHub Actions.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/activate.sh"
export OPENGRADS_BUILD_JOBS="${OPENGRADS_BUILD_JOBS:-4}"
export OPENGRADS_BUILD_SDF_DEPS=1 OPENGRADS_BUILD_TMUX=1 OPENGRADS_BUILD_JPEG=1
"$OPENGRADS_DEV_ROOT/release/build-dependencies.sh" "$OPENGRADS_DEV_ROOT/.release-work"
mkdir -p "$OPENGRADS_BUILD_ROOT/src" "$OPENGRADS_BUILD_ROOT/lib"
cd "$OPENGRADS_BUILD_ROOT"
"$OPENGRADS_DEV_ROOT/cola/configure" \
  --enable-dyn-supplibs --enable-openmp --enable-sdfopen \
  --with-opengrads --without-gadap \
  --with-adios2="$OPENGRADS_ADIOS2_ROOT" \
  --with-netcdf="$OPENGRADS_DEPS_ROOT" --with-udunits="$OPENGRADS_DEPS_ROOT"
make -C src --jobs "$OPENGRADS_BUILD_JOBS" \
  grads libgxdummy.la libgxdX11.la libgxdCairo.la libgxdTerm.la libgxpCairo.la
printf '\nBuild complete. Run: ./dev/run ./opengrads\n'
