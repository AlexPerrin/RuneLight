---
name: qa-client
description: "The client reviewer's aggregate for a release, and the dev's self-check for a client change: device regression, network QA, native claims, the single APK, parity with desktop (A/B). Judges the contract row state-doc and the release rows."
---
# QA, client (judgment; the device bring-up and the probe are scripts)

Facts: `tools/track.sh up <slug>` (emulator, install after the repack-log check, login, devtools port), `log`,
`measure` (the API probe report), the `osrs-devtools-mobile` data tools; `reverse` (client repo skill) for native claims.

## A client change (dev, before hand-over)
- The changed plugin or host path shown working on the device: a data-tool dump or a logcat extract, never a
  screenshot unless the claim is about drawing.
- **state-doc**: `docs/client-state.md` rewritten from the probe report, not by hand.
- **A/B** when the change claims parity with desktop: both clients in the same state by the same commands, stepped in
  lockstep; per step the plugin's widgets, varps and highlight targets; a screenshot only for a drawing difference.

## A release (qa)
1. Device regression: core and pinned hub plugins, scripted flows where they exist.
2. Network QA (`network-qa`), sign-in by LOCAL_AUTH, the privacy patches in the repack log.
3. Native claims (offsets, patched bytes, rewrite match counts) checked with `reverse`.
4. The single universal APK: one install on a device without the official app; hooks load, host starts; notices
   embedded equal to the committed file; `apksigner verify`, `zipalign -c`; arm64-v8a only; no secrets or personal data.
5. Release notes by `pr-text`'s claims rule.
6. `osrs-runelite.apk`, `THIRD_PARTY_NOTICES.md` and the repack log copied into the evidence dir for the release.
Emulator quirks that stay judgment: Chrome's local-network grant, the OneTrust dialog, injected events refused
(use `adb shell input`), idle logout at 5 min, the server holding a session after a force-stop.
