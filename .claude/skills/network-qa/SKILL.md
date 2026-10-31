---
name: network-qa
description: "Judge a client build's network behaviour: the sampled sockets and DNS of a play sequence against the allowed hosts and the must-not-appear list, and the repack log. Every release and every change with network: true. Judges the contract row network-qa."
---
# Network QA (judgment; the sampling is a script)

Facts: `tools/track.sh netqa <slug>` runs the sequence (sign out, LOCAL_AUTH sign in, play, `::drop` relog,
force-stop and relaunch, play) while sampling the app's ESTABLISHED sockets every 0.5 s and collecting DNS lookups,
and prints the distinct remote addresses and names; `check-log` checks the repack log.

## Judgment
- Allowed: the test server (10.0.2.2:43594, :8080), api/static.runelite.net, the Braze and OneTrust hosts, one
  Google 443 (FCM), the system's private DNS. The list lives in the client repo's `docs/app-telemetry.md`.
- Must not appear, stop and report at once: any `jagex.com` or `runescape.com` host, `singular.net`,
  `app-measurement.com`, `*.x.invalid`, ConsentSync or Singular log lines.
- A host on neither list: the row fails and the change stops until the user decides.
## Report
`evidence/<slug>/netqa-<ts>.md`: the build, the window, the DNS list, the distinct addresses, PASS or FAIL; a dated
heading in the client repo's `docs/app-telemetry.md`.
