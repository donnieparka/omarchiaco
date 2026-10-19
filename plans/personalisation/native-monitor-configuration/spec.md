# Native Hyprland monitor configuration

Status: draft

Readiness: testing boundary confirmed; issue tracker not selected. The replacement test adapters and native-compositor coverage remain to be implemented.

Repository baseline: `a62cdd1b` on `feature/personalisation`, with existing uncommitted monitor work. This specification describes the agreed target architecture, not the behavior currently installed. Writing this specification does not install or activate it.

## Problem Statement

The display panel, its custom monitor engine, and existing session scripts can each influence monitor activation. A separate preferences store can disagree with native Hyprland configuration and with manual edits. Failed activation can become a saved disabled preference, and competing recovery mechanisms make it difficult to explain why a monitor appeared or disappeared.

The user wants predictable monitor settings with less machinery. They want to keep the existing controls, relative placement, geometry reflow, and automatic rollback while letting Hyprland handle display detection and configuration. They also need to distinguish two physical monitors of the same model and inspect configuration errors without delaying a display connection.

The original incident included a display absent from kernel detection before it later appeared. Its hardware cause remains unproven. Simplifying the application must not be described as a verified repair for that incident.

## Solution

Use native Hyprland monitor rules as the single saved configuration. The existing display popup reads those rules and the current observed state, then invokes brief operations to apply user changes. Manual edits remain authoritative.

Hyprland handles connection, activation, and normal recovery. Session events update the tray, assess temporary laptop recovery, and launch advisory validation after every monitor connection. Validation runs independently of activation and leaves the user free to fix findings later. The tray also exposes manual validation and "Retry displays".

Keep the current placement and geometry behavior. A provisional panel change receives the existing independent confirmation dialog and a 15-second rollback deadline. Removing the custom reconciliation engine does not remove these controls or their confirmation behavior.

## User Stories

1. As a desktop user, I want the display icon to open the existing popup, so that I can change monitor settings from the bar.
2. As a desktop user, I want separate position, rotation, video mode, scale, and activation controls, so that I can change individual settings directly.
3. As a desktop user, I want the panel to show the current observed state, so that a failed activation is distinguishable from my saved preference.
4. As a user who edits configuration manually, I want native monitor rules to be authoritative, so that the panel cannot restore older preferences over my edits.
5. As a user who edits configuration manually, I want the panel to reflect a configuration reload, so that its controls match my changes.
6. As a desktop user, I want one saved source of monitor settings, so that I do not have to reconcile a preferences database with Hyprland rules.
7. As a user with two monitors of the same model, I want to configure each physical instance separately, so that their settings do not collide.
8. As a user who changes cables or ports, I want an identifiable physical monitor to retain its settings, so that its configuration follows the monitor.
9. As a user with indistinguishable monitor descriptions, I want to explicitly choose connector-based identity, so that I can still configure the connected screens separately.
10. As a user who disconnects a monitor, I want its native rule to remain saved, so that its settings are available when it returns.
11. As a user connecting an unknown monitor, I want Hyprland to use its preferred mode, scale 1, and automatic placement, so that I can start using it immediately.
12. As a user connecting an unknown monitor, I want a specific rule saved when I deliberately configure it, so that a mere connection does not create an unnecessary profile.
13. As a user arranging several screens, I want to choose a reference monitor, side, and alignment, so that I can place a screen precisely.
14. As a user arranging screens, I want relative placement to calculate saved coordinates once, so that my screens do not acquire persistent dependencies.
15. As a user changing scale, resolution, or rotation from the panel, I want nearby screens to retain their sides, alignments, gaps, and offsets, so that the arrangement remains usable.
16. As a laptop user changing geometry, I want the active laptop screen to remain anchored, so that the main desktop reference stays in place.
17. As a user without an active laptop screen, I want the screen nearest the origin to remain anchored, so that geometry changes have a predictable reference.
18. As a user disconnecting or disabling a screen, I want other screens to keep their positions, so that the desktop does not rearrange itself.
19. As a user reconnecting a configured screen, I want its saved coordinates restored, so that I recover my arrangement.
20. As a user applying a layout from the panel, I want an overlapping result rejected as a whole, so that I do not receive a partially applied arrangement.
21. As a user selecting a video mode, I want resolution and refresh rate treated together, so that a refresh-rate-only change is represented correctly.
22. As a user choosing scale, I want the existing compatible preset behavior preserved, so that a selected preset does not silently become a different scale.
23. As a user making a provisional change, I want a separate centered Keep/Revert dialog, so that I can assess the resulting display setup.
24. As a user making a provisional change, I want 15 seconds to keep it, so that an unusable setup automatically returns to the previous settings.
25. As a user who closes the panel or disables its host screen, I want rollback to remain active, so that losing the panel does not strand me with the change.
26. As a user who explicitly keeps a change, I want the resulting settings saved in native configuration, so that they survive reconnection and future sessions.
27. As a laptop user who loses every usable external display, I want the laptop screen temporarily enabled, so that I retain a usable desktop.
28. As a laptop user relying on temporary recovery, I want my saved activation preference preserved, so that recovery does not permanently change my setup.
29. As a laptop user reconnecting a usable external display, I want my saved laptop preference restored, so that temporary recovery ends automatically.
30. As a laptop user opening or closing the lid, I want lid handling and recovery to share one activation policy, so that session scripts do not fight each other.
31. As a user connecting a monitor, I want validation to run in the background every time, so that configuration mistakes are surfaced without delaying activation.
32. As a user receiving validation findings, I want readable explanations without automatic changes, so that I can decide whether and when to fix them.
33. As a user who edits configuration manually, I want to request validation myself, so that I can check the agreed schema without reconnecting a monitor.
34. As a user who intentionally chose a connector fallback, I want validation to recognize that exception, so that it does not report my deliberate choice as an accident.
35. As a user whose display has not recovered, I want "Retry displays" in the tray popup, so that I can request another application of the configuration.
36. As a desktop user, I want the tray to react to session events and inspect the initial state at startup, so that it reflects monitors already connected or newly changed.
37. As a desktop user, I want idle monitor management to avoid periodic preference reconciliation and repeated reloads, so that the session stays quiet when nothing changes.
38. As a user encountering a driver failure, I want the failure to remain separate from my activation preference, so that recovery does not require undoing a preference I never chose.

## Implementation Decisions

- Native Hyprland configuration is the sole persistent source of monitor preferences. Remove the custom C reconciliation engine, its continuous reconciliation service, and its separate preferences database. Temporary transaction snapshots and rollback metadata are permitted; they are not a competing preferences store.
- Retain the existing QML display popup and its user-facing controls. Replace its dependency on engine-owned preferences with native configuration and observed compositor state. Short-lived operations perform explicit edits, placement calculations, retries, and validation. Their implementation language is not prescribed by this specification.
- Follow a documented schema of literal native monitor rules. Each configured physical monitor has a selector, video mode, position, scale, transform, and activation value. Rules for disconnected monitors remain in the configuration. Preserve unrelated configuration when updating a rule.
- Identify physical instances by the native description selector using the full description including serial. Do not group settings by model alone. Use a connector selector only as an explicit fallback when available descriptions cannot distinguish the instances; mark the intentional fallback with the agreed connector-fallback comment so validation can distinguish it from a mistake.
- Use the native generic fallback for unconfigured monitors: preferred mode, scale 1, and automatic position. Create a specific saved rule on an explicit panel edit or manual configuration edit, not on connection alone.
- Assume schema-compliant manual edits. Do not build support for arbitrary Lua programs, runtime schema enforcement, automatic correction, or conflict merging between manual edits and a pending panel transaction. Simultaneous manual edits during a provisional change are outside the supported workflow.
- Keep saved configuration and observed state distinct. Inventory must represent connected outputs even when disabled or temporarily without a usable video mode, and include identity, resolution, refresh rate, scale, transform, position, activation state, and available video modes. Driver failure never writes a new disabled preference.
- Relative placement computes absolute coordinates once. Panel changes to scale, resolution, or rotation invoke the existing geometry policy using the arrangement observed before the command and the effective scale after normalization. Preserve sides, alignments, gaps, and offsets; anchor the active laptop, otherwise the monitor nearest the origin. For ambiguous arrangements, use the nearest neighbor and nearest edge or center alignment, preferring center on a tie. Reject an overlapping result entirely.
- Disconnecting or disabling a screen does not reposition other screens. Manual native configuration changes are reflected without running a second geometry reflow. Preserve existing mode selection and compatible scale preset behavior, including the representation of a non-preset current scale.
- A panel change is provisional until kept. Retain the independent centered confirmation dialog, explicit revert, Escape behavior, and automatic rollback after 15 seconds. A short-lived helper owns the pending transaction and deadline independently of the panel and configuration reloads. Closing the panel or losing its host screen does not cancel rollback. A successful keep commits native rules; a revert restores the previous saved configuration and requests the corresponding display state. Configuration rollback cannot guarantee repair of a driver failure.
- If no usable external screen remains, temporarily enable the laptop screen without changing its saved preference. An external screen is usable only when it is active with a valid nonzero video mode. Restore the saved laptop preference when a usable external screen returns. Lid handling and recovery share one owner for activation decisions. Remove or adapt legacy toggles and startup recovery that would otherwise rewrite preferences or compete with this policy.
- At session startup, read the current state once. Monitor connection events refresh the tray, assess recovery, and launch advisory validation. Monitor removal and layout changes refresh observed state and assess recovery. Configuration reloads refresh the panel and reassess temporary recovery. Lid switch notifications enter the same activation policy. An explicit resume notification is optional and would come from session power management rather than a Hyprland monitor hook.
- Event handlers inspect current state and tolerate duplicate notifications. They apply only necessary runtime changes, avoid feedback loops, and return promptly. Assign one owner to each behavior. Existing compositor and shell event facilities provide the subscriptions; do not introduce a second always-running custom monitor engine.
- Run advisory schema validation after every distinct monitor connection and on manual request. Launch it outside the compositor callback. Display activation and tray recovery never wait for validation completion or success. Findings identify the offending rule and violated convention, leave settings unchanged, and allow the user to defer action. An explicitly marked connector fallback is valid. Repeated notifications for the same connection must not create duplicate work or a reload loop; a later reconnection must trigger validation again.
- "Retry displays" requests that Hyprland reapply native configuration and refreshes the observed result. It does not edit saved preferences, depend on successful validation, or start an indefinite retry loop. It cannot force the driver to detect absent hardware.
- Migration must preserve authoritative native rules and explicitly configured disabled preferences, retain disconnected rules, and stop competing legacy writers. Old model-only profiles must not silently overwrite native per-instance rules or be ambiguously assigned to identical monitors. Build and verification remain separate from installing or activating the replacement on the live session.

## Testing Decisions

The proposed main seam is the monitor operations used by the popup, exercised as real processes against the existing private simulated-Hyprland environment. Drive native configuration edits, compositor inventory, session notifications, confirmation responses, and validator completion from the fixture. Observe resulting native rules, reported state, command effects, validation findings, and rollback behavior. Adapt the existing fixture rather than introduce another simulator or test the removed engine's internal state.

Good tests assert externally observable outcomes and ordering. A blocked validator can prove that connection handling proceeds before validation finishes, without relying on a fragile elapsed-time threshold. Avoid assertions about helper language, private function names, process internals, or obsolete preferences files.

The user confirmed this testing boundary and asked whether it remains feasible after removing the engine. The boundary is the short-lived operations and their observable results, not the old C executable. The existing simulator requires adaptation for the native configuration operations and chosen event interface; its current implementation cannot be reused unchanged.

Cover the following acceptance scenarios through that main boundary:

1. A manual edit followed by reload appears in reported state and survives subsequent events and panel reads. A refresh-rate-only edit is represented correctly.
2. Two instances of one model with distinct serials retain independent settings across port changes. Explicit connector fallback is recognized when descriptions are indistinguishable.
3. A disconnected configured rule remains saved. Reconnection restores its settings without moving other screens.
4. An unknown monitor receives native defaults without creating a specific saved profile. A deliberate edit creates that monitor's rule while preserving unrelated and disconnected rules.
5. Relative placement and geometry changes preserve the established anchor, sides, alignments, gaps, offsets, and tie-breaking behavior across multiple screens, rotations, and scales. Overlap rejection leaves the arrangement unchanged.
6. Mode choices preserve refresh rate, compatible scale presets retain their selected values, and a non-preset current scale remains accurately represented.
7. Keep commits the proposed native settings. Explicit revert, Escape, and timeout restore the previous settings. Panel closure, loss of the host screen, and a configuration reload do not cancel the independent deadline.
8. A driver activation failure does not persist disabled intent. An external output with zero dimensions or no usable mode does not suppress laptop recovery.
9. Losing the last usable external screen temporarily activates the laptop while its saved disabled preference remains unchanged. A returning usable external screen restores that preference. Other screens retain their positions.
10. Lid notifications, configuration reload, and initial startup state use the same recovery policy without competing writers or repeated activation loops.
11. Every new monitor connection starts advisory validation, including reconnection of a previously known instance. Duplicate notifications from one connection do not create repeated jobs. Connection handling proceeds while the validator is blocked and after it reports errors or fails.
12. Validation reports a selector that violates the schema, accepts an intentional connector fallback, and leaves configuration and activation unchanged. Manual validation uses the same rules.
13. Manual retry requests a fresh application of configuration without changing preferences or starting an autonomous reload loop. Repeat requests remain possible after a failed attempt.
14. Idle sessions and unrelated events produce no periodic preference reconciliation or display reloads. Runtime recovery changes do not cause an event feedback loop.
15. Migration preserves authoritative native configuration and disconnected rules, and leaves only one activation owner when legacy services, toggles, and hooks are present.

Prior art is the existing process-level monitor integration fixture, the geometry/reflow scenarios, and the shell-side monitor control and scale tests. Reuse behavior that remains valid. Retire assertions requiring model-only identity, rejecting identical models, separate profile database writes, periodic imports, daemon backstops, automatic retries, or daemon-based watchdog recovery.

Use narrow geometry tests only where they explain boundary cases more clearly than process-level assertions. Complement simulated integration tests with real Hyprland checks in the repository's disposable desktop acceptance environment. Verify that native rules are accepted, manual reloads take effect, chosen session hooks actually run, and temporary runtime activation leaves saved preferences unchanged. Check that the rollback helper survives a real configuration reload. The simulated fixture proves how our code reacts to supplied events; it does not prove that the compositor emits those events or interprets its rules as the fixture assumes.

In the same disposable environment, verify the actual popup, centered confirmation, findings presentation, and retry action. Multiple-output compositor checks depend on the display capabilities of the test environment and must not be reported as verified from a single-output VM alone. Physical cable, lid, and adapter behavior requires a later controlled hardware check; a simulator cannot establish driver reliability. Writing this specification does not run display-changing tests on the active desktop.

## Out of Scope

- Implementing, installing, or activating the redesign while preparing this specification.
- Repairing USB, cable, adapter, kernel, driver, or compositor detection failures, or claiming a proven cause for the original intermittent incident.
- A continuous custom reconciliation engine, periodic preference imports, automatic reload retry loops, or a second persistent preferences store.
- Arbitrary manually authored Lua expressions and programs, blocking schema enforcement, and automatic configuration corrections.
- Merging simultaneous manual configuration edits with a pending panel transaction.
- Persistent placement dependencies, automatic compaction on disconnect, and changing other screens' positions merely because a monitor is disabled.
- Redesigning the display controls, unrelated desktop customizations, or rewriting unrelated native utilities.
- A mandatory resume hook or a guarantee that every low-level display state transition emits a public session event.
- Selecting a tracker, creating external records, committing, pushing, or treating this draft as published without the required workflow inputs.

## Further Notes

The agreed architectural requirements are recorded in [ADR 0001](../../../docs/personalisation/adr/0001-display-c-qml.md), [ADR 0002](../../../docs/personalisation/adr/0002-independent-monitor-state.md), [ADR 0003](../../../docs/personalisation/adr/0003-geometry-reflow.md), and [ADR 0004](../../../docs/personalisation/adr/0004-monitor-configuration-authority.md). Domain terms follow the [monitor glossary](../../../docs/personalisation/GLOSSARY.md).

Publication is pending because the repository has no issue-tracker selection document. Run `/setup-westsite-skills` to select the tracker. If local Markdown is explicitly selected, publish this specification at the local tracker convention's destination and set its status to ready-for-agent. The testing boundary is confirmed. This planning file is a draft, not a tracker record.

The specification intentionally leaves helper language, private APIs, exact user-interface wording for findings, and migration mechanics to implementation. These choices must satisfy the stated authority, identity, nonblocking validation, and rollback requirements. Existing uncommitted monitor work must be reviewed against these ADRs; its presence is not evidence that the new architecture has been implemented or verified.
