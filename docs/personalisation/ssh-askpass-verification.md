# Verifying SSH askpass

Run from the repository root. Tests use dummy values and private temporary Unix
sockets. They do not connect to SSH servers, inspect private keys, install the
helper, or change the live desktop configuration.

## C bridge

```bash
make test
make test-sanitize
```

`make test` compiles the actual bridge source with the normal strict warnings
and a 500 ms test deadline. Its C fixture binds an isolated socket and launches
copies of itself named `git`, `ssh`, and `ssh-add`. Each fixture remains alive as
the bridge's real parent. The received metadata must match the fixture's PID,
executable path, and controlled working directory. That directory contains both
Unicode and a newline to check metadata sanitation through `/proc`.

The suite checks supported and ambiguous argument forms, preservation of prompt
quotes/backslashes/Unicode, exclusion of arbitrary remote-command arguments,
empty and escaped Unicode responses, and the exact stdout/exit contract. It
also exercises cancellation, busy replies, malformed JSON, duplicate required
fields including escaped keys, missing response fields, invalid UTF-8/surrogates,
forbidden decoded CR/LF/NUL, early disconnect, missing sockets, bounds, and the
socket deadline. Confirmation and notification hints must fail before any
connection reaches the test listener.

Both commands run two transport variants. The first uses the actual socket
transport without wrappers. The second links test-only `send`/`recv` wrappers
which limit every syscall to three bytes, forcing short writes and short reads
in the same production exchange loop. The server also reads request lines one
byte at a time and sends the Unicode success reply one byte at a time.

`make test-sanitize` compiles separate test artifacts with AddressSanitizer and
UndefinedBehaviorSanitizer. These are build-time verification dependencies,
with no effect on the installed program. A compiler without sanitizer support
will fail this target; report that limitation rather than treating it as a pass.
`make test` and `make` continue to use their normal artifacts afterward.

## Real Quickshell dialog

This optional test requires an active Wayland session, Quickshell, the QtTest QML
module, and the existing Omarchy `Commons` and `Ui` modules. It briefly displays
dummy authentication requests and takes keyboard focus, so run it when typing
into another application will not be interrupted.

```bash
make test-dialog
```

For an Omarchy shell installed elsewhere:

```bash
OMARCHY_TEST_SHELL=/path/to/omarchy/shell make test-dialog
```

The runner starts an isolated Quickshell instance with a fresh temporary runtime
directory. It uses a symlink to the existing Wayland socket to reach the current
display; its askpass listener has a separate socket from the live shell. It
loads this checkout's actual plugin and the installed theme modules. The runtime
directory is removed after the instance exits. Screenshots and console output
remain under the printed `build/dialog-tests.*` directory, ignored by Git.

The eight checks cover requester/operation summaries and details, legacy and
malformed optional context, remote-password and `ssh-add` wording, busy handling
and cleanup, Enter/Escape, the actual compiled C bridge through the real QML
SocketServer, native requester disconnect, and long Unicode text with 16
ancestors in a constrained card. The end-to-end success case submits only
`dummy-雪-😀` and checks exact stdout, exit 0, and cleared UI state.

A successful run prints `QML_CHECK_PASS 0`. Check `context.png` and
`constrained.png` as well; assertions do not establish visual quality. The
constrained check forces the real card to 300 by 240 logical pixels, scrolls its
details, and verifies that the input/footer remain available.

The metadata schema tests can also run without the desktop:

```bash
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner \
  -input shell/plugins/services/ssh-askpass/tests
```

Adjust the `qmltestrunner` path for the installed Qt tools.

## Verification evidence and limits

On 2026-10-05, strict compiler builds and both transport variants passed with
666 assertions per variant. Both variants also passed under AddressSanitizer
and UndefinedBehaviorSanitizer. The
500 ms stalled-socket check returned without stdout in approximately 0.5 seconds.
The eight real-plugin checks passed with `QML_CHECK_PASS 0`, including C-to-QML
success and native disconnect. Both generated screenshots were inspected: the
normal summary wraps cleanly and the constrained details scroll while keeping
the secret field and keyboard instructions visible. The 13 schema tests passed.

The isolated host emitted a portal registration warning and `PeerClosedError`
for the intentional disconnect. These did not fail the tests. Any other warning
or changed test result needs investigation.

The missing-metadata case exercises the actual collector against a nonexistent
PID. It does not simulate every `/proc` permission restriction or race. The
normal 120-second wait is checked through the same code using the shorter
compile-time deadline; the suite does not spend two minutes waiting. Display
verification used the current machine's theme and scale, with a constrained
card, rather than a second physical monitor. Tests never install the helper or
verify authentication against a real SSH server. No live installation was
performed. An install into a temporary destination was separately checked for
matching bytes and executable mode 755.

## Code review

Two independent read-only reviews compared baseline `6ee71eb` with implementation
`e430dde` on 2026-10-05. The Standards review found no actionable code-quality
issues or documented-standard breaches. The Spec review found no missing,
incorrect, or unrelated behavior against the local specification and tickets.
The Spec reviewer independently ran both normal transport variants and the
offscreen schema tests. The main agent separately reran the sanitizer variants
and the real-dialog checks on the merged branch; all passed.
