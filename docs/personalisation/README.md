# Original personalisation notes

This document preserves the previous repository documentation. Use [the source migration guide](migration.md) for the current fork layout and setup commands.

# Omarchy personalisation

Personal Omarchy configuration, Quickshell plugins, and supporting scripts.

This repository lives in `~/.config/omarchy`. Its configuration and plugins are
the files used by the desktop on this machine.

## Contents

- `shell.json` and `shell.toml`: shell layout, idle settings, and font preferences.
- `shell/plugins/services/ssh-askpass/`: the SSH authentication dialog.
- `shell/plugins/panels/monitor/`: monitor controls.
- `shell/Ui/`: visible keyboard shortcuts inside bar panels.
- `extensions/`, `hooks/`, `defaults/`, `branding/`, and `themed/`: other Omarchy configuration.
- `default/native/`: the C monitor controller and the libc-only C SSH askpass bridge.
- `bin/`: compatibility entry points for the monitor controller.
- `default/systemd/user/`: the user service for monitor connection changes.
- `environment.d/`: a copy of the askpass configuration installed in `~/.config/environment.d`.

The `environment.d/` files are snapshots. Editing them here does not update
the installed copies automatically. C executables need rebuilding and installing.

## Building and installing askpass

Build the askpass bridge with the system C compiler, libc headers, and Make:

```bash
make build/omarchy-ssh-askpass
```

This creates `build/omarchy-ssh-askpass` with strict compiler warnings. Building
does not replace the installed helper or start a service. The executable reads
bounded caller information from `/proc` and uses the existing Omarchy socket.
It needs no Python, helper subprocess, or additional runtime library.

Before deliberately installing, save the current helper so it can be restored:

```bash
cp -p "$HOME/.local/bin/omarchy-ssh-askpass" "$HOME/.local/bin/omarchy-ssh-askpass.bak"
make install
```

The install target uses the existing `~/.local/bin/omarchy-ssh-askpass` path.
For a different configured path, use `make install ASKPASS_INSTALL_PATH=/path/to/helper`.
The matching plugin under `shell/plugins/services/ssh-askpass/` must be available in your
Omarchy configuration. Installation of the executable alone does not copy the
plugin from a separate checkout.

To restore the previous installed bridge:

```bash
install -Dm755 "$HOME/.local/bin/omarchy-ssh-askpass.bak" "$HOME/.local/bin/omarchy-ssh-askpass"
```

Caller ancestry shows observed programs, not an authenticated identity or an
application's internal intent. Unsupported command forms remain unknown.
The helper rejects confirmation and notification hints, which require a
different interface, and never prints metadata to SSH's response channel.

## Monitor controls

The display icon opens a QML popup with controls for each connected monitor:
position, rotation, resolution, scale, and enabling/disabling. Position selects
a reference monitor, a side, and an alignment. Resolution choices come from
Hyprland's available modes. There is no separate monitor menu.

The C controller calculates positions from logical dimensions after scale and rotation. Each monitor saves its own coordinates, resolution, scale, rotation and activation preference. Relative placement uses a reference to calculate coordinates once; it does not establish a persistent dependency. Explicit scale, resolution and rotation changes recalculate the active layout while keeping the laptop fixed, or the monitor nearest the origin when no laptop is active. Neighbour sides, alignments, gaps and custom offsets come from the live layout before the command. Scale buttons offer familiar presets only when they produce whole-pixel logical dimensions exactly, without adjusting the selected value. The C controller filters 1, 1.25, 1.5, 1.6, 1.75, 2, 2.5, 3, 3.5 and 4; the panel caches the choices by resolution until it is reloaded. Changing resolution loads the matching choices, while status polls, scale changes and rotation reuse the cache. Preset application rejects a choice if a concurrent resolution change makes it inexact. Existing command-line scale requests remain supported, including normalization of non-exact values. Disconnecting or disabling a monitor does not move the others, and reconnection restores its saved coordinates. Gaps are allowed; layouts that would overlap are rejected. Legacy relations are removed while retaining each saved position. See [independent state](adr/0002-independent-monitor-state.md) and [geometry reflow](adr/0003-geometry-reflow.md).

Manual changes open a large, centered Omarchy confirmation
dialog with Mantieni and Ripristina, separate from the settings panel. Choosing
Ripristina, pressing Escape, or letting 15 seconds expire restores the previous
settings. An independent process owns the transaction, so disabling the monitor
hosting the settings panel cannot destroy its confirmation or rollback.

Build requirements are a C11 compiler, Make, pkg-config and json-c headers.
The controller uses `hyprctl` to query and apply configuration and `notify-send`
for rollback notifications. Build and test without changing the desktop:

```bash
make build/omarchy-monitor-control
make test-monitor
```

The integration tests use a private simulated Hyprland and need Python 3.
Python is not a dependency of the installed monitor controller.

Install deliberately after testing:

```bash
make install-monitor
systemctl --user daemon-reload
systemctl --user enable --now omarchy-monitor-control.service
```

The installed `omarchy-monitor-layout` alias opens the popup.
`omarchy-monitor-apply` is a compatibility alias for the C apply command.
The service watches connections and geometry changes in the graphical session.
It can be stopped with `systemctl --user stop omarchy-monitor-control.service`.

Panel keyboard shortcuts are described in [panel-shortcuts.md](panel-shortcuts.md).
They reserve codes for Wi-Fi SSIDs and Bluetooth addresses while a panel is open,
so scanning or reordering cannot make an old code select a different device.
Run `make test-panel-hints` to test changing delegates, input handling and sizing.
The separate smart mouse selector is documented in [keyboard-mouse.md](keyboard-mouse.md).

CLI examples:

```bash
omarchy-monitor-control status
omarchy-monitor-control preview DP-1 position left DP-3 center
omarchy-monitor-control apply DP-1 position left DP-3 center
omarchy-monitor-control open
# Use the token returned by apply, or confirm in the centered dialog:
omarchy-monitor-control confirm TOKEN
omarchy-monitor-control revert TOKEN
```

Directions are `left`, `right`, `up`, `down`; alignments are `start`, `center`,
`end`. Other actions are `mode`, `scale`, `rotation` with values, and `enable`
or `disable` without a value. Rotation uses 0, 1, 2, 3 for 0°, 90°, 180°, 270°.

The controller saves preferences by Hyprland model name. The connector is only the current route used to apply those preferences; switching ports or changing the description or serial does not change the model profile. Two connected monitors with the same model are unsupported and return an error. The managed Lua block contains rules only for currently connected monitors. Stale or ambiguous legacy connector rules are discarded, not assigned to a model and not allowed to block another monitor. Lua outside the managed block is preserved; unknown properties in a managed rule are retained only when its ownership is known. Legacy description profiles retain their preferences until the corresponding monitor reconnects and supplies its model.

An observed activation failure is not a request to disable a monitor. Explicit enable/disable preferences are saved only when the transaction is confirmed. Legacy disabled snapshots without explicit intent stay off until explicitly enabled, but must not cancel a subsequently active display. A new model starts enabled at its advertised preferred mode, scale 1 and normal rotation, with an initial non-overlapping position. An observed disabled state must not stop this first activation or establish a disabled preference, and a reused connector must not supply another model's settings. If a saved mode is temporarily unavailable, the engine uses an advertised fallback without overwriting the preferred geometry, and restores the saved mode when it becomes available again. Verification also checks the monitor model, so replacing a display with a different model on the same connector during application cannot confirm another model's settings.

Layout state, pending transactions and backups live in `$XDG_STATE_HOME/omarchy/monitor-control`, normally
`~/.local/state/omarchy/monitor-control`. A lock prevents concurrent changes.
The service recovers an interrupted pending transaction before reconciling.
It verifies that Hyprland actually applied the settings before asking to keep
them. Restoring configuration cannot repair a compositor or driver fault that
also prevents the previous video mode from being activated.

The saved askpass environment configuration contains this machine's absolute
home path. Adjust `SSH_ASKPASS` before installing it on another machine. New
environment settings apply to applications started in a new login session.

SSH keys, credentials, and runtime sockets are not part of this repository.
