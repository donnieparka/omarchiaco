#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

require_command cc
require_command make
require_command pkg-config
require_command python3
ulimit -c 0
scratch=$(mktemp -d)
trap 'rm -f "$scratch/monitor-model-test" "$scratch/omarchy-monitor-control"; rmdir "$scratch"' EXIT

make -s -f "$ROOT/default/native/Makefile" BUILD_DIR="$scratch" \
  "$scratch/monitor-model-test" "$scratch/omarchy-monitor-control"
"$scratch/monitor-model-test" || fail "native scale presets apply without normalization"
python3 "$ROOT/tests/run-monitor-control.py" "$scratch/omarchy-monitor-control" ScaleOptionsTests \
  || fail "scale-options is bounded and independent of session and state"
pass "native scale presets apply unchanged and scale-options is read-only"
