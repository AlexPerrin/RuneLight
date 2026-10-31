#!/usr/bin/env bash
# tools/track.sh <verb> <slug> [args]: the client track's scripts (docs/ARCHITECTURE.md). The device side runs on
# Windows through tools/up.ps1 in one interop call. Env from hd: SLUG HEAD T TCOMMIT BASE CLONE EVIDENCE WORK.
# Artefacts named by the change: ~/src/work/<slug>-build, the APK evidence/<slug>/<T>.apk and its repack log, the
# emulator Agent_Phone instance owned by <slug>.
set -euo pipefail
verb=$1; slug=$2; shift 2

build()          { :; }   # R8: build copy at TCOMMIT; host build, ninja, repack with OPENRUNE=1 LOCAL_AUTH=1 (never a plain
                          #     repack); the repack log checked (7 telemetry lines, RSA modulus, jav_config, appauth) before the
                          #     APK is kept; cached when evidence/<slug>/<T>.apk exists; writes state/<slug>/build.json.
up()             { :; }   # tools/up.ps1: boot the emulator (sys.boot_completed), install evidence/<slug>/<T>.apk after the
                          #     log check, grant Chrome local network, back up the device state, enable devtools, log in by widget
                          #     actions; prints `LOGGED_IN <serial> <devtools port>`.
down()           { :; }   # restore the device state (checked), stop the emulator.
gate()           { :; }   # R3: every check row with the env; writes state/<slug>/gate.json {T, ok, detail}.
check_log()      { :; }   # the repack log rows; check_evidence, check_probe (report built at HEAD, docs-only drift allowed),
check_evidence() { :; }   #     check_netqa (netqa*.md PASS when the change says network) likewise: exit 0 or 1 with the reason.
check_probe()    { :; }
check_netqa()    { :; }
ship()           { :; }   # pushes TCOMMIT (the merge of base and head) to origin main: the merge and the push in one fact.
measure()        { :; }   # R1: build and up at the target, run the API probe, write state/<goal>/measure.json {tree, value, report}.
log()            { :; }   # logcat since the run's marker, filtered by pattern (collected on the device, read with exec-out).

case $verb in build|up|down|gate|check-log|check-evidence|check-probe|check-netqa|ship|measure|log) ${verb//-/_} "$@" ;;
  *) echo "verbs: build up down gate check-log check-evidence check-probe check-netqa ship measure log" >&2; exit 2 ;; esac
