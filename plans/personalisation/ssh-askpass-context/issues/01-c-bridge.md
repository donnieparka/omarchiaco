# C askpass bridge

Status: complete
Blocked by: none
Spec: ../spec.md

Replace the Python askpass bridge with a libc-only C executable. Collect bounded
caller ancestry and conservative Git/SSH context, preserve the existing socket
and OpenSSH response contracts, and provide explicit build/install instructions.

Acceptance criteria:

- Build with the system C compiler and strict warnings, without new runtime libraries.
- All collection, interpretation, JSON, socket, timeout, and failure behavior in
  the specification is implemented. Ambiguity or missing metadata yields unknown.
- No subprocesses, raw command lines, environment contents, or secret logs.
- Building does not replace the installed helper. Installation is a separate target
  and documents backup and rollback of the existing helper.
- Preserve monitor helpers unchanged.

Own bridge source, headers, build/install targets, removal of the repository's
Python askpass snapshot, and corresponding README changes. Ticket 03 owns the
full test fixtures and test targets after this ticket is merged.

## Comments

Implemented in `1bc2094`, merged into the PR branch in `61dc979`.
The libc-only bridge replaces the Python snapshot and provides bounded process
collection, conservative operation parsing, JSON encoding and decoding, and
socket transport. The README documents explicit installation and rollback.

The implementer reported a strict build, targeted AddressSanitizer and
UndefinedBehaviorSanitizer checks, and checks through the actual Unix socket.
The merger independently ran `make` with the strict warning flags successfully
and confirmed `git diff --check` passes. Ticket 03 owns the complete committed
fixture suite and integration verification.
