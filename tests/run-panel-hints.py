#!/usr/bin/env python3
"""Exercise shortcuts against changing QML delegates, without desktop actions."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="omarchy-panel-hints-") as directory:
    config = Path(directory)
    (config / "shell.qml").write_text((repo / "tests/panel-hints.qml").read_text())
    for name in ("Commons", "Ui"):
        (config / name).symlink_to((repo / "shell") / name)
    for name in ("PanelHints.qml", "PanelHintModel.js"):
        (config / name).symlink_to(repo / "shell/Ui" / name)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    result = subprocess.run(
        ["quickshell", "-p", str(config), "--no-color"],
        env=env, capture_output=True, text=True, timeout=15,
    )
    output = result.stdout + result.stderr
    print(output, end="")
    if result.returncode or "HINT_TEST_PASSED" not in output or "FAIL" in output:
        raise SystemExit(1)
