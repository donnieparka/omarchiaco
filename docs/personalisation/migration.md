# Personalisation source migration

The personalisation previously maintained in `donnieparka/omarchy-personalisation` now lives in this public fork of `omacom/omarchy`. The `feature/personalisation` branch contains the port. Upstream history is preserved; the old private repository history is not included.

## Included controls

- The monitor panel selects resolutions from Hyprland's reported modes and controls scale, rotation, activation, and position.
- Relative placement persists through resolution, scale, rotation, and connection changes. Left and right placement defaults to aligned centers. Moving a monitor manually removes its placement relations. Disconnected monitors no longer leave a gap in the connected layout.
- Display changes open a separate confirmation popup with Keep and Revert actions. A separate watchdog restores the previous configuration after 15 seconds without confirmation.
- Tray panels show letter shortcuts beside actions. Wi-Fi and Bluetooth entries retain their labels while the panel is open as devices appear and disappear. Badges stay inside panel bounds; text fields retain normal typing behavior.
- SSH askpass uses a C bridge and a trusted shell service to show the observed caller context.
- `wl-kbptr` remains a separate keyboard mouse selector. `Super+Ctrl+M` starts smart selection and `Super+Ctrl+Shift+M` starts the grid.

## Source locations

| Previous location | Fork location |
| --- | --- |
| `src/` and native Makefile | `default/native/`, with a root Makefile for development |
| `plugins/iaco.monitor/` | `shell/plugins/panels/monitor/`, using `omarchy.monitor` |
| `plugins/iaco.bar/` | Shared `shell/Ui/PanelHints.qml` and `PanelHintModel.js`, instantiated by the core bar |
| `plugins/iaco.ssh-askpass/` | `shell/plugins/services/ssh-askpass/`, using `omarchy.ssh-askpass` |
| Shell settings, branding, defaults, extensions, hooks, and template samples | `config/omarchy/` |
| Monitor user unit | `default/systemd/user/omarchy-monitor-control.service` |
| Askpass environment snapshot | Generated for the current user by the setup script |
| Smart mouse configuration and compatibility patch | `config/wl-kbptr/` and `default/keyboard-mouse/` |
| Tests and previous design notes | `tests/`, `test/shell.d/personalisation-test.sh`, `docs/personalisation/`, and `plans/personalisation/` |

## Setup

Use this fork as the Omarchy source checkout through the normal Omarchy development or installation workflow. `OMARCHY_PATH` must identify that checkout and its `bin/` commands must be on `PATH`. Pointing this variable at the checkout alone does not activate its shell or refresh user configuration.

On an existing installation using the fork, build and install the C helpers and enable the monitor service:

```bash
omarchy-setup-desktop-controls
```

The compiler, Make, and `json-c` headers are required. The default installer includes them. The command also generates `~/.config/environment.d/omarchy-ssh-askpass.conf` with the current user's helper path. A new session picks up those environment settings.

The fork's `config/omarchy/shell.json` enables `omarchy.ssh-askpass` and uses the built-in monitor plugin and bar. Existing users must merge those plugin settings into their shell configuration and remove the old `iaco.monitor`, `iaco.bar`, and `iaco.ssh-askpass` overrides before restarting the fork's shell. Review the other defaults before refreshing personal configuration.

The standalone keyboard mouse helper is optional:

```bash
omarchy-setup-keyboard-mouse
```

This builds the pinned `wl-kbptr` source with its compatibility patch and configures `ydotool` through Omarchy's existing setup command. Its upstream GPL license is included next to the patch. See [keyboard mouse controls](keyboard-mouse.md) and [panel shortcuts](panel-shortcuts.md) for usage.

## Verification

Run focused tests from the checkout:

```bash
make all test test-monitor test-panel-hints
./test/cli
bash test/shell.d/monitor-test.sh
bash test/shell.d/panel-command-path-test.sh
bash test/shell.d/bin-style-test.sh
bash test/shell.d/manifest-entrypoints-test.sh
```

`make test-dialog` checks the real SSH dialog on an active Wayland desktop using dummy credentials and saves screenshots beneath `build/`. Tests do not install the fork or replace the running desktop configuration.

The previous repository's documentation is preserved in [the original notes](README.md). Machine-specific environment files are generated rather than copied. Local configuration and a local backup of the previous repository remain available after the old GitHub repository is removed.
