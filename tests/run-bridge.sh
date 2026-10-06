#!/bin/bash
set -euo pipefail
bridge=$(realpath "${1:?compiled bridge required}")
fixture=$(realpath "${2:?compiled C fixture required}")
work=$(mktemp -d /tmp/askpass-tests.XXXXXXXX)
trap 'find "$work" -depth -delete' EXIT
mkdir "$work/runtime" "$work/fixtures"
caller_cwd="$work/"$'caller-雪-\n'
mkdir "$caller_cwd"
chmod 700 "$work/runtime"
# These copies are controlled executables, never actual Git or SSH connections.
for name in git ssh ssh-add; do
  cp "$fixture" "$work/fixtures/$name"
done
ASKPASS_TEST_CWD="$caller_cwd" "$fixture" "$bridge" "$work/runtime" "$work/fixtures"
