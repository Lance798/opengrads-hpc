#!/usr/bin/env bash
# Run the complete HDF5 time-worker contract against classic and NetCDF-4.
# GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${OPENGRADS_BUILD_ROOT:-$repo_root/build}"
if [[ -r "$build_root/src/config.h" ]] && ! grep -Eq '^#define USENETCDF 1$' "$build_root/src/config.h"; then
  printf 'SKIP: NetCDF time workers require a NetCDF-enabled build\n'; exit 0
fi
for format in nc nc4; do
  GA_TEST_TIME_FORMAT="$format" bash "$repo_root/pytests/TestSpatialReductions.sh"
  for suite in Performance TimeWorkers TimeMeanWorkers TimeExpressions TimeExpressionReductions TimeDefinedInputs TimeSpatialFunctions TimeFileInputs TimeRoll; do
    printf '\nNetCDF time workers: %s / %s\n' "$format" "$suite"
    GA_TEST_TIME_FORMAT="$format" bash "$repo_root/pytests/Test$suite.sh"
  done
done
