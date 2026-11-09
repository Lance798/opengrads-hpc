#!/usr/bin/env bash
# Build and package a runnable Linux release using this checkout's environment.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/activate.sh"
if [[ "$(uname -s)" != Linux ]]; then
  printf 'Use the platform release builder in release/ on this host.\n' >&2
  exit 1
fi
export OPENGRADS_BUILD_PATCHELF=1
"$OPENGRADS_DEV_ROOT/dev/build.sh"
"$OPENGRADS_DEV_ROOT/release/package-linux.sh" \
  "$OPENGRADS_BUILD_ROOT" "$OPENGRADS_DEPS_ROOT" "$OPENGRADS_ADIOS2_ROOT" \
  "$OPENGRADS_DEV_ROOT/.release-work" "$OPENGRADS_DEV_ROOT/release-dist"
