# Display controls and tray shortcuts

The monitor panel selects modes reported by Hyprland and controls resolution, scale, rotation, activation, and position. Relative placement remains valid through geometry and connection changes. Left and right placement defaults to aligned centers. Manually moving a monitor removes the relations involving it. Disconnected monitors leave no gap in the connected layout.

Manual changes open a separate confirmation popup. Keep saves the change; Revert, Escape, or the independent 15-second watchdog restores the previous configuration. Disabling the last active monitor is rejected.

The core bar displays two-letter keyboard labels beside active controls. Wi-Fi labels follow SSIDs and Bluetooth labels follow MAC addresses while the panel is open. Removed labels stay reserved until close. Text fields retain normal typing; slider labels enable arrow-key adjustment. The labels and panel content stay inside their bounds. Final confirmation popups retain their own keyboard handling.

## Setup

Use this checkout through the normal Omarchy development or installation workflow. Its commands must be on PATH and OMARCHY_PATH must identify the checkout. The shell must run from this checkout to load the monitor panel and shared PanelHints component.

Build and install the C controller and enable its user service:

```bash
omarchy-setup-desktop-controls
```

The default installer performs this build automatically and includes json-c headers and the compiler. The optional ydotool service is used for custom controls that require a mouse click; configure it with `omarchy-dev-install-ydoo`. The separate wl-kbptr keyboard mouse selector is independent of the tray shortcuts.

See [panel shortcuts](panel-shortcuts.md) for keyboard usage. The implementation lives in `default/native/`, `shell/plugins/panels/monitor/`, and `shell/Ui/PanelHints.qml`.

## Verification

```bash
make all test-monitor test-panel-hints
./test/cli
bash test/shell.d/monitor-test.sh
bash test/shell.d/panel-command-path-test.sh
bash test/shell.d/bin-style-test.sh
bash test/shell.d/manifest-entrypoints-test.sh
```

Monitor tests use simulated Hyprland responses and transactions. Panel tests exercise dynamic identities, typing, key handling, and size constraints without changing network connections or the active monitor configuration.
