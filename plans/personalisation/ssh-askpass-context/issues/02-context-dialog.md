# Requester context in the Omarchy dialog

Status: complete
Blocked by: none
Spec: ../spec.md

Extend the existing QML service to validate and display the optional context
schema from the specification, keeping legacy prompt-only requests supported.

Acceptance criteria:

- Show observed requester, supported operation or unknown, and supplied destination.
- Distinguish local key passphrases and account passwords while preserving the prompt.
- Expand details for the observed ancestry, PIDs, and working directory.
- Long text and constrained displays remain readable; text is plain text.
- Details retain typed input and keyboard access; Enter/Escape retain existing semantics.
- Busy rejection, disconnect cleanup, malformed context fallback, and secret clearing work.
- Use the existing Omarchy UI host and theme without additional dependencies.

Own files inside the askpass plugin only. Develop in the assigned worktree;
do not edit the live plugin or replace its runtime socket.

## Comments

Implemented in `74ba3f1`, merged into the PR branch in `18a10c3`.
The dialog accepts optional bounded context, preserves legacy prompts, and shows
requester, operation, destination, and expandable process details. Text wraps
inside the scrollable card, and supplied text uses plain-text rendering.

Verification: the Qt context test suite passed all 13 cases using
`QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input plugins/iaco.ssh-askpass/tests`.
`git diff --check` passed after merging. Real dialog verification belongs to
ticket 03 and remains pending.
