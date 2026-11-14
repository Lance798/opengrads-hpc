#!/usr/bin/env bash
# Vector page geometry, background isolation and graphics output regressions.
set -euo pipefail
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$repo_root/pytests/vector_export.py"
