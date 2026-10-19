#!/usr/bin/env python3
"""Exercise the production monitor CLI against a private simulated Hyprland.

Usage: run-monitor-control.py BINARY
Build BINARY with MONITOR_CONFIRM_SECONDS=2 and MONITOR_RECHECK_MS=5000.
No live desktop tools are invoked.
"""
import copy
import fcntl
import json
import os
from pathlib import Path
import signal
import select
import socket
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

for monitor in MONITORS:
    monitor["model"] = monitor["description"]

# Only the test double is Python. It executes no system Hyprland command.
FAKE_HYPRCTL = r'''#!/usr/bin/env python3
import json, os, re, socket, sys, time
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
    if once("fail-inventory"):
        raise SystemExit(1)
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
            output = fields["output"]
            if output.startswith("desc:"):
                selector = output[5:].strip()
                matched = [m for m in live if m["description"].startswith(selector) or
                           " ".join(m.get(k, "") for k in ("make", "model", "serial")).strip().replace(",", "").startswith(selector)]
            else:
                matched = [m for m in live if m["name"] == output]
            for monitor in matched:
                if fields.get("disabled", False):
                    monitor.update(disabled=True, width=0, height=0)
                    continue
                requested = fields.get("mode", "preferred")
                if requested == "preferred":
                    requested = monitor["availableModes"][0]
                mode = re.fullmatch(r'(\d+)x(\d+)@([\d.]+)(?:Hz)?', requested)
                if mode:
                    monitor.update(width=int(mode[1]), height=int(mode[2]), refreshRate=float(mode[3]))
                position = re.fullmatch(r'(-?\d+)x(-?\d+)', fields.get("position", ""))
                if position: monitor.update(x=int(position[1]), y=int(position[2]))
                monitor.update(disabled=False, scale=fields.get("scale", 1), transform=fields.get("transform", 0))
    (root / "live.json").write_text(json.dumps(live))
    if not os.environ.get("MONITOR_TEST_NO_RELOAD_EVENT"):
        try:
            with socket.socket(socket.AF_UNIX) as events:
                events.connect(str(Path(os.environ["XDG_RUNTIME_DIR"]) / "hypr/test/.socket2.sock"))
                events.sendall(b"configreloaded>>\n")
        except OSError:
            pass
    print("ok")
elif args == ["configerrors"]:
    if once("block-layout-save"):
        # Block atomic replacement after installation, even when running as root.
        layout = Path(os.environ["XDG_STATE_HOME"]) / "omarchy/monitor-control/layout.json"
        layout.rename(root / "layout-before-obstruction.json")
        layout.mkdir()
    if once("fail-configerrors-and-restore-reload"):
        (root / "fail-reload").touch()
        print("invalid monitor rule")
    elif once("fail-configerrors"): print("invalid monitor rule")
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


class EventSocket:
    """Private socket2 double. Short-lived fake hyprctl clients relay reloads."""
    def __init__(self, path):
        self.path = path
        path.parent.mkdir(parents=True, exist_ok=True)
        self.server = socket.socket(socket.AF_UNIX)
        self.server.bind(str(path))
        self.server.listen()
        self.clients = []
        self.reject_clients = False
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopped.is_set():
            with self.lock:
                readers = [self.server, *self.clients]
            try:
                ready, _, _ = select.select(readers, [], [], 0.05)
            except (OSError, ValueError):
                continue
            for reader in ready:
                if reader is self.server:
                    client, _ = self.server.accept()
                    if self.reject_clients:
                        client.close()
                    else:
                        with self.lock:
                            self.clients.append(client)
                else:
                    try:
                        data = reader.recv(4096)
                    except OSError:
                        data = b""
                    if data:
                        self.send(data)
                    else:
                        with self.lock:
                            if reader in self.clients:
                                self.clients.remove(reader)
                        reader.close()

    def send(self, data):
        with self.lock:
            for client in self.clients:
                try:
                    client.sendall(data)
                except OSError:
                    pass

    def disconnect(self):
        with self.lock:
            for client in self.clients:
                client.shutdown(socket.SHUT_RDWR)
                client.close()
            self.clients.clear()

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=2)
        self.disconnect()
        self.server.close()
        self.path.unlink(missing_ok=True)


class ScaleOptionsTests(unittest.TestCase):
    def test_read_only_without_session_or_home(self):
        with tempfile.TemporaryDirectory(prefix="omarchy-scale-options-") as temp:
            root = Path(temp)
            config = root / "config/hypr/monitors.lua"
            config.parent.mkdir(parents=True)
            config.write_text("-- must not be changed\n")
            original = config.read_bytes()
            before = sorted(root.rglob("*"))
            environments = [
                {"PATH": str(root / "no-tools")},
                {"PATH": str(root / "no-tools"), "HOME": str(root / "home")},
                {"PATH": str(root / "no-tools"), "HOME": str(root / "home"),
                 "XDG_CONFIG_HOME": str(root / "config"),
                 "XDG_STATE_HOME": str(root / "state"),
                 "XDG_RUNTIME_DIR": str(root / "runtime")},
            ]
            cases = [
                (3840, 2160, [1, 1.25, 1.5, 1.6, 2, 2.5, 3, 4]),
                (6016, 3384, [1, 1.6, 2, 4]),
                (3440, 1440, [1, 1.25, 1.6, 2, 2.5, 4]),
                (1280, 800, [1, 1.25, 1.6, 2, 2.5, 4]),
                (1, 65536, [1]),
                (65536, 65536, [1, 1.6, 2, 4]),
            ]
            for env in environments:
                for width, height, expected in cases:
                    with self.subTest(env=env, width=width, height=height):
                        result = subprocess.run(
                            [BINARY, "scale-options", str(width), str(height)],
                            env=env, cwd=root, capture_output=True, text=True, timeout=5)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(json.loads(result.stdout), expected)
                        self.assertEqual(result.stderr, "")
                        self.assertEqual(sorted(root.rglob("*")), before)
                        self.assertEqual(config.read_bytes(), original)

    def test_invalid_dimensions_and_arity_are_read_only(self):
        invalid = ["", "0", "-1", "+1", "1.0", "1e3", "0x100", " 1", "1 ",
                   "1\n", "nan", "inf", "65537", "2147483648", "9" * 100, "12px"]
        cases = [[], ["3840"], ["3840", "2160", "extra"]]
        for value in invalid:
            cases.extend([[value, "2160"], ["3840", value]])
        with tempfile.TemporaryDirectory(prefix="omarchy-scale-options-") as temp:
            root = Path(temp)
            for home in (None, str(root / "home")):
                env = {"PATH": str(root / "no-tools"),
                       "XDG_CONFIG_HOME": str(root / "config"),
                       "XDG_STATE_HOME": str(root / "state")}
                if home is not None:
                    env["HOME"] = home
                for args in cases:
                    with self.subTest(home=home, args=args):
                        result = subprocess.run([BINARY, "scale-options", *args],
                                                env=env, cwd=root, capture_output=True,
                                                text=True, timeout=5)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("scale-options WIDTH HEIGHT", json.loads(result.stdout)["error"])
                        self.assertEqual(list(root.iterdir()), [])


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
        self.events = EventSocket(self.root / "runtime/hypr/test/.socket2.sock")
        self.env = dict(os.environ, HOME=str(self.root),
                        XDG_RUNTIME_DIR=str(self.root / "runtime"),
                        HYPRLAND_INSTANCE_SIGNATURE="test",
                        XDG_CONFIG_HOME=str(self.root / "config"),
                        XDG_STATE_HOME=str(self.root / "state"),
                        MONITOR_TEST_ROOT=str(self.root),
                        PATH=str(bindir) + os.pathsep + os.environ.get("PATH", ""))
        self.children = []
        preview = self.cli("preview", *POSITION)
        self.before_state = copy.deepcopy(preview["state"])
        self.before_state["relations"] = []
        for m in MONITORS:
            self.before_state["outputs"]["model:" + m["model"]].update(m)
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
        self.events.close()
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
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [])
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

    def test_stale_scale_preset_rejects_before_transaction(self):
        self.assertIn(1.5, self.cli("scale-options", "3840", "2160"))
        changed = self.live()
        next(m for m in changed if m["name"] == "eDP-1").update(
            width=1280, height=800, availableModes=["1280x800@60.00Hz"])
        self.set_live(changed, notify=False)
        config = self.config.read_bytes()
        state_files = {p.name: p.read_bytes() for p in self.state.iterdir()}
        response = self.cli("apply", "eDP-1", "scale-preset", "1.5", success=False)
        self.assertIn("not supported", response["error"])
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual({p.name: p.read_bytes() for p in self.state.iterdir()}, state_files)
        self.assertEqual(self.live(), changed)
        self.assertEqual(self.shell_calls(), [])
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertEqual(calls, [["-j", "monitors", "all"]])
        custom = self.cli("preview", "eDP-1", "scale", "1.5")
        self.assertEqual(next(m for m in custom["monitors"] if m["name"] == "eDP-1")["scale"], 1.6)

    def apply_scale_preset(self):
        response = self.cli("apply", "DP-3", "scale-preset", "1.6")
        pending = response["pending"]
        self.assertEqual(pending["phase"], "confirming")
        self.assertIn("Scala", pending["summary"])
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertEqual(json.loads(self.layout.read_text()), self.before_state)
        self.assertEqual(next(m for m in self.live() if m["name"] == "DP-3")["scale"], 1.6)
        self.assertTrue((self.state / "before.lua").exists())
        return pending["token"]

    def test_scale_preset_manual_revert_restores_configuration_and_state(self):
        token = self.apply_scale_preset()
        payload = self.popup(token)
        self.cli("revert", token)
        self.assert_restored()
        self.until(self.popup_was_hidden)
        self.assert_popup_cleaned(payload)

    def test_scale_preset_manual_confirm_commits_configuration_and_state(self):
        token = self.apply_scale_preset()
        payload = self.popup(token)
        self.cli("confirm", token)
        self.completed()
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertEqual(json.loads(self.layout.read_text())["outputs"]["model:Xiaomi"]["scale"], 1.6)
        self.until(self.popup_was_hidden)
        self.assert_popup_cleaned(payload)

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

    def test_apply_confirm_commits_geometry_without_dependency(self):
        token = self.apply()
        status = self.cli("status")
        self.assertEqual(status["pending"]["token"], token)
        self.assertEqual(status["relations"], [])
        self.cli("confirm", token)
        self.completed()
        state = json.loads(self.layout.read_text())
        self.assertEqual(state["relations"], [])
        hp = state["outputs"]["model:HP"]
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertIsNone(self.cli("status")["pending"])

    def test_confirmed_configuration_follows_model_not_connector_or_serial(self):
        self.confirm_position()
        self.assertIn('output = "DP-1"', self.config.read_text())
        self.start_daemon()
        renamed = self.live()
        next(m for m in renamed if m["model"] == "HP").update(
            name="DP-9", description="HP different serial", serial="new-serial",
            width=0, height=0, disabled=True, scale=1, transform=0, x=1280, y=0)
        self.set_live(renamed)
        self.wait_monitor("DP-9", transform=1, x=-4115, y=120)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["name"] == "DP-9")
        actual = next(m for m in self.cli("status")["monitors"] if m["model"] == "HP")
        self.assertIsNone(actual["relation"])
        self.assertEqual(actual["id"], "model:HP")
        self.assertIn('output = "DP-9"', self.config.read_text())
        self.assertNotIn('output = "DP-1"', self.config.read_text())

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

    def test_replacement_on_same_connector_during_apply_is_not_confirmed(self):
        self.env["MONITOR_TEST_RELOAD_DELAY"] = "0.3"
        process = subprocess.Popen([BINARY, "apply", "DP-1", "mode", "1920x1080@60.00"],
                                   env=self.env, stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL)
        self.children.append(process)
        self.until(lambda: (self.root / "slow-reload-started").exists(), timeout=3)
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1").update(model="Different display", description="Different display")
        self.set_live(changed)
        output, _ = process.communicate(timeout=10)
        result = json.loads(output)
        self.assertIn("error", result)
        self.assertNotEqual(process.returncode, 0)
        self.assert_restored()
        self.assertFalse(any("summon" in args for args in self.shell_calls()))

    def test_manual_partial_backup_failure_preserves_configuration_and_artifacts(self):
        state = self.layout.read_bytes()
        blocked = self.state / "before.json"
        blocked.mkdir()
        (blocked / "keep").write_text("existing backup obstruction\n")
        (self.state / "before.lua").write_text("stale manual backup\n")
        automatic = self.state / "automatic.before.lua"
        automatic.write_text("existing automatic backup\n")
        (self.state / "error.json").write_text(json.dumps({"message": "old error"}))
        (self.state / "response.json").write_text(json.dumps({"token": "old-token"}))

        result = self.cli("apply", *POSITION, success=False)

        self.assertEqual(result["error"], "Impossibile salvare il backup monitor")
        self.assertEqual(self.config.read_text(), self.original)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), MONITORS)
        self.assertEqual((self.state / "before.lua").read_text(), self.original)
        self.assertEqual((blocked / "keep").read_text(), "existing backup obstruction\n")
        self.assertEqual(automatic.read_text(), "existing automatic backup\n")
        for name in ("next.json", "pending.json", "response.json", "error.json"):
            self.assertFalse((self.state / name).exists(), name)
        self.assertFalse(list(self.state.glob("*.tmp.*")))
        self.assertEqual(self.shell_calls(), [])
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertNotIn(["reload"], calls)
        status = self.cli("status")
        self.assertIsNone(status["pending"])
        self.assertIsNone(status["error"], "Backup preparation failure is not published to status")

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

    def set_live(self, monitors, notify=True):
        before = self.live()
        temporary = self.root / "live.next.json"
        temporary.write_text(json.dumps(monitors))
        temporary.replace(self.root / "live.json")
        if notify:
            old = {m["name"] for m in before}
            new = {m["name"] for m in monitors}
            for name in old - new:
                self.events.send(f"monitorremoved>>{name}\n".encode())
            for name in new - old:
                self.events.send(f"monitoraddedv2>>1,{name},Test monitor\n".encode())
            self.events.send(b"configreloaded>>\n")

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
        self.wait_daemon_idle()
        return process

    def inventory_calls(self):
        return (self.root / "calls").read_text().count('["-j", "monitors", "all"]')

    def wait_daemon_idle(self):
        last = self.inventory_calls()
        quiet_since = time.monotonic()
        deadline = quiet_since + 7
        while time.monotonic() < deadline:
            current = self.inventory_calls()
            if current != last:
                last, quiet_since = current, time.monotonic()
            if time.monotonic() - quiet_since >= 0.4:
                with (self.state / "lock").open("a+") as lock:
                    try:
                        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                        return
                    except BlockingIOError:
                        pass
            time.sleep(0.04)
        self.fail("Daemon did not become idle")

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

    def test_automatic_backup_failure_still_reloads_original_configuration(self):
        state = self.layout.read_bytes()
        blocked = self.state / "automatic.before.lua"
        blocked.mkdir()
        (blocked / "keep").write_text("existing backup obstruction\n")
        (self.state / "error.json").write_text(json.dumps({"message": "old error"}))

        daemon = self.start_daemon()
        self.until(lambda: self.cli("status")["error"] == "")
        daemon.terminate()
        self.assertEqual(daemon.wait(timeout=8), 0)

        self.assertEqual(self.config.read_text(), self.original)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), MONITORS)
        self.assertEqual((blocked / "keep").read_text(), "existing backup obstruction\n")
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertEqual(calls.count(["reload"]), 1, "Restoration reload is attempted even without a backup")
        self.assertIn(["configerrors"], calls)
        self.assertFalse(list(self.state.glob("*.tmp.*")))
        for name in ("before.lua", "before.json", "next.json", "pending.json"):
            self.assertFalse((self.state / name).exists(), name)
        self.assertEqual(self.shell_calls(), [])
        status = self.cli("status")
        self.assertIsNone(status["pending"])
        self.assertEqual(status["error"], "", "Automatic backup failure publishes the existing empty diagnostic")

    def test_automatic_final_state_save_failure_keeps_installed_configuration(self):
        state = self.layout.read_bytes()
        (self.root / "block-layout-save").touch()

        daemon = self.start_daemon()
        self.until(lambda: (self.state / "error.json").exists())
        daemon.terminate()
        self.assertEqual(daemon.wait(timeout=8), 0)

        self.assertFalse((self.root / "block-layout-save").exists())
        self.assertTrue(self.layout.is_dir())
        self.assertEqual(list(self.layout.iterdir()), [])
        self.assertEqual((self.root / "layout-before-obstruction.json").read_bytes(), state)
        self.assertNotEqual(self.config.read_text(), self.original)
        self.assertIn('output = "DP-1"', self.config.read_text())
        self.assertEqual((self.state / "automatic.before.lua").read_text(), self.original)
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertEqual(calls.count(["reload"]), 1, "A final state save failure must not restore Lua")
        self.assertIn(["configerrors"], calls)
        self.assertFalse(list(self.state.glob("*.tmp.*")))
        self.assertFalse((self.state / "pending.json").exists())
        self.assertFalse((self.state / "next.json").exists())
        self.assertEqual(self.shell_calls(), [])
        self.assertEqual(json.loads((self.state / "error.json").read_text()), {"message": ""})
        status = self.cli("status", success=False)
        self.assertEqual(status["error"], "Lo stato salvato dei monitor non è valido; configurazione non modificata")

    def test_automatic_restore_failure_preserves_original_diagnostic(self):
        state = self.layout.read_bytes()
        (self.root / "fail-configerrors-and-restore-reload").touch()

        daemon = self.start_daemon()
        self.until(lambda: (self.state / "error.json").exists())
        daemon.terminate()
        self.assertEqual(daemon.wait(timeout=8), 0)

        self.assertFalse((self.root / "fail-configerrors-and-restore-reload").exists())
        self.assertFalse((self.root / "fail-reload").exists(), "The restoration reload must consume its failure")
        self.assertEqual(self.config.read_text(), self.original)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual((self.state / "automatic.before.lua").read_text(), self.original)
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertEqual(calls.count(["reload"]), 2)
        self.assertEqual(calls.count(["configerrors"]), 1)
        self.assertFalse(list(self.state.glob("*.tmp.*")))
        self.assertFalse((self.state / "pending.json").exists())
        self.assertFalse((self.state / "next.json").exists())
        self.assertEqual(self.shell_calls(), [])
        status = self.cli("status")
        self.assertIsNone(status["pending"])
        self.assertEqual(status["error"], "Hyprland ha rifiutato la configurazione: invalid monitor rule")
        self.assertEqual(json.loads((self.state / "error.json").read_text())["message"], status["error"])

    def test_daemon_restores_saved_mode_after_temporary_mode_unavailability(self):
        self.confirm_position()
        self.start_daemon()
        changed = self.live()
        hp = next(m for m in changed if m["name"] == "DP-1")
        hp.update(disabled=True, width=0, height=0,
                  availableModes=["1280x720@60.00Hz"])
        self.set_live(changed)
        self.wait_monitor("DP-1", disabled=False, width=1280, height=720)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"].get("modeFallback", False))
        saved = json.loads(self.layout.read_text())["outputs"]["model:HP"]
        self.assertEqual(saved["mode"], "1920x1080@60.00")
        self.assertEqual((saved["width"], saved["height"]), (1920, 1080))
        self.assertTrue(saved["modeFallback"])
        # Only EDID modes change here: the daemon must notice without a hotplug.
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["availableModes"] = [
            "1920x1080@60.00Hz", "1280x720@60.00Hz"]
        self.set_live(changed)
        self.wait_monitor("DP-1", width=1920, height=1080)
        self.until(lambda: not json.loads(self.layout.read_text())["outputs"]["model:HP"].get("modeFallback", False))

    def test_quoted_description_applies_and_rolls_back(self):
        live = self.live()
        hp = next(m for m in live if m["name"] == "DP-1")
        hp["model"] = hp["description"] = 'ACME "wide" \\ panel'
        self.set_live(live)
        token = self.cli("apply", "DP-1", "rotation", "2")["pending"]["token"]
        actual = next(m for m in self.live() if m["name"] == "DP-1")
        self.assertEqual(actual["transform"], 2)
        self.assertIn('output = "DP-1"', self.config.read_text())
        proposed = json.loads((self.state / "next.json").read_text())
        self.assertEqual(proposed["outputs"]["model:" + hp["model"]]["model"], hp["model"])
        self.cli("revert", token)
        self.assert_restored()

    def test_malformed_disconnected_profile_returns_error_without_writes(self):
        state = json.loads(self.layout.read_text())
        state["outputs"]["desc:Disconnected"] = 17
        self.layout.write_text(json.dumps(state))
        original_state = self.layout.read_bytes()
        result = self.cli("apply", "DP-1", "mode", "1920x1080@60.00", success=False)
        self.assertIn("error", result)
        self.assertEqual(self.config.read_text(), self.original)
        self.assertEqual(self.layout.read_bytes(), original_state)
        self.assertFalse((self.state / "pending.json").exists())
        calls = [json.loads(s) for s in (self.root / "calls").read_text().splitlines()]
        self.assertNotIn(["reload"], calls)

    def test_prefix_descriptions_do_not_disable_the_other_monitor(self):
        live = self.live()
        next(m for m in live if m["name"] == "DP-1")["description"] = "ACME Panel"
        next(m for m in live if m["name"] == "DP-3")["description"] = "ACME Panel 2"
        self.set_live(live)
        self.layout.unlink()
        self.config.write_text(self.original +
                               '\n-- BEGIN monitor-layout selector (managed automatically)\n'
                               'hl.monitor({ output = "DP-3" })\n'
                               'hl.monitor({ output = "DP-1" })\n'
                               '-- END monitor-layout selector\n')
        token = self.cli("apply", "DP-1", "disable")["pending"]["token"]
        self.assertFalse(next(m for m in self.live() if m["name"] == "DP-3")["disabled"])
        self.assertNotIn('output = "desc:ACME Panel"', self.config.read_text())
        self.cli("confirm", token)
        self.completed()
        self.assertTrue(next(m for m in self.live() if m["name"] == "DP-1")["disabled"])
        self.assertFalse(next(m for m in self.live() if m["name"] == "DP-3")["disabled"])

    def test_daemon_remembers_external_move_without_moving_another_monitor(self):
        self.confirm_position()
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1").update(x=-5000, y=500)
        self.set_live(changed)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -5000)
        saved = json.loads(self.layout.read_text())["outputs"]["model:HP"]
        self.assertEqual((saved["x"], saved["y"]), (-5000, 500))
        self.assertEqual(next(m for m in self.live() if m["name"] == "DP-3")["x"], -3440)
        self.set_live([m for m in self.live() if m["name"] != "DP-1"])
        self.until(lambda: "model:HP" not in json.loads(self.layout.read_text())["connected"])
        hp = next(m for m in changed if m["name"] == "DP-1")
        hp.update(x=1280, y=0)
        self.set_live(self.live() + [hp])
        self.wait_monitor("DP-1", x=-5000, y=500)

    def test_status_uses_model_not_legacy_connector_identity(self):
        state = json.loads(self.layout.read_text())
        hp = state["outputs"].pop("model:HP")
        hp["id"] = "name:DP-1"
        state["outputs"]["name:DP-1"] = hp
        self.layout.write_text(json.dumps(state))
        actual = next(m for m in self.cli("status")["monitors"] if m["name"] == "DP-1")
        self.assertEqual(actual["id"], "model:HP")
        self.assertIsNone(actual["relation"])

    def test_status_identifies_inactive_model_without_saving_disabled_snapshot(self):
        live = self.live()
        next(m for m in live if m["name"] == "DP-1").update(disabled=True, width=0, height=0)
        self.set_live(live)
        self.layout.unlink()
        actual = next(m for m in self.cli("status")["monitors"] if m["name"] == "DP-1")
        self.assertEqual(actual["id"], "model:HP")
        self.assertIsNone(actual["relation"])
        self.assertFalse(self.layout.exists())

    def test_duplicate_connected_models_return_error_without_writes(self):
        live = self.live()
        next(m for m in live if m["name"] == "DP-3")["model"] = "HP"
        self.set_live(live)
        before = self.layout.read_bytes()
        result = self.cli("apply", "DP-1", "rotation", "2", success=False)
        self.assertIn("error", result)
        self.assertEqual(self.layout.read_bytes(), before)
        self.assertEqual(self.config.read_text(), self.original)
        self.assertFalse((self.state / "pending.json").exists())
        self.assertIn("error", self.cli("status", success=False))

    def test_daemon_legacy_disabled_profile_stays_off_until_explicit_enable(self):
        state = json.loads(self.layout.read_text())
        hp = state["outputs"]["model:HP"]
        hp.pop("desiredDisabled", None)
        hp.update(disabled=True, width=0, height=0, mode="preferred")
        self.layout.write_text(json.dumps(state))
        live = self.live()
        next(m for m in live if m["name"] == "DP-1").update(disabled=True, width=0, height=0)
        self.set_live(live)
        self.start_daemon()
        actual = next(m for m in self.cli("status")["monitors"] if m["name"] == "DP-1")
        self.assertTrue(actual["disabled"])
        self.assertNotIn("desiredDisabled", json.loads(self.layout.read_text())["outputs"]["model:HP"])
        token = self.daemon_apply("DP-1", "enable")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-1", disabled=False, width=1920, height=1080)
        self.assertFalse(json.loads(self.layout.read_text())["outputs"]["model:HP"]["desiredDisabled"])

    def test_daemon_migrates_relations_without_moving_saved_positions(self):
        state = json.loads(self.layout.read_text())
        state["relations"] = [dict(monitor="desc:HP", reference="desc:Xiaomi",
                                   direction="left", alignment="center")]
        self.layout.write_text(json.dumps(state))
        expected = {m["name"]: (m["x"], m["y"]) for m in self.live()}
        self.start_daemon()
        self.until(lambda: json.loads(self.layout.read_text())["relations"] == [])
        self.assertEqual({m["name"]: (m["x"], m["y"]) for m in self.live()}, expected)
        status = self.cli("status")
        self.assertEqual(status["relations"], [])
        self.assertTrue(all(m["relation"] is None for m in status["monitors"]))

    def test_daemon_legacy_observed_disabled_state_does_not_cancel_activation(self):
        state = json.loads(self.layout.read_text())
        hp = state["outputs"]["model:HP"]
        hp.pop("desiredDisabled", None)
        hp.update(disabled=True, width=0, height=0, mode="preferred")
        self.layout.write_text(json.dumps(state))
        self.start_daemon()
        actual = next(m for m in self.cli("status")["monitors"] if m["description"] == "HP")
        self.assertFalse(actual["disabled"])
        self.assertEqual((actual["width"], actual["height"]), (1920, 1080))

    def test_daemon_new_inactive_model_ignores_ambiguous_old_connector(self):
        state = json.loads(self.layout.read_text())
        other = copy.deepcopy(state["outputs"]["model:HP"])
        other.update(id="desc:Beihai", description="Beihai", model="Beihai",
                     name="DP-1", disabled=True, width=0, height=0, mode="preferred")
        other.pop("desiredDisabled", None)
        state["outputs"]["desc:Beihai"] = other
        legacy_outputs = {}
        for profile in state["outputs"].values():
            profile.pop("model", None)
            profile["id"] = "desc:" + profile["description"]
            legacy_outputs[profile["id"]] = profile
        state["outputs"] = legacy_outputs
        self.layout.write_text(json.dumps(state))
        self.config.write_text(self.original +
                               '\n-- BEGIN monitor-layout selector (managed automatically)\n'
                               'hl.monitor({ output = "DP-1", disabled = true })\n'
                               'hl.monitor({ output = "DP-2", disabled = true })\n'
                               '-- END monitor-layout selector\n')
        internal = next(m for m in self.live() if m["name"] == "eDP-1")
        acer = dict(name="DP-2", model="Acer X34",
                    description="Acer Technologies Acer X34 serial1",
                    width=0, height=0, refreshRate=0, scale=1, transform=0,
                    x=-1, y=-1, disabled=True,
                    availableModes=["3440x1440@59.97Hz", "1024x768@60.00Hz"])
        self.set_live([internal, acer])
        self.start_daemon()
        self.wait_monitor("DP-2", disabled=False, width=3440, height=1440,
                          scale=1, transform=0, x=1280, y=0)
        self.until(lambda: "model:Acer X34" in json.loads(self.layout.read_text())["outputs"])
        saved = json.loads(self.layout.read_text())
        self.assertEqual(saved["relations"], [])
        self.assertTrue(any(p["description"] == "HP" and p["transform"] == 1
                            for p in saved["outputs"].values()))
        self.assertTrue(any(p["description"] == "Beihai" for p in saved["outputs"].values()))
        self.assertNotIn('output = "DP-1"', self.config.read_text())
        self.assertEqual(self.cli("status")["error"], None)

    def test_daemon_without_saved_state_activates_new_inactive_model(self):
        self.layout.unlink()
        internal = next(m for m in self.live() if m["name"] == "eDP-1")
        hp = next(m for m in self.live() if m["name"] == "DP-1")
        hp.update(disabled=True, width=0, height=0, x=0, y=0)
        self.set_live([internal, hp])
        self.start_daemon()
        self.wait_monitor("DP-1", disabled=False, width=1920, height=1080,
                          scale=1, transform=0, x=1280, y=0)
        self.until(lambda: self.layout.exists())
        self.assertFalse(json.loads(self.layout.read_text())["outputs"]["model:HP"]["desiredDisabled"])

    def test_daemon_reused_connector_does_not_transfer_portrait_profile(self):
        self.confirm_position()
        # Emulate a pre-migration connector rule for the historical HP display.
        legacy = self.config.read_text()
        self.config.write_text(legacy)
        replacement = [m for m in self.live() if m["description"] != "HP"]
        replacement.append(dict(name="DP-1", model="Beihai ultrawide", description="Beihai ultrawide",
                                width=1920, height=1080, refreshRate=60.0,
                                scale=1.6, transform=1, x=-4115, y=120,
                                disabled=False,
                                availableModes=["3440x1440@60.00Hz", "1920x1080@60.00Hz"]))
        self.set_live(replacement)
        self.start_daemon()
        self.wait_monitor("DP-1", width=3440, height=1440, scale=1, transform=0,
                          disabled=False)
        source = self.config.read_text()
        self.assertNotIn('output = "desc:HP"', source)
        self.assertNotIn('output = "desc:Beihai ultrawide"', source)
        self.assertEqual(source.count('output = "DP-1"'), 1)
        self.assertTrue(source.startswith(self.original))
        self.until(lambda: "model:Beihai ultrawide" in json.loads(self.layout.read_text())["outputs"])
        state = json.loads(self.layout.read_text())
        self.assertEqual(state["outputs"]["model:HP"]["transform"], 1)
        self.assertEqual(state["outputs"]["model:Beihai ultrawide"]["transform"], 0)

    def test_daemon_hotplug_preserves_individual_saved_coordinates(self):
        self.confirm_position()
        config = self.config.read_bytes()
        original_live = self.live()
        self.start_daemon()
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.live(), original_live)
        self.set_live([m for m in self.live() if m["name"] != "DP-3"])
        self.until(lambda: "model:Xiaomi" not in json.loads(self.layout.read_text())["connected"])
        remaining = self.live()
        hp = next(m for m in remaining if m["name"] == "DP-1")
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertEqual(next(m for m in remaining if m["name"] == "eDP-1")["x"], 0)
        xiaomi = next(m for m in original_live if m["name"] == "DP-3")
        xiaomi.update(x=1280, y=-360, width=1920, height=1080)
        self.set_live(remaining + [xiaomi])
        self.wait_monitor("DP-3", x=-3440, y=0, width=3440, height=1440)
        hp = next(m for m in self.live() if m["name"] == "DP-1")
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [])

    def test_scale_reflow_preview_and_revert_preserve_the_entire_layout(self):
        self.confirm_position()
        config = self.config.read_bytes()
        state = self.layout.read_bytes()
        live = self.live()
        preview = self.cli("preview", "DP-3", "scale", "2")
        planned = {m["name"]: (m["x"], m["y"]) for m in preview["monitors"]}
        self.assertEqual(planned, {"eDP-1": (0, 0), "DP-3": (-1720, 0), "DP-1": (-2395, -240)})
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), live)
        token = self.cli("apply", "DP-3", "scale", "2")["pending"]["token"]
        self.assertEqual({m["name"]: (m["x"], m["y"]) for m in self.live()}, planned)
        self.assertEqual(self.layout.read_bytes(), state, "No reflow preference commits before Keep")
        self.cli("revert", token)
        self.completed()
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), live)

    def test_scale_reflow_timeout_restores_the_entire_layout(self):
        self.confirm_position()
        config = self.config.read_bytes()
        state = self.layout.read_bytes()
        live = self.live()
        self.cli("apply", "DP-3", "scale", "2")
        self.assertNotEqual(self.live(), live)
        self.completed()
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), live)

    def test_scale_reflow_confirm_commits_positions_and_daemon_does_not_reflow_twice(self):
        self.confirm_position()
        self.start_daemon()
        token = self.daemon_apply("DP-3", "scale", "2")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("eDP-1", x=0, y=0)
        self.wait_monitor("DP-3", scale=2, x=-1720, y=0)
        self.wait_monitor("DP-1", x=-2395, y=-240)
        state = json.loads(self.layout.read_text())
        self.assertEqual((state["outputs"]["model:HP"]["x"], state["outputs"]["model:HP"]["y"]), (-2395, -240))
        self.assertEqual(state["relations"], [])
        confirmed_state = json.loads(self.layout.read_text())
        self.wait_daemon_idle()
        self.events.send(b"configreloaded>>\n")
        self.wait_daemon_idle()
        self.assertEqual(json.loads(self.layout.read_text()), confirmed_state)
        self.wait_monitor("DP-1", x=-2395, y=-240)

    def test_external_scale_change_is_imported_without_a_second_reflow(self):
        self.confirm_position()
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-3")["scale"] = 2.0
        self.set_live(changed)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:Xiaomi"]["scale"] == 2.0)
        hp = next(m for m in self.live() if m["name"] == "DP-1")
        self.assertEqual((hp["x"], hp["y"]), (-4115, 120))
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [])

    def test_daemon_disable_enable_does_not_move_other_monitors(self):
        self.confirm_position()
        self.start_daemon()
        token = self.daemon_apply("DP-3", "disable")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-3", disabled=True)
        self.wait_monitor("DP-1", x=-4115, y=120)
        token = self.daemon_apply("DP-3", "enable")
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-3", disabled=False, x=-3440, y=0)
        self.wait_monitor("DP-1", x=-4115, y=120)
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [])

    def test_daemon_handles_hotplug_during_pending_confirmation(self):
        self.confirm_position()
        self.start_daemon()
        token = self.daemon_apply("DP-1", "rotation", "3")
        self.set_live([m for m in self.live() if m["name"] != "DP-3"])
        self.cli("confirm", token)
        self.completed()
        self.wait_monitor("DP-1", transform=3, x=-4115, y=120)
        self.assertEqual(json.loads(self.layout.read_text())["relations"], [])

    def test_daemon_idle_and_unrelated_events_do_not_query_hyprland(self):
        process = self.start_daemon()
        before = self.inventory_calls()
        self.events.send(b"workspace>>2\nactivewindow>>test,window\nwindowtitlev2>>123,title\n")
        time.sleep(1.4)
        self.assertIsNone(process.poll())
        self.assertEqual(self.inventory_calls(), before)

    def test_daemon_reassembles_fragmented_events_and_ignores_oversized_lines(self):
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["x"] = -5000
        self.set_live(changed, notify=False)
        before = self.inventory_calls()
        self.events.send(b"activewindow>>" + b"x" * 8192 + b"\nconfigrelo")
        time.sleep(0.2)
        self.assertEqual(self.inventory_calls(), before)
        self.events.send(b"aded>>\n")
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -5000, timeout=2)

    def test_daemon_rescans_after_socket_disconnect(self):
        self.start_daemon()
        self.events.disconnect()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["x"] = -5000
        self.set_live(changed, notify=False)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -5000, timeout=2.5)
        self.wait_daemon_idle()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["x"] = -6000
        self.set_live(changed)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -6000, timeout=2)

    def test_daemon_throttles_repeated_socket_eof(self):
        self.start_daemon()
        self.events.reject_clients = True
        before = self.inventory_calls()
        self.events.disconnect()
        time.sleep(1.5)
        self.assertLessEqual(self.inventory_calls() - before, 5)

    def test_daemon_retries_failed_inventory_before_the_slow_backstop(self):
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["x"] = -5000
        (self.root / "fail-inventory").touch()
        self.set_live(changed)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -5000, timeout=2.5)

    def test_daemon_slow_backstop_imports_changes_without_an_event(self):
        self.start_daemon()
        changed = self.live()
        next(m for m in changed if m["name"] == "DP-1")["x"] = -5000
        self.set_live(changed, notify=False)
        self.until(lambda: json.loads(self.layout.read_text())["outputs"]["model:HP"]["x"] == -5000)

    def test_running_daemon_recovers_killed_watchdog_without_socket_events(self):
        daemon = self.start_daemon()
        config, state, live = self.config.read_bytes(), self.layout.read_bytes(), self.live()
        self.env["MONITOR_TEST_NO_RELOAD_EVENT"] = "1"
        self.cli("apply", *POSITION)
        watchdogs = [pid for pid in self.owned_processes() if pid != daemon.pid]
        self.assertEqual(len(watchdogs), 1)
        os.kill(watchdogs[0], signal.SIGKILL)
        self.until(lambda: not (self.state / "pending.json").exists(), timeout=2)
        self.assertEqual(self.config.read_bytes(), config)
        self.assertEqual(self.layout.read_bytes(), state)
        self.assertEqual(self.live(), live)

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
