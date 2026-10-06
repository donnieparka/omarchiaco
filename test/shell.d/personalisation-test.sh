#!/bin/bash

set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

make -C "$ROOT" test-panel-hints
pass "dynamic panel shortcuts and size constraints"

make -C "$ROOT" test-monitor
pass "native monitor transactions and persistent layout"

make -C "$ROOT" test
pass "native SSH bridge protocol and requester context"
