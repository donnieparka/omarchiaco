# Verify bridge and dialog behavior

Status: complete
Blocked by: 01, 02
Spec: ../spec.md

Add C fixtures and a Bash runner exercising the compiled production bridge
through the actual socket boundary. Verify the real QML service with dummy
requests in an isolated Quickshell instance and runtime directory.

Acceptance criteria:

- Cover the bridge integration cases enumerated in the specification, including
  fragmented I/O, escaped and Unicode responses, malformed replies, bounded
  protocol handling, cancellation, timeout, and evidence-based context parsing.
- Run strict compiler warnings and available address/undefined-behavior sanitizers.
- Document executable test commands and the dialog checks performed.
- Use only dummy credentials and temporary fixtures; no SSH server, real key,
  live helper installation, or modification of the user's current desktop config.
- Record any verification limits honestly instead of marking untested cases as passed.

Own test fixtures, test targets, and verification documentation. The main agent
will perform the isolated visual dialog checks and final end-to-end validation.

## Comments

Implemented in `c0d5302`, merged into the PR branch in `88ba97e`.
Verification commands, evidence, and remaining limits are recorded in
`docs/ssh-askpass-verification.md`.

- `make test` passed 666 assertions for each normal and forced three-byte
  transport variant. The merger independently reran both variants after merging.
- `make test-sanitize` passed both variants with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- `make test-dialog` passed all eight checks against the actual plugin,
  including the compiled bridge and native disconnect cleanup.
- The Qt context schema suite passed all 13 cases.
- The root agent inspected both real dialog screenshots and verified that
  installation into a temporary destination copies matching bytes with mode 755.
- `git diff --check` passed after the merge.

Tests used dummy credentials and isolated sockets. No live helper installation
or desktop configuration change occurred.
