#!/usr/bin/env python3
"""Exercise the production monitor CLI against a private simulated Hyprland.

Usage: run-monitor-control.py BINARY
Build BINARY with MONITOR_CONFIRM_SECONDS=2. No live desktop tools are invoked.
"""
import copy
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else None
if not BINARY:
    raise SystemExit(__doc__)

MONITORS = [
    dict(name="eDP-1", description="Internal", width=1280, height=720,
         refreshRate=60.0, scale=1.0, transform=0, x=0, y=0,
         disabled=False, availableModes=["1280x720@60.00Hz"]),
    dict(name="DP-3", description="Xiaomi", width=3440, height=1440,
         refreshRate=60.0, scale=1.0, transform=0, x=-3440, y=0,
         disabled=False, availableModes=["3440x1440@60.00Hz", "1920x1080@60.00Hz"]),
    dict(name="DP-1", description="HP", width=1920, height=1080,
         refreshRate=60.0, scale=1.6, transform=1, x=0, y=-1200,
         disabled=False, availableModes=["1920x1080@60.00Hz", "1280x720@60.00Hz"]),
]

# Only the test double is Python. It executes no system Hyprland command.
FAKE_HYPRCTL = r'''#!/usr/bin/env python3
import json, os, re, sys, time
from pathlib import Path
root = Path(os.environ["MONITOR_TEST_ROOT"])
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(json.dumps(args) + "\n")

def once(name):
    p = root / name
    if not p.exists(): return False
    p.unlink()
    return True

if args == ["-j", "monitors", "all"]:
    print((root / "live.json").read_text())
elif args == ["reload"]:
    if os.environ.get("MONITOR_TEST_RELOAD_DELAY"):
        (root / "slow-reload-started").touch()
        time.sleep(float(os.environ["MONITOR_TEST_RELOAD_DELAY"]))
    if once("fail-reload"):
        raise SystemExit(1)
    original = (root / "original.lua").read_text()
    source = (Path(os.environ["XDG_CONFIG_HOME"]) / "hypr/monitors.lua").read_text()
    if source == original:
        live = json.loads((root / "initial.json").read_text())
    else:
        live = json.loads((root / "live.json").read_text())
        for body in re.findall(r'hl\.monitor\s*\(\s*\{(.*?)\}\s*\)', source, re.S):
            fields = dict(re.findall(r'(\w+)\s*=\s*("(?:\\.|[^"\\])*"|true|false|[-+\d.]+)', body))
            if "output" not in fields: continue
            fields = {k: json.loads(v) for k, v in fields.items()}
            monitor = next((m for m in live if m["name"] == fields["output"]), None)
            if monitor is None: continue
            if fields.get("disabled", False):
                monitor["disabled"] = True
                continue
            mode = re.fullmatch(r'(\d+)x(\d+)@([\d.]+)(?:Hz)?', fields.get("mode", ""))
            if mode:
                monitor.update(width=int(mode[1]), height=int(mode[2]), refreshRate=float(mode[3]))
            position = re.fullmatch(r'(-?\d+)x(-?\d+)', fields.get("position", ""))
            if position: monitor.update(x=int(position[1]), y=int(position[2]))
            monitor.update(disabled=False, scale=fields.get("scale", 1), transform=fields.get("transform", 0))
    (root / "live.json").write_text(json.dumps(live))
    print("ok")
elif args == ["configerrors"]:
    if once("fail-configerrors"): print("invalid monitor rule")
    else: print("")
else:
    print("Unexpected fake hyprctl command", args, file=sys.stderr)
    raise SystemExit(2)
'''

FAKE_OMARCHY_SHELL = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
root = Path(os.environ["MONITOR_TEST_ROOT"])
args = sys.argv[1:]
with (root / "shell-calls.jsonl").open("a") as log:
    log.write(json.dumps(args) + "\n")
if "summon" in args and "omarchy.menu" in args:
    if os.environ.get("MONITOR_TEST_POPUP_FAIL") == "1":
        raise SystemExit(1)
    payload = next(json.loads(a) for a in args if a.startswith("{"))
    answer = os.environ.get("MONITOR_TEST_POPUP_ANSWER")
    if answer is not None:
        if answer:
            Path(payload["selectionFile"]).write_text(answer + "\n")
        Path(payload["doneFile"]).touch()
    print("ok")
elif "hide" in args and any(plugin in args for plugin in ("omarchy.menu", "omarchy.monitor")):
    pass
else:
    print("Unexpected fake omarchy-shell command", args, file=sys.stderr)
    raise SystemExit(2)
'''

POSITION = ("DP-1", "position", "left", "DP-3", "center")


class MonitorControlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="omarchy-monitor-test-")
        self.root = Path(self.temp.name)
        self.config = self.root / "config/hypr/monitors.lua"
        self.config.parent.mkdir(parents=True)
        self.state = self.root / "state/omarchy/monitor-control"
        self.state.mkdir(parents=True)
        self.original = "-- custom user configuration: preserve exactly\n\nhl.monitor({ output = \"\", mode = \"preferred\", position = \"auto\", scale = 1 })\n"
        self.config.write_text(self.original)
        (self.root / "original.lua").write_text(self.original)
        for name in ("initial.json", "live.json"):
            (self.root / name).write_text(json.dumps(MONITORS))
        bindir = self.root / "bin"
        bindir.mkdir()
        (bindir / "hyprctl").write_text(FAKE_HYPRCTL)
        (bindir / "omarchy-shell").write_text(FAKE_OMARCHY_SHELL)
        (self.root / "shell-calls.jsonl").write_text("")
        (bindir / "omarchy-notification-send").write_text("#!/bin/sh\nexit 0\n")
        for path in bindir.iterdir():
            path.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.root),
                        XDG_CONFIG_HOME=str(self.root / "config"),
                        XDG_STATE_HOME=str(self.root / "state"),
                        MONITOR_TEST_ROOT=str(self.root),
                        PATH=str(bindir) + os.pathsep + os.environ.get("PATH", ""))
        self.children = []
        preview = self.cli("preview", *POSITION)
        self.before_state = copy.deepcopy(preview["state"])
        self.before_state["relations"] = []
        for m in MONITORS:
            self.before_state["outputs"]["desc:" + m["description"]].update(m)
        self.layout.write_text(json.dumps(self.before_state))
        (self.root / "calls").write_text("")

    @property
    def layout(self):
        return self.state / "layout.json"

    def tearDown(self):
        for process in self.children:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        pending = self.state / "pending.json"
        if pending.exists():
            try:
                token = json.loads(pending.read_text())["token"]
                self.cli("revert", token, success=None)
                self.until(lambda: not pending.exists(), timeout=5)
            except (AssertionError, KeyError, ValueError):
                pass
        for pid in self.owned_processes():
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        self.temp.cleanup()

    def owned_processes(self):
        """Find only this fixture's production processes, including watchdogs."""
        found = []
        marker = ("MONITOR_TEST_ROOT=" + str(self.root)).encode()
        for proc in Path("/proc").iterdir():
            if not proc.name.isdigit():
                continue
            try:
                environ = (proc / "environ").read_bytes().split(b"\0")
                exe = (proc / "exe").resolve()
                if marker in environ and str(exe) == BINARY:
                    found.append(int(proc.name))
            except (OSError, RuntimeError):
                pass
        return found

    def cli(self, *args, success=True):
        p = subprocess.run([BINARY, *args], env=self.env, capture_output=True,
                           text=True, timeout=12)
        if success is not None:
            self.assertEqual(p.returncode == 0, success,
                             f"{args}: return={p.returncode}, stdout={p.stdout}, stderr={p.stderr}")
        try:
            return json.loads(p.stdout)
        except ValueError:
            self.fail(f"{args}: invalid JSON: {p.stdout!r}; stderr={p.stderr!r}")

    def until(self, predicate, timeout=7):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.04)
        self.fail("Timed out waiting for transaction completion")

    def apply(self):
        response = self.cli("apply", *POSITION)
        token = response["pending"]["token"]
        self.assertTrue(token)
        self.assertEqual(response["pending"]["phase"], "confirming")
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertEqual(json.loads(self.layout.read_text()), self.before_state)
        return token

    def completed(self):
        self.until(lambda: not (self.state / "pending.json").exists())

    def assert_restored(self):
        self.completed()
        self.assertEqual(self.config.read_bytes(), self.original.encode())
        self.assertEqual(json.loads(self.layout.read_text()), self.before_state)
        self.assertEqual(json.loads((self.root / "live.json").read_text()), MONITORS)

    def shell_calls(self):
        return [json.loads(line) for line in (self.root / "shell-calls.jsonl").read_text().splitlines()]

    def popup(self, token):
        def summons():
            return [args for args in self.shell_calls() if "summon" in args and "omarchy.menu" in args]
        self.until(lambda: bool(summons()), timeout=1)
        args = summons()[-1]
        payloads = [json.loads(arg) for arg in args if arg.startswith("{")]
        self.assertEqual(len(payloads), 1)
        payload = payloads[0]
        self.assertEqual(payload["mode"], "select")
        self.assertGreaterEqual(payload["width"], 640)
        self.assertEqual(payload["options"], ["Ripristina", "Mantieni"])
        for key in ("selectionFile", "doneFile"):
            path = Path(payload[key])
            self.assertTrue(path.is_absolute())
            self.assertEqual(path.parent, self.state)
            self.assertIn(token, path.name)
        return payload

    def assert_popup_cleaned(self, payload):
        for key in ("selectionFile", "doneFile"):
            self.assertFalse(Path(payload[key]).exists())

    def popup_was_hidden(self):
        return any("hide" in args and "omarchy.menu" in args for args in self.shell_calls())

    def test_killed_client_during_reload_preserves_popup_and_watchdog(self):
        self.env["MONITOR_TEST_RELOAD_DELAY"] = "0.3"
        process = subprocess.Popen([BINARY, "apply", *POSITION], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.children.append(process)
        self.until(lambda: (self.root / "slow-reload-started").exists(), timeout=3)
        self.assertNotEqual(self.config.read_text(), self.original)
        token = json.loads((self.state / "pending.json").read_text())["token"]
        process.kill()
        process.stdout.close()
        self.assertEqual(process.wait(timeout=2), -signal.SIGKILL)
        payload = self.popup(token)
        self.assert_restored()
        self.until(self.popup_was_hidden)
        self.assert_popup_cleaned(payload)

    def test_popup_keep_confirms_transaction(self):
        token = self.apply()
        payload = self.popup(token)
        Path(payload["selectionFile"]).write_text("Mantieni\n")
        Path(payload["doneFile"]).touch()
        self.completed()
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [
            dict(monitor="desc:HP", reference="desc:Xiaomi", direction="left", alignment="center")])
        self.assert_popup_cleaned(payload)
        self.assertFalse(self.popup_was_hidden(), "The answered menu already dismisses itself")

    def test_popup_escape_restores_transaction(self):
        token = self.apply()
        payload = self.popup(token)
        Path(payload["doneFile"]).touch()
        self.assert_restored()
        self.assert_popup_cleaned(payload)
        self.assertFalse(self.popup_was_hidden(), "The dismissed menu must not hide a newer menu")

    def test_popup_failure_restores_transaction(self):
        self.env["MONITOR_TEST_POPUP_FAIL"] = "1"
        self.cli("apply", *POSITION, success=None)
        self.until(lambda: any("summon" in args for args in self.shell_calls()), timeout=1)
        self.assert_restored()
        self.assertFalse(list(self.state.glob("*selection*")))
        self.assertFalse(list(self.state.glob("*done*")))

    def test_popup_timeout_hides_menu_and_restores_transaction(self):
        payload = self.popup(self.apply())
        self.assert_restored()
        self.until(self.popup_was_hidden)
        self.assert_popup_cleaned(payload)

    def test_popup_cli_confirmation_hides_menu(self):
        token = self.apply()
        payload = self.popup(token)
        self.cli("confirm", token)
        self.completed()
        self.until(self.popup_was_hidden)
        self.assert_popup_cleaned(payload)
        self.assertNotEqual(self.config.read_text(), self.original)

    def test_preview_does_not_change_configuration_or_durable_state(self):
        config = self.config.read_bytes()
        state = self.layout.read_bytes()
        preview = self.cli("preview", *POSITION)
        hp = next(m for m in preview["monitors"] if m["name"] == "DP-1")
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertFalse((self.state / "pending.json").exists())
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertNotIn(["reload"], calls)

    def test_apply_confirm_commits_geometry_and_relation(self):
        token = self.apply()
        status = self.cli("status")
        self.assertEqual(status["pending"]["token"], token)
        self.assertEqual(status["relations"], [])
        self.cli("confirm", token)
        self.completed()
        state = json.loads(self.layout.read_text())
        self.assertEqual(state["relations"], [dict(monitor="desc:HP", reference="desc:Xiaomi",
                                                   direction="left", alignment="center")])
        hp = state["outputs"]["desc:HP"]
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertIsNone(self.cli("status")["pending"])

    def test_timeout_restores_original_configuration_and_state(self):
        self.apply()
        self.assert_restored()

    def test_explicit_revert_restores_original_configuration_and_state(self):
        self.cli("revert", self.apply())
        self.assert_restored()

    def test_apply_waits_for_daemon_lock_without_pending_transaction(self):
        with (self.state / "lock").open("a+") as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            release = threading.Timer(0.3, fcntl.flock, args=(lock.fileno(), fcntl.LOCK_UN))
            release.start()
            started = time.monotonic()
            try:
                token = self.apply()
                self.assertGreaterEqual(time.monotonic() - started, 0.25)
            finally:
                release.join()
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
        self.cli("revert", token)
        self.assert_restored()

    def test_concurrent_apply_is_rejected(self):
        token = self.apply()
        configured = self.config.read_bytes()
        second = self.cli("apply", "DP-1", "rotation", "2", success=False)
        self.assertIn("error", second)
        self.assertEqual(self.config.read_bytes(), configured)
        self.assertEqual(self.cli("status")["pending"]["token"], token)
        self.cli("revert", token)
        self.assert_restored()

    def test_closing_client_stdout_preserves_timeout_watchdog(self):
        process = subprocess.Popen([BINARY, "apply", *POSITION], env=self.env,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.children.append(process)
        process.stdout.close()
        process.wait(timeout=10)
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assert_restored()

    def test_reload_failure_rolls_back(self):
        (self.root / "fail-reload").touch()
        self.assertIn("error", self.cli("apply", *POSITION, success=False))
        self.assert_restored()
        self.assertTrue(self.cli("status")["error"])

    def test_configerrors_failure_rolls_back(self):
        (self.root / "fail-configerrors").touch()
        self.assertIn("error", self.cli("apply", *POSITION, success=False))
        self.assert_restored()

    def test_unsupported_mode_does_not_change_configuration(self):
        state = self.layout.read_bytes()
        self.assertIn("error", self.cli("apply", "DP-1", "mode", "9999x9999@60", success=False))
        self.assertEqual(self.config.read_text(), self.original)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertFalse((self.state / "pending.json").exists())

    def test_stale_confirmation_token_is_rejected(self):
        token = self.apply()
        self.assertIn("error", self.cli("confirm", "stale-token", success=False))
        self.assertFalse((self.state / "response.json").exists())
        self.cli("revert", token)
        self.assert_restored()
        self.assertIn("error", self.cli("confirm", token, success=False))

    def live(self):
        return json.loads((self.root / "live.json").read_text())

    def set_live(self, monitors):
        temporary = self.root / "live.next.json"
        temporary.write_text(json.dumps(monitors))
        temporary.replace(self.root / "live.json")

    def wait_monitor(self, name, **expected):
        def matches():
            actual = next((m for m in self.live() if m["name"] == name), None)
            return actual and all(actual.get(k) == v for k, v in expected.items())
        try:
            self.until(matches, timeout=9)
        except AssertionError as error:
            self.fail(f"{error}; expected {name} {expected}; live={self.live()}; state={self.layout.read_text()}")

    def confirm_position(self):
        self.cli("confirm", self.apply())
        self.completed()

    def start_daemon(self):
        (self.root / "calls").write_text("")
        process = subprocess.Popen([BINARY, "daemon"], env=self.env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(process)
        def initialized():
            calls = (self.root / "calls").read_text().splitlines()
            return sum(line == '["-j", "monitors", "all"]' for line in calls) >= 3
        self.until(initialized)
        self.assertIsNone(process.poll())
        return process

    def daemon_apply(self, *args):
        deadline = time.monotonic() + 5
        while True:
            result = self.cli("apply", *args, success=None)
            if "pending" in result:
                return result["pending"]["token"]
            self.assertIn("Un'altra modifica", result.get("error", ""))
            if time.monotonic() >= deadline:
                self.fail("Daemon did not release the configuration lock")
            time.sleep(0.1)

    def test_daemon_hotplug_compacts_then_restores_saved_relation(self):
        self.confirm_position()
        config = self.config.read_bytes()
        original_live = self.live()
        self.start_daemon()
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.live(), original_live)
        self.set_live([m for m in self.live() if m["name"] != "DP-3"])
        self.wait_monitor("DP-1", x=1280, y=-240)
        fallback = self.live()
        self.assertEqual(next(m for m in fallback if m["name"] == "eDP-1")["x"], 0)
        self.assertEqual(len(json.loads(self.layout.read_text())["relations"]), 1)
        xiaomi = next(m for m in original_live if m["name"] == "DP-3")
        self.set_live(fallback + [xiaomi])
        self.wait_monitor("DP-1", x=-4115, y=120)
        self.wait_monitor("DP-3", x=-3440, y=0)
        self.assertEqual(len(json.loads(self.layout.read_text())["relations"]), 1)

    def test_daemon_recalculates_relation_after_external_scale_change(self):
        self.confirm_position()
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-3")["scale"] = 2.0
        self.set_live(changed)
        self.wait_monitor("DP-1", x=-4115, y=-240)
        self.wait_monitor("DP-3", scale=2.0)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["desc:Xiaomi"]["scale"] == 2.0)
        self.assertEqual(len(json.loads(self.layout.read_text())["relations"]), 1)

    def test_daemon_disable_enable_retains_saved_relation(self):
        self.confirm_position()
        self.start_daemon()
        token = self.daemon_apply("DP-3", "disable")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-3", disabled=True)
        self.wait_monitor("DP-1", x=1280, y=-240)
        token = self.daemon_apply("DP-3", "enable")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-3", disabled=False, x=-3440, y=0)
        self.wait_monitor("DP-1", x=-4115, y=120)
        self.assertEqual(len(json.loads(self.layout.read_text())["relations"]), 1)

    def test_daemon_handles_hotplug_during_pending_confirmation(self):
        self.confirm_position()
        self.start_daemon()
        token = self.daemon_apply("DP-1", "rotation", "2")
        self.set_live([m for m in self.live() if m["name"] != "DP-3"])
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-1", x=1280, y=22)
        self.assertEqual(len(json.loads(self.layout.read_text())["relations"]), 1)

    def test_daemon_recovers_transaction_after_watchdog_is_killed(self):
        self.apply()
        watchdogs = self.owned_processes()
        self.assertEqual(len(watchdogs), 1, f"Expected isolated watchdog, found {watchdogs}")
        os.kill(watchdogs[0], signal.SIGKILL)
        process = subprocess.Popen([BINARY, "daemon"], env=self.env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(process)
        self.assert_restored()


if __name__ == "__main__":
    unittest.main(verbosity=2)
