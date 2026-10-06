#!/bin/bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
bridge=$(realpath "${1:?normal compiled bridge required}")
omarchy_shell=${OMARCHY_TEST_SHELL:-$repo/shell}
command -v quickshell >/dev/null
[[ -d "$omarchy_shell/Commons" && -d "$omarchy_shell/Ui" ]] || {
  echo 'Omarchy Commons and Ui modules required; set OMARCHY_TEST_SHELL.' >&2
  exit 1
}
[[ -n ${WAYLAND_DISPLAY:-} ]] || { echo 'A Wayland desktop is required for real dialog tests.' >&2; exit 1; }
mkdir -p "$repo/build"
work=$(mktemp -d "$repo/build/dialog-tests.XXXXXXXX")
# Unix socket paths are short even if this checkout has a long pathname.
runtime=$(mktemp -d /tmp/askpass-dialog.XXXXXXXX)
trap 'find "$runtime" -depth -delete' EXIT
chmod 700 "$runtime"
cp "$repo/tests/dialog.qml" "$work/shell.qml"
ln -s "$repo/shell/plugins/services/ssh-askpass" "$work/Askpass"
ln -s "$omarchy_shell/Commons" "$work/Commons"
ln -s "$omarchy_shell/Ui" "$work/Ui"
# Preserve the real display connection when moving XDG_RUNTIME_DIR to an isolated
# directory. The dummy askpass server always uses the new private socket path.
if [[ $WAYLAND_DISPLAY != /* ]]; then
  ln -s "${XDG_RUNTIME_DIR:?desktop runtime required}/$WAYLAND_DISPLAY" "$runtime/$WAYLAND_DISPLAY"
fi
printf 'Dummy dialog screenshots and log: %s\n' "$work"
status=0
XDG_RUNTIME_DIR="$runtime" ASKPASS_TEST_BINARY="$bridge" ASKPASS_TEST_OUTPUT="$work" \
  timeout 30s quickshell --no-color --path "$work" >"$work/output.log" 2>&1 || status=$?
cat "$work/output.log"
[[ $status == 0 ]] || exit "$status"
log=$(<"$work/output.log")
[[ $log == *'QML_CHECK_PASS 0'* && $log != *QML_CHECK_FAIL* \
   && $log != *QML_CHECK_TIMEOUT* && $log != *QML_FUNCTION_FAIL* ]]
