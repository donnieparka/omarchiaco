# SSH askpass requester context

Status: ready-for-agent

Repository: `donnieparka/omarchy-personalisation`

Baseline: `6ee71eb`, the existing Omarchy configuration and Python askpass bridge.

This specification authorizes no implementation, installation, or deployment by
itself. The current request is to write the specification.

## Problem statement

When the SSH authentication dialog appears, I cannot tell which program caused
it or what that program is doing. The dialog displays only SSH's prompt, and
long prompts are truncated. This makes an unexpected request difficult to assess.

I want the smallest practical implementation. The askpass bridge should use C,
with no Python runtime, additional daemon, or third-party runtime library.

## Solution

Replace the short-lived Python bridge with a small C executable. At invocation,
it inspects its caller and accessible process ancestors through Linux `/proc`,
then sends that context with the original prompt to the existing Quickshell
dialog through its existing Unix socket.

The dialog answers two separate questions:

- Who is asking? Show the immediate caller and any identifiable initiating
  program in its ancestry.
- Why is it asking? Show the authentication request described by SSH, and a
  recognized application operation when process arguments provide evidence.

A local key passphrase unlocks a private key on this machine. It is different
from a remote account password. The dialog must not imply that a local key
passphrase will be sent to the destination server.

Unknown or ambiguous context remains explicitly unknown. An application name,
operation, destination, or explanation must not be invented to fill a field.

## User stories

1. When an SSH request appears, I can see the program that invoked askpass without
   having to inspect processes myself.
2. When an identifiable parent program caused the request, I can see that program
   as well as the immediate SSH caller. If the evidence identifies only a shell
   or generic runtime, the dialog does not invent a desktop application name.
3. When Git's command arguments identify an operation, I can see that operation,
   such as fetch or push, alongside the authentication request.
4. When a direct SSH invocation exposes its destination, I can see that supplied
   destination. If it is a configuration alias, it remains labeled as the
   supplied destination rather than a verified hostname.
5. When SSH asks for a local key passphrase, I can read the full original prompt
   and key path. When it requests a remote account password or another secret,
   its original wording remains available.
6. When `ssh-add` asks for a key passphrase, the dialog identifies `ssh-add` and
   describes adding or unlocking a key for the agent without inventing a remote
   connection.
7. When caller information is unavailable or incomplete, authentication still
   works and I see an explicit unknown value rather than a guessed explanation.
8. I can expand details to inspect the available process chain and working
   directory. Expanding details does not discard what I have typed.
9. I can submit with Enter or cancel with Escape. Cancellation, disconnects, and
   errors clear the dialog and typed secret, and do not return a secret to SSH.
10. The new bridge installs at the existing askpass executable location and
    requires no additional service or runtime dependency.

## Implementation decisions

### Scope and components

Keep the existing Omarchy plugin, styling, socket name, and environment-based
askpass configuration. Change the bridge and authentication dialog only. The
monitor scripts and other personalisations retain their existing behavior.

The bridge is one executable built with the system C compiler and libc. It reads
process information directly; it does not execute `ps`, `jq`, `socat`, Python,
Hyprland queries, or another helper to collect or transport context. The existing
Quickshell process remains the UI host.

Use a minimal build target and an explicit install target. Build output is ignored
by Git. The install target places the compiled executable at the location already
configured by `SSH_ASKPASS`. Building alone does not replace the live executable.

### Module boundaries

Keep these responsibilities separate inside the executable, without introducing
additional executables or public APIs:

- Context collection reads a bounded snapshot of the parent process ancestry.
- Context interpretation extracts supported operations and destinations from
  that snapshot. It returns unknown for unsupported or ambiguous inputs.
- Socket transport encodes the request, reads and validates the reply, and
  preserves the OpenSSH stdout and exit-status contract.

The QML service validates incoming metadata and renders it. It does not collect
caller information from its own process tree: that tree belongs to the shell,
not the program requesting authentication.

### Caller attribution

Start with the bridge's actual parent PID. Collect executable identity, PID,
parent PID, and readable working directory. Inspect at most 16 ancestors and
stop at PID 1, a missing process, or a repeated PID. Each file read has a fixed
size limit. Missing or inaccessible information must not block the prompt.

Executable identity is observed metadata. An originating desktop app inferred
from ancestry is best effort and must be presented as such. The focused window
does not establish who caused the request. A service or agent may separate the
actual initiator from the visible ancestry; show the observed caller in that case.

Command arguments may be inspected transiently to extract allowlisted facts.
Do not send full command lines or environment contents to the dialog, write them
to disk, or expose arbitrary remote commands. Preserve valid UTF-8 and render all
metadata as plain text; replace control characters in collected metadata.

### Supported explanations

The following conservative rules define the first version:

| Evidence | Displayed explanation |
| --- | --- |
| Recognizable key-passphrase prompt | Unlock a local SSH key; preserve the original prompt and key path |
| Recognizable account-password prompt | Enter the account password; preserve the original account and host wording |
| Immediate caller is `ssh-add` with a key-passphrase prompt | Unlock or add a key to the SSH agent; no implied destination |
| Identified Git command and supported subcommand | Show the observed Git operation |
| Unambiguous destination argument on an identified SSH process | Show the supplied destination or alias |
| Other prompt or ambiguous evidence | Preserve the prompt; label the unavailable explanation or operation as unknown |

Recognize Git `clone`, `fetch`, `pull`, `push`, and `ls-remote` when argument
parsing establishes the actual subcommand. Distinguish Git global options from
the subcommand. Recognize SSH destinations only when supported option parsing
establishes which positional argument is the destination. Unsupported forms
fall back to unknown. Do not assume every Git operation is a fetch.

Do not read private key contents to classify a request, contact a destination,
execute SSH configuration directives, or run Git hooks to obtain an explanation.
Key-agent cache state and an application's internal motivation are not generally
available at this boundary and must not be asserted.

### Socket contract

Keep the Unix stream socket at
`$XDG_RUNTIME_DIR/omarchy-ssh-askpass.sock`, with the existing fallback to the
current UID's directory under `/run/user`.

Use one UTF-8 JSON request terminated by a newline. Preserve the existing
required `prompt` string and add optional structured context:

| Field | Type | Meaning |
| --- | --- | --- |
| `prompt` | string | Original askpass argument, or the existing fallback prompt |
| `context` | object, optional | Additional context collected by the C bridge |
| `context.processes` | array | Observed ancestors, immediate caller first; at most 16 |
| Process `pid` | positive integer | Observed process ID |
| Process `executable` | string | Observed executable identity, or readable process name as fallback |
| `context.cwd` | string, optional | Immediate caller's readable working directory |
| `context.operation` | string, optional | Recognized Git operation from the supported set |
| `context.destination` | string, optional | Supplied SSH destination or alias |

Do not transmit a separate speculative application label or a free-form generated
reason. Derive the user-facing summary from the prompt and bounded metadata.
Unknown values are omitted and displayed as unknown where relevant.

The QML receiver continues to accept legacy prompt-only requests. Malformed
optional context is ignored without hiding a valid authentication prompt.
Keep the existing prompt limit of 1,024 QML string code units. Limit collected
strings to 1,024 UTF-8 bytes each and the encoded request to 32 KiB. If optional
context cannot fit, omit it rather than truncating JSON or the original prompt.

Replies retain the existing contract:

- Success: `ok` is boolean true and `response` is a string.
- Cancellation or rejection: `ok` is boolean false, with an optional error code.
- An active request causes a second request to receive the existing busy error;
  it does not replace the visible request.

The C bridge implements bounded JSON escaping and decoding with no third-party
runtime dependency. It must decode JSON string escapes and Unicode correctly,
reject malformed or ambiguous replies, and tolerate socket reads and writes
being split into multiple operations. A regular expression is not a JSON parser.

### OpenSSH compatibility and failure behavior

On success, stdout contains only the decoded response followed by one newline,
and the bridge exits with status 0. No caller metadata or diagnostic output goes
to stdout. Preserve successful empty responses.

On cancellation, busy rejection, connection failure, malformed reply, oversized
reply, or timeout, exit nonzero with no response on stdout. Reject decoded
responses containing CR, LF, or NUL. Keep the existing 8,192-byte reply-line limit
and use a 120-second end-to-end socket deadline, including waiting for the user.

Metadata collection failure alone is not an authentication failure. Termination
or disconnect of the requesting bridge clears the matching visible dialog.

Do not log typed secrets or responses. Clear the QML input and release request
context on completion or disconnect. Clear the bridge's response buffer before
exit where practical. Stdout remains exclusively the OpenSSH response channel.

This version covers interactive secret requests. OpenSSH also has confirmation
and notification hints through `SSH_ASKPASS_PROMPT`. Those require separate UI
semantics and are outside this change. If either hint is supplied, reject the
unsupported request without presenting it as a password or passphrase request.

### Dialog behavior

Show a short request heading, the observed requester, and a supported operation
or an explicit unknown value. Preserve the original SSH prompt separately.
Show a supplied destination when available. The wording distinguishes a local
key passphrase from a remote password.

Provide expandable details for the process chain, PIDs, and working directory.
Keep technical details out of the default summary when they do not help identify
the request. All externally supplied text uses plain-text rendering.

Replace the fixed-height card with content-aware sizing. Wrap text and provide
scrolling when needed so the original prompt and useful details remain readable
on smaller displays. Retain the Omarchy theme, masked secret input, initial
keyboard focus, Enter submission, and Escape cancellation. Expanding details
must preserve the input; the input must remain keyboard-accessible afterward.

## Testing decisions

Confirmed testing boundary: exercise the compiled executable through its actual
Unix socket and process ancestry, then inspect the real QML dialog using dummy
prompts and secrets. The user confirmed both automatic bridge tests and a check
of the actual dialog after reviewing the explanation.

The repository currently has no test suite or established test examples. Use
small C fixtures and a Bash runner so verification does not introduce a Python
or JavaScript dependency. Tests use a temporary runtime directory and must not
contact GitHub or another SSH server, read real keys, or use real credentials.

### Bridge integration tests

A test server implements the existing newline-delimited socket protocol. A
controlled parent fixture invokes the real executable with known arguments and
ancestry. Verify these externally observable behaviors:

1. Original prompt text, including quotes, backslashes, and Unicode, survives
   encoding. The immediate caller PID and executable match the actual fixture.
2. Recognized Git operations, SSH destination arguments, `ssh-add`, and ambiguous
   command forms produce the expected fields or unknown fallback. Parsing cases
   can use focused tests against the same production interpretation functions.
3. Successful replies return exactly the secret and one newline on stdout,
   including empty and escaped Unicode responses; metadata never appears there.
4. Cancellation, busy rejection, missing socket, early disconnect, malformed or
   oversized replies, duplicate response fields, and forbidden decoded characters
   produce a nonzero exit with no secret output.
5. Fragmented request writes and response reads preserve the protocol. A stalled
   connection respects the deadline. A short test deadline may be injected at
   build time without adding a production environment override.
6. Unavailable process metadata does not prevent a valid request. Where a
   disappearing process cannot be reproduced deterministically, test the same
   production collector against a controlled missing-process fixture.

Compile with strict warnings. Run the fixture suite with memory and undefined
behavior sanitizers when supported by the installed compiler. Cover boundary
conditions of the JSON decoder and bounds handling rather than duplicating
implementation details in tests.

### Dialog verification

Use a dummy request to check the actual shell plugin for:

- A direct SSH request, a Git operation, an `ssh-add` request, unknown context,
  and a legacy prompt-only request.
- Long prompt and key path, Unicode, and a process chain with expandable details.
- Readable layout at the current display scale and on a constrained display area.
- Retained input when details expand, initial focus, Enter, and Escape.
- Busy rejection, requester disconnect, and no context or typed-secret carryover
  into a subsequent request.

Do not install the new live askpass executable as part of automated tests.
Installation and rollback verification occur only when implementation and live
installation are explicitly requested.

## Out of scope

- Implementing or installing the feature while writing this specification.
- Rewriting the monitor helpers or removing Python from unrelated tools.
- Additional daemons, polling, persistent request logs, analytics, or history.
- Wrappers or integrations in individual applications to supply their own reasons.
- Mapping a generic runtime to a desktop app through window titles or focus.
- Complete interpretation of every SSH or Git command-line/configuration form.
- Determining why an application scheduled a background operation internally.
- Changing SSH agent caching, key management, host-key policy, or account security.
- Adding confirmation, notification, or hardware-token interfaces.
- Repository-wide build infrastructure, CI, tracker setup, or unrelated refactors.

## Further notes

Confirmed decisions are C for the bridge, libc-only runtime dependencies, the
existing Quickshell UI, automatic caller context, explicit unknown fallback,
automatic bridge tests, and verification of the real dialog with dummy requests.
The concrete metadata fields, bounds, and operation allowlist above are proposed
implementation defaults for review, rather than decisions the user separately
specified.

This is a new personal repository without a configured issue tracker, glossary,
ADRs, or a test framework. Keep this specification as local Markdown. Do not
create external tasks, select a tracker, commit, or push it implicitly.

Process ancestry describes what can be observed locally. It is not proof of an
application's intent or an authenticated identity. Metadata supplied to the UI
must not be presented as a security guarantee.

The repository's helper scripts are currently snapshots of installed copies.
Implementation must update the documented build/install instructions so the C
binary is deployed deliberately and the previous installed bridge can be restored.

Reference behavior was checked against the installed bridge and QML plugin.
OpenSSH's [askpass implementation](https://github.com/openssh/openssh-portable/blob/master/readpass.c)
passes prompt text to the helper and uses its stdout as the response. Its
[authentication implementation](https://github.com/openssh/openssh-portable/blob/master/sshconnect2.c)
constructs distinct local-key passphrase and remote-account password prompts.
