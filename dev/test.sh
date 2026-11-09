#!/usr/bin/env bash
# Run the development regression suite in the same environment as the compiler.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/activate.sh"
export OPENGRADS_COLOR=0 OPENGRADS_UPDATE_CHECK=0
for test in BP5 BP5Compression SDFOpen OpenMP Version AveSection Undo Prompt Gscript \
  ClearCanvas ZSlice ZInt ReductionIO Saturation DefineNames BinaryHalo MaskThreshold \
  TimeWorkers TimeMeanWorkers TimeExpressions TimeExpressionReductions TimeDefinedInputs \
  NetCDFTimeWorkers H5Decode \
  Progress AxisScale ExtremeValues TermDisplay; do
  printf '\nRunning Test%s.sh\n' "$test"
  "$OPENGRADS_DEV_ROOT/pytests/Test$test.sh"
done
