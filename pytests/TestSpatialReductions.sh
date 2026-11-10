#!/usr/bin/env bash
# Fixed-time spatial reductions share bounded readers and existing math kernels.
# Copyright (C) 2026 Aaron Hsieh. GPL-2.0-only; see COPYING.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/opengrads-spatial-reductions.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
if [[ "${GA_TEST_TIME_FORMAT:-binary}" != binary ]]; then
  if [[ "$GA_TEST_TIME_FORMAT" == bp5 ]]; then
    adios_config="${OPENGRADS_ADIOS2_ROOT:?BP5 tests need OPENGRADS_ADIOS2_ROOT}/bin/adios2-config"
    writer_define=-DZS_ADIOS
    read -r -a hflags <<< "$("$adios_config" --serial --c-flags)"
    read -r -a hlibs <<< "$("$adios_config" --serial --c-libs)"
  else
    source "$repo_root/pytests/time_worker_format.sh"
  fi
  "${CC:-cc}" -std=gnu99 -O2 "$writer_define" -DNX=17 -DNY=13 -DNT=3 \
    "${hflags[@]}" "$repo_root/pytests/zslice_writer.c" "${hlibs[@]}" -lm -o "$test_root/writer"
  "$test_root/writer" "$test_root" zint
fi
python3 -u "$repo_root/pytests/fixed_time_reductions.py" "$repo_root" "$test_root"
