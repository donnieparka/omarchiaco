#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

require_command cc
require_command pkg-config
ulimit -c 0
scratch=$(mktemp -d)
trap 'rm -f "$scratch/monitor-reflow"; rmdir "$scratch"' EXIT

cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  $(pkg-config --cflags json-c) -I"$ROOT/default/native" \
  "$ROOT/test/shell.d/fixtures/monitor-reflow.c" "$ROOT/default/native/monitor-model.c" \
  $(pkg-config --libs json-c) -lm -o "$scratch/monitor-reflow"
"$scratch/monitor-reflow" || fail "geometry changes preserve relative monitor layout"
pass "geometry changes preserve relative monitor layout"
