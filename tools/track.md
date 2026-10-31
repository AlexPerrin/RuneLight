# Client track

RuneLite plugins in the official OSRS Android app (`native/`, `host/`, APK repack) in `~/osrs-plugins-reverse`,
tested on the Agent_Phone emulator against the test server. `hd` is `python3 ~/osrs-harness/hd.py`; the track
script is `tools/track.sh <verb> <slug>`; the evidence dir is `evidence/<slug>/`. Reference in the client repo:
`docs/app-telemetry.md` (read in full before any build that touches networking), `docs/plugin-host.md`,
`docs/offsets.md`.

## The dev
Clone `~/src/work/<slug>` on branch `client/<slug>`. Analyse with Ghidra on `re/ghidra` (`-readOnly` unless the task
updates the project); ids and game data from `osrs-mcp`; what the server sends through `tracks/server.sh log`.
`tools/track.sh build <slug>` runs the host build, ninja and the repack with `OPENRUNE=1 LOCAL_AUTH=1` in the
build copy and checks the repack log before anything is installable; `tools/track.sh up <slug>` boots your own
emulator, installs the APK, logs in and prints the devtools port; `down` restores and stops it; `log <slug>
<pattern>` reads logcat since the run's marker. Test through `osrs-devtools-mobile` data tools; `adb shell input`
only where the emulator refuses injected events. Blocked by the server: file the blocker as a server change with
the repro, as a dep of yours; never work around it. Push `origin/client/<slug>`, then `hd handover <slug>`.

## The reviewer (releases only)
Full QA on the client's live tip at the exact commit, every finding at once: device regression of core and pinned
hub plugins, network QA, sign-in, the privacy patches in the repack log, native claims against Ghidra, the release
notes, the single universal APK (`apksigner verify`, `zipalign -c`, arm64-v8a only, the notices embedded equal to the
committed file), no secrets or personal data. Copy `osrs-runelite.apk`, `THIRD_PARTY_NOTICES.md` and the repack log
into the evidence dir. Releases are `vX.Y.Z`; autonomous builds are 0.1.x; a major version needs the user.

## Network QA (every release, every change with `network: true`)
Sample the app's ESTABLISHED sockets every 0.5 s through the sign-out, LOCAL_AUTH sign-in, play, `::drop` relog and
restart sequence; DNS from logcat's "asked for" lines. Allowed: 10.0.2.2:43594 and :8080, api/static.runelite.net,
sdk.fra-01.braze.eu, the onetrust hosts, one Google 443 (FCM). Must not appear: any `jagex.com`, `runescape.com`,
`singular.net`, `app-measurement.com`, `*.x.invalid`, ConsentSync or Singular log lines. Report as `netqa*.md`.

## Never
Connect anything to Jagex or RuneScape servers (a new outbound host: stop and report at once); restart or edit the
server; touch another agent's emulator or Medium_Phone (it has the official app); read `C:\Users\Alex\.runelite`;
send keyevent BACK in game; push any ref but `client/<slug>`; tag, release or deploy.

## Seed rows
- repack-log | the repack log has the 7 telemetry lines, patched RSA modulus, patched /jav_config.ws, the appauth line | check: tools/track.sh check-log
- device-evidence | a logcat extract, dump or devtools JSON in the evidence dir | check: tools/track.sh check-evidence
- probe | the probe report is built at HEAD (docs-only differences allowed) | check: tools/track.sh check-probe
- network-qa | netqa*.md present and PASS when the change has network: true | check: tools/track.sh check-netqa
- state-doc | docs/client-state.md rewritten from the probe report | gate: tools/track.sh check-state-doc
