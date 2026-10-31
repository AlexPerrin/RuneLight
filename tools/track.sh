#!/usr/bin/env bash
# tools/track.sh <verb> <slug> [arg]: the client track (docs/ARCHITECTURE.md of osrs-harness). Env from hd: SLUG HEAD T
# TCOMMIT BASE CLONE EVIDENCE HD_ROOT. The device side runs on Windows through tools/up.ps1. Verbs:
#   build [TCOMMIT]   host build, ninja and the repack with OPENRUNE=1 LOCAL_AUTH=1 in the build copy; the APK is kept only
#                     when its repack log passes check-log; cached by T (evidence/<slug>/<T>.apk)
#   up                boots the change's emulator, installs the APK, logs in; prints LOGGED_IN <serial> <devtools port>
#   down              restores the device state (checked) and stops the emulator
#   gate TCOMMIT      every check: and gate: row of the contract from the build copy (`hd rows`)
#   check-log | check-evidence | check-probe | check-netqa | check-state-doc   the seed rows
#   ship TCOMMIT      pushes the merge commit of base and head to origin main
#   measure GOAL      build and up at the target, the API probe; state/<goal>/measure.json
#   log PATTERN       logcat since the run's marker, filtered
#   netqa start|stop  samples the app's sockets and DNS between the two calls
set -euo pipefail
verb=$1; slug=${2:-}; arg=${3:-}
self=$(cd "$(dirname "$0")" && pwd); repo=$(dirname "$self")
root=${HD_ROOT:-$HOME/osrs-harness}; state=$root/state/$slug; mkdir -p "$state"
ev=${EVIDENCE:-$root/evidence/$slug}; mkdir -p "$ev"
dev=$repo/.worktrees/$slug; copy=$repo/.worktrees/$slug-build
adb=${ADB:-/mnt/c/Users/Alex/AppData/Local/Android/Sdk/platform-tools/adb.exe}
ndk=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/android-ndk-r27d}
record() { python3 -c 'import json,sys;print(json.dumps({"T":sys.argv[1],"ok":sys.argv[2]=="true","detail":sys.stdin.read()[-2000:]}))' "$T" "$1" > "$state/$2.json"; }
serial() { cat "$state/serial" 2>/dev/null || { echo "no device for $slug: run up"; return 1; }; }
win() { wslpath -w "$1"; }

tree() {
  if [ -n "$arg" ]; then tc=$arg; T=${T:-$(git -C "$repo" rev-parse "$tc^{tree}")}; return; fi
  idx=$(git -C "$dev" rev-parse --git-path snapshot-index); rm -f "$idx"
  GIT_INDEX_FILE=$idx git -C "$dev" add -A . && snap=$(git -C "$dev" commit-tree "$(GIT_INDEX_FILE=$idx git -C "$dev" write-tree)" -p HEAD -m snapshot)
  read -r tc T < <(HD_ROOT=$root python3 "$root/hd.py" tree "$slug" "$snap")
}
build() {
  tree
  if [ -f "$ev/$T.apk" ]; then echo "cached $ev/$T.apk"; record true build <<< "cached"; return; fi
  if [ -d "$copy" ]; then git -C "$copy" reset -q --hard "$tc" && git -C "$copy" clean -fdq; else git -C "$repo" worktree add -q --detach "$copy" "$tc"; fi
  mkdir -p "$copy/re/host"
  for e in "$repo"/re/*; do n=$(basename "$e"); case $n in host|build|out) ;; *) ln -sfn "$e" "$copy/re/$n" ;; esac; done
  [ -d "$copy/re/host/plugins-extra" ] || cp -a "$repo/re/host/plugins-extra" "$copy/re/host/"
  [ -f "$copy/re/build/build.ninja" ] || cmake -S "$copy/native" -B "$copy/re/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 >/dev/null
  log=$ev/repack-$T.log
  (cd "$copy" && host/build.sh && ninja -C re/build && OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh) > "$log" 2>&1 || { record false build < "$log"; echo "build failed: $log"; return 1; }
  EVIDENCE=$ev check_log >/dev/null || { record false build <<< "repack log fails the privacy check: $log"; rm -f "$copy/re/out/osrs-runelite.apk"; return 1; }
  cp "$copy/re/out/osrs-runelite.apk" "$ev/$T.apk"; record true build < "$log"; echo "built $ev/$T.apk"
}
up() {
  T=${T:-$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["T"])' "$state/build.json")}
  [ -f "$ev/$T.apk" ] || { echo "no APK for $T: build first"; return 1; }
  EVIDENCE=$ev check_log >/dev/null
  out=$(powershell.exe -NoProfile -File "$(win "$self/up.ps1")" -Slug "$slug" -Apk "$(win "$ev/$T.apk")" -Plugins "$(win "$repo/re/host/plugins-extra")" -Backup "$(win "$state")" | tr -d '\r')
  echo "$out" | tail -1 | awk '{print $2}' > "$state/serial"
  ANDROID_SERIAL=$(serial) python3 "$self/device_login.py"
  date -u +%FT%T.000 > "$state/log-mark"
  echo "LOGGED_IN $(serial) 7780"
}
down() {
  s=$(serial) || return 0
  powershell.exe -NoProfile -File "$(win "$self/up.ps1")" -Slug "$slug" -Serial "$s" -Backup "$(win "$state")" -Down | tr -d '\r'
  rm -f "$state/serial"
}
gate() { tc=$arg; build; git -C "$copy" reset -q --hard "$tc"
  if out=$(HD_ROOT=$root python3 "$root/hd.py" rows "$slug" "$tc" "$copy" 2>&1); then record true gate <<< "$out"; else record false gate <<< "$out"; fi; }
check_log() {
  l=$(ls -t "$ev"/repack*.log 2>/dev/null | head -1 || true); [ -n "$l" ] || { echo "no repack log in $ev"; return 1; }
  [ "$(grep -c '^telemetry:' "$l")" = 7 ] && grep -q '^patched RSA modulus' "$l" && grep -q '^patched /jav_config\.ws' "$l" && grep -qE '^patched \S*/net/openid/appauth/' "$l" \
    && echo "repack log: 7 telemetry lines, RSA modulus, jav_config and appauth patched" || { echo "$l: the APK talks to Jagex (OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh)"; return 1; }
}
check_evidence() { for p in "$ev"/*logcat*.txt "$ev"/*.dump.* "$ev"/*devtools*.json; do [ -e "$p" ] && { echo "device evidence: $(basename "$p")"; return 0; }; done; echo "no logcat extract, dump or devtools JSON in $ev"; return 1; }
check_probe() {
  f=$ev/api-probe.tsv; [ -f "$f" ] || { echo "no api-probe.tsv in $ev"; return 1; }
  b=$(grep -oE '^# build: \S*-g[0-9a-f]+' "$f" | grep -oE '[0-9a-f]+$' || true); [ -n "$b" ] || { echo "$f has no '# build:' line"; return 1; }
  case $HEAD in "$b"*) echo "probe built at $HEAD"; return 0 ;; esac
  d=$(git -C "$repo" diff --name-only "$b" "$HEAD" | grep -v '^docs/' || true); [ -z "$d" ] && echo "probe at $b, only docs differ" || { echo "probe built at $b, not $HEAD: rerun it"; return 1; }
}
check_netqa() { grep -qE '^network: true' "$root/harness/changes/$slug.md" 2>/dev/null || { echo "no network QA needed"; return 0; }
  grep -lq 'PASS' "$ev"/netqa*.md 2>/dev/null && echo "network QA PASS" || { echo "network: true and no netqa*.md with PASS in $ev"; return 1; }; }
check_state_doc() { r=$(grep -oE '^# result: pass [0-9]+ / [0-9]+ = [0-9.]+%' "$ev/api-probe.tsv" 2>/dev/null | grep -oE '[0-9.]+%$' || true); [ -n "$r" ] || { echo "no probe result"; return 1; }; grep -q "$r" docs/client-state.md && echo "client-state.md carries the probe's $r" || { echo "docs/client-state.md does not carry the probe's result $r"; return 1; }; }
ship() { git -C "$repo" push -q origin "$arg:refs/heads/main" && echo "main at ${arg:0:9}"; }
measure() {
  slug=$arg; state=$root/state/$arg; ev=$root/evidence/$arg; mkdir -p "$state" "$ev"; copy=$repo/.worktrees/$arg-build
  tc=$(git -C "$repo" rev-parse origin/HEAD); arg=$tc; T=$(git -C "$repo" rev-parse "$tc^{tree}"); build; up
  (cd "$copy" && ANDROID_SERIAL=$(serial) tools/api-probe.sh "$ev")
  v=$(grep -oE '^# result: pass [0-9]+ / [0-9]+ = [0-9.]+%' "$ev/api-probe.tsv" | grep -oE '[0-9.]+%$' | tr -d %)
  python3 -c 'import json,sys;json.dump({"tree":sys.argv[1],"value":float(sys.argv[2]),"report":sys.argv[3]},open(sys.argv[4],"w"))' "${TREE:-$T}" "$v" "$ev/api-probe.tsv" "$state/measure.json"
  down; echo "client-api: $v"
}
log() { "$adb" -s "$(serial)" logcat -d -v threadtime -T "$(cat "$state/log-mark" 2>/dev/null || echo 1)" 2>/dev/null | tr -d '\r' | grep -E "${arg:-.}" || true; }
netqa() {  # start: a sampler on the device; stop: the distinct remote addresses (app uid 10071) and the DNS lookups
  s=$(serial); f=/sdcard/Download/netqa-$slug.txt
  if [ "$arg" = start ]; then "$adb" -s "$s" logcat -c; "$adb" -s "$s" shell "nohup sh -c 'while :; do date +%s; cat /proc/net/tcp /proc/net/tcp6; sleep 0.5; done' > $f 2>/dev/null &"; echo "sampling"
  else "$adb" -s "$s" shell pkill -f "/proc/net/tcp" || true
    "$adb" -s "$s" exec-out cat "$f" | python3 -c '
import sys, socket, struct
seen = set()
for l in sys.stdin:
    p = l.split()
    if len(p) > 7 and p[3] == "01" and p[7] == "10071":
        a, port = p[2].split(":"); ip = socket.inet_ntoa(struct.pack("<I", int(a[-8:], 16))) if len(a) <= 8 else socket.inet_ntop(socket.AF_INET6, b"".join(struct.pack("<I", int(a[i:i+8], 16)) for i in range(0, 32, 8)))
        seen.add(f"{ip}:{int(port, 16)}")
print("\n".join(sorted(seen)))'
    "$adb" -s "$s" shell rm -f "$f"; echo "--- dns"; "$adb" -s "$s" logcat -d | tr -d '\r' | grep -i "asked for" || true
  fi
}

case $verb in build|up|down|gate|check-log|check-evidence|check-probe|check-netqa|check-state-doc|ship|measure|log|netqa) "${verb//-/_}" ;;
  *) echo "verbs: build up down gate check-log check-evidence check-probe check-netqa check-state-doc ship measure log netqa" >&2; exit 2 ;; esac
