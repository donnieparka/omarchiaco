#!/bin/bash

set -euo pipefail

source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/base-test.sh"

require_command cc
require_command pkg-config
require_command python3
ulimit -c 0
scratch=$(mktemp -d)
trap 'rm -f "$scratch/monitor-control"; rmdir "$scratch"' EXIT

cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
  -DMONITOR_CONFIRM_SECONDS=2 -DMONITOR_RECHECK_MS=5000 \
  $(pkg-config --cflags json-c) -I"$ROOT/default/native" \
  "$ROOT/default/native/omarchy-monitor-control.c" "$ROOT/default/native/monitor-model.c" \
  $(pkg-config --libs json-c) -lm -o "$scratch/monitor-control"
python3 "$ROOT/tests/run-monitor-control.py" "$scratch/monitor-control" \
  MonitorControlTests.test_daemon_idle_and_unrelated_events_do_not_query_hyprland \
  MonitorControlTests.test_daemon_reassembles_fragmented_events_and_ignores_oversized_lines \
  MonitorControlTests.test_daemon_rescans_after_socket_disconnect \
  MonitorControlTests.test_daemon_throttles_repeated_socket_eof \
  MonitorControlTests.test_daemon_retries_failed_inventory_before_the_slow_backstop \
  MonitorControlTests.test_daemon_slow_backstop_imports_changes_without_an_event \
  MonitorControlTests.test_running_daemon_recovers_killed_watchdog_without_socket_events \
  MonitorControlTests.test_daemon_hotplug_preserves_individual_saved_coordinates \
  MonitorControlTests.test_daemon_handles_hotplug_during_pending_confirmation \
  || fail "event-driven monitor reconciliation preserves idle and recovery behaviour"
pass "event-driven monitor reconciliation preserves idle and recovery behaviour"
