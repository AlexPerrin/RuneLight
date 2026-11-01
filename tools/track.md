# Client track

RuneLite plugins in the official OSRS Android app (`native/`, `host/`, APK repack) in `~/osrs-plugins-reverse`,
tested on the Agent_Phone emulator against the test server. `hd` is `python3 ~/osrs-harness/hd.py`; the track
script is `tools/track.sh <verb> <slug>`; the evidence dir is `evidence/<slug>/`. Reference in the client repo:
`docs/app-telemetry.md` (read in full before any build that touches networking), `docs/plugin-host.md`,
`docs/offsets.md`.

## The dev
Clone `~/src/work/<slug>` on branch `client/<slug>`. Analyse with Ghidra on `re/ghidra` (`-readOnly` unless the task
updates the project); ids and game data from `osrs-mcp`; what the server sends through `tracks/server.sh log`.
`tools/track.sh build <slug>` runs the devtools plugin build (its own re/devtools), the host build, ninja and the repack with `OPENRUNE=1 LOCAL_AUTH=1` in the
build copy and checks the repack log before anything is installable; `tools/track.sh up <slug>` boots your own
emulator on the network allowlist (checked), installs the APK and pushes the devtools jar built with it, logs in and prints the devtools port; `down` restores and stops it; `log <slug>
<pattern>` reads logcat since the run's marker. Test through `osrs-devtools-mobile` data tools; `adb shell input`
only where the emulator refuses injected events. Blocked by the server: file the blocker as a server change with
the repro, as a dep of yours; never work around it. Push `origin/client/<slug>`, then `hd handover <slug>`.

The mobile side is Windows programs run from bash through WSL interop by absolute path, located by `ANDROID_SDK`
(default `/mnt/c/Users/Alex/AppData/Local/Android/Sdk`: adb.exe, emulator.exe) and `WINDOWS_SYSTEM32` (default
`/mnt/c/Windows/System32`: netstat.exe); a missing program fails naming its variable. The emulator runs with no
console window, its log in `state/<slug>/emulator.log`; `down` is the only stop. `DOCKER` (default `docker`) runs the
allowlist container `mobile-allow`, one for every mobile, left running.

## The reviewer (releases only)
Full QA on the client's live tip at the exact commit, every finding at once: mobile regression of core and pinned
hub plugins, network QA, sign-in, the privacy patches in the repack log, native claims against Ghidra, the release
notes, the single universal APK (`apksigner verify`, `zipalign -c`, arm64-v8a only, the notices embedded equal to the
committed file), no secrets or personal data. Copy `osrs-runelite.apk`, `THIRD_PARTY_NOTICES.md` and the repack log
into the evidence dir. Releases are `vX.Y.Z`; autonomous builds are 0.1.x; a major version needs the user.

## Network QA (every release, every change with `network: true`)
Sample the app's ESTABLISHED sockets every 0.5 s through the sign-out, LOCAL_AUTH sign-in, play, `::drop` relog and
restart sequence; DNS from logcat's "asked for" lines. Allowed sockets: 10.0.2.2:43594, :8080 and :443 only. The network allowlist: `up` starts the container `mobile-allow`
(`tools/allow.py` on the Windows loopback, udp 53 and tcp 443) and the emulator with `-dns-server 127.0.0.1`, and turns
the mobile's wifi off (that path, through netsimd, resolves with Windows' DNS); the cellular path asks `mobile-allow`,
which answers `api.runelite.net` and `static.runelite.net` with 10.0.2.2 and every other name NXDOMAIN, and relays a TLS
connection on 10.0.2.2:443 to its SNI name only when that name is one of the two. The Windows Firewall rules of group
`osrs-harness` block the emulator's qemu-system-x86_64.exe and netsimd.exe from every address but loopback and the LAN.
`check-blocked` (run by `up`) proves both halves: each allowed host answers a TLS ClientHello, and the Jagex, RuneScape,
Braze, OneTrust, Google analytics/Firebase, Singular and Google hosts do not resolve and no public address connects.
Must not appear, even as a lookup: any `jagex.com`, `runescape.com`,
`singular.net`, `app-measurement.com`, `*.x.invalid`, ConsentSync or Singular log lines. First the jav_config the app loads names
local hosts only: `python3 ~/src/OpenRune-Server/.worktrees/harness/tools/server/javcheck.py http://127.0.0.1:8080/jav_config.ws 10.0.2.2`
prints `jav_config local only`. Report as `netqa*.md`.

## Never
Connect anything to Jagex or RuneScape servers (a new outbound host: stop and report at once); restart or edit the
server; touch another agent's mobile or Medium_Phone (it has the official app); read `C:\Users\Alex\.runelite`;
send keyevent BACK in game; push any ref but `client/<slug>`; tag, release or deploy.

## Seed rows
- repack-log | the repack log has the 7 telemetry lines, patched RSA modulus, patched /jav_config.ws, the appauth line | check: tools/track.sh check-log
- mobile-evidence | a logcat extract, dump or devtools JSON in the evidence dir | check: tools/track.sh check-evidence
- probe | the probe report is built at HEAD (docs-only differences allowed) | check: tools/track.sh check-probe
- network-qa | netqa*.md present and PASS when the change has network: true | check: tools/track.sh check-netqa
- state-doc | docs/client-state.md rewritten from the probe report | gate: tools/track.sh check-state-doc
