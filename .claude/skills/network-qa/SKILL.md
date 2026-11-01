---
name: network-qa
description: "Judge a client build's network behaviour: the sampled sockets and DNS of a play sequence against the three allowed hosts, and the repack log. Every release and every change with network: true. Judges the contract row network-qa."
---
# Network QA (judgment; the sampling is a script)

Facts: `tools/track.sh netqa <slug>` runs the sequence (sign out, LOCAL_AUTH sign in, play, `::drop` relog,
force-stop and relaunch, play) while sampling the app's ESTABLISHED sockets every 0.5 s and collecting DNS lookups,
and prints the distinct remote addresses and names; `check-log` checks the repack log.

## Judgment
- Allowed, exactly: the test server (10.0.2.2:43594, :8080), `api.runelite.net` and `static.runelite.net` (plugin
  data); nothing else.
- Any other host is a FAIL: stop and report at once. Telemetry above all: Braze, OneTrust, Google (analytics,
  Firebase, FCM), `app-measurement.com`, `singular.net`, any `jagex.com` or `runescape.com` host, `*.x.invalid`,
  ConsentSync or Singular log lines.
## Report
`evidence/<slug>/netqa-<ts>.md`: the build, the window, the DNS list, the distinct addresses, PASS or FAIL; a dated
heading in the client repo's `docs/app-telemetry.md`.
