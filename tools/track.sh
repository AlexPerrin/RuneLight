#!/usr/bin/env bash
# tools/track.sh <verb> <slug> [arg]: the client track (docs/ARCHITECTURE.md of osrs-harness). Env from hd: SLUG HEAD T
# TCOMMIT BASE CLONE EVIDENCE HD_ROOT. The Windows programs (adb, the emulator) run through WSL interop, by absolute path. Verbs:
#   build [TCOMMIT]   host build, ninja and the repack with OPENRUNE=1 LOCAL_AUTH=1 in the build copy; the APK is kept only
#                     when its repack log passes check-log; cached by T (evidence/<slug>/<T>.apk)
#   up                boots the change's emulator (no console window, its log in state/<slug>/emulator.log) with no
#                     hostname resolving (checked on Jagex, RuneScape, RuneLite and Google hosts), installs the APK, logs
#                     in; prints LOGGED_IN <serial> <devtools port>;
#                     a failed up or measure runs down (state/<slug>/serial is written as the emulator starts)
#   down              removes devtools, clears the debug props, checks the plugins dir is the three defaults; stops the emulator
#   gate TCOMMIT      every check: and gate: row of the contract from the build copy (`hd rows`)
#   check-blocked     the running device resolves none of the Jagex, RuneScape, RuneLite and Google hosts (up runs it)
#   check-log | check-evidence | check-probe | check-netqa | check-state-doc   the seed rows
#   ship TCOMMIT      pushes the merge commit of base and head to origin main
#   measure GOAL [TCOMMIT]  build and up at the target (origin/HEAD by default), the API probe; state/<goal>/measure.json
#   log <slug> PATTERN  logcat since the run's marker, filtered
#   netqa start|stop  samples the app's sockets and DNS between the two calls
# Windows locations, run through WSL interop (override in the environment):
#   ANDROID_SDK       /mnt/c/Users/Alex/AppData/Local/Android/Sdk   platform-tools/adb.exe (ADB overrides adb alone), emulator/emulator.exe
#   WINDOWS_SYSTEM32  /mnt/c/Windows/System32                      netstat.exe
set -euo pipefail
verb=$1; slug=${2:-}; arg=${3:-}
self=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$(git -C "$self" rev-parse --path-format=absolute --git-common-dir)")  # the main checkout (re/ lives there), also when run from a worktree
root=${HD_ROOT:-$HOME/osrs-harness}
[ -f "$root/harness/changes/$slug.md" ] || [ -f "$root/harness/goals/$slug.md" ] || case $slug in live|integration) ;; *) echo "no change or goal \"$slug\": tools/track.sh <verb> <slug> [arg]" >&2; exit 2 ;; esac
state=$root/state/$slug; mkdir -p "$state"
ev=${EVIDENCE:-$root/evidence/$slug}; mkdir -p "$ev"
dev=$repo/.worktrees/$slug; copy=$repo/.worktrees/$slug-build
sdk=${ANDROID_SDK:-/mnt/c/Users/Alex/AppData/Local/Android/Sdk}; sys32=${WINDOWS_SYSTEM32:-/mnt/c/Windows/System32}; adb=${ADB:-$sdk/platform-tools/adb.exe}
need() { [ -x "$1" ] || { echo "$1 not found: set $2" >&2; exit 2; }; }   # need <program> <the variable that locates it>
pkg=com.jagex.oldscape.android; files=/sdcard/Android/data/$pkg/files; defaults="grounditems.jar opponentinfo.jar quest-helper.jar"
hosts="legal.jagex.com auth.jagex.com oldschool.runescape.com www.runescape.com api.runelite.net static.runelite.net google.com connectivitycheck.gstatic.com"
ndk=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/android-ndk-r27d}
record() { python3 -c 'import json,sys;print(json.dumps({"T":sys.argv[1],"ok":sys.argv[2]=="true","detail":sys.stdin.read()[-2000:]}))' "$T" "$1" > "$state/$2.json"; }
serial() { cat "$state/serial" 2>/dev/null || { echo "no device for $slug: run up"; return 1; }; }
win() { wslpath -w "$1"; }
A() { { "$adb" -s "$s" "$@" 2>&1 || true; } | tr -d '\r'; }   # adb on the run's device ($s); callers judge its output
listening() { "$sys32/netstat.exe" -anp TCP | tr -d '\r' | awk '$4=="LISTENING"{n=split($2,a,":");print a[n]}'; }   # the Windows listening ports

tree() {
  if [ -n "$arg" ]; then tc=$arg; T=${T:-$(git -C "$repo" rev-parse "$tc^{tree}")}; return; fi
  [ -d "$dev" ] || git -C "$repo" worktree add -q "$dev" -B "client/$slug" "$(git -C "$repo" rev-parse -q --verify "origin/client/$slug" || echo origin/HEAD)"
  idx=$(git -C "$dev" rev-parse --git-path snapshot-index); rm -f "$idx"
  GIT_INDEX_FILE=$idx git -C "$dev" add -A . && snap=$(git -C "$dev" commit-tree "$(GIT_INDEX_FILE=$idx git -C "$dev" write-tree)" -p HEAD -m snapshot)
  read -r tc T < <(HD_ROOT=$root python3 "$root/hd.py" tree "$slug" "$snap")
}
build() {
  tree
  if [ -f "$ev/$T.apk" ]; then echo "cached $ev/$T.apk"; record true build <<< "cached"; return; fi
  if [ -d "$copy" ]; then git -C "$copy" reset -q --hard "$tc" && git -C "$copy" clean -fdq; else git -C "$repo" worktree add -q --detach "$copy" "$tc"; fi
  mkdir -p "$copy/re/host"; for f in base.apk split_config.arm64_v8a.apk; do ln -sfn "$repo/$f" "$copy/$f"; done  # the ignored store APKs
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
  boot
  ANDROID_SERIAL=$(serial) python3 "$self/device_login.py"
  date -u +%FT%T.000 > "$state/log-mark"
  echo "LOGGED_IN $(serial) 7780"
}
boot() {  # an Agent_Phone emulator on a free even console port, the APK in place of the official app, Chrome granted the
  # local network, devtools and the three default plugins pushed; the serial goes to state/<slug>/serial as the emulator starts
  need "$adb" ANDROID_SDK; need "$sdk/emulator/emulator.exe" ANDROID_SDK; need "$sys32/netstat.exe" WINDOWS_SYSTEM32
  exec 9> "$root/state/emulator-port.lock"; flock 9   # one boot at a time picks a port, until adb sees its emulator there
  port=$(comm -23 <(seq 5560 2 5598 | sort) <(listening | sort -u) | sort -n | head -1); s=emulator-$port; [ -n "$port" ] || { echo "no free even port in 5560-5598" >&2; return 1; }
  (cd /mnt/c && setsid "$sdk/emulator/emulator.exe" -avd Agent_Phone -read-only -gpu swiftshader_indirect -port "$port" < /dev/null > "$state/emulator.log" 2>&1 9>&- &)   # no console window: its log is a file, down is the only stop
  echo "$s" > "$state/serial"
  "$adb" -s "$s" wait-for-device; exec 9>&-
  until [ "$(A shell getprop sys.boot_completed)" = 1 ]; do sleep 2; done
  A uninstall "$pkg" >/dev/null
  r=$(A install -r "$(win "$ev/$T.apk")"); case $r in *Success*) ;; *) echo "install failed: $r" >&2; return 1 ;; esac
  path=$(A shell pm path "$pkg"); path=${path#package:}
  [ "$(A shell "unzip -l $path | grep -c libosrsrl")" = 1 ] || { echo "the APK carries no libosrsrl" >&2; return 1; }
  A shell pm grant com.android.chrome android.permission.ACCESS_LOCAL_NETWORK >/dev/null
  A shell settings put global private_dns_mode hostname >/dev/null   # Private DNS strict on a name that never resolves:
  A shell settings put global private_dns_specifier blocked.invalid >/dev/null   # no hostname resolves; 10.0.2.2 is an address
  blocked >&2 || return 1
  A shell monkey -p "$pkg" -c android.intent.category.LAUNCHER 1 >/dev/null   # a first start creates files/
  until [ "$(A shell "[ -d $files ] && echo y")" = y ]; do sleep 1; done; A shell am force-stop "$pkg" >/dev/null
  A shell mkdir -p "$files/plugins" >/dev/null; A shell chmod 777 "$files/plugins" >/dev/null
  for j in $defaults; do A push "$(win "$repo/re/host/plugins/$j")" "$files/plugins/$j" >/dev/null; done
  [ "$(A shell ls -1 "$files/plugins" | LC_ALL=C sort | paste -sd' ')" = "$defaults" ] || { echo "the three default plugins are not in place" >&2; return 1; }
  A push "$(win "$repo/re/host/plugins-extra/devtools.jar")" "$files/plugins/devtools.jar" >/dev/null
  A shell setprop debug.osrsrl.devtools 1 >/dev/null
}
check_blocked() { s=$(serial) && need "$adb" ANDROID_SDK && blocked; }
blocked() {  # every host must fail to resolve on the device; a connect by name needs a resolve first
  until [[ $(A shell dumpsys connectivity) == *"PrivateDnsServerName: blocked.invalid"* ]]; do sleep 1; done   # the setting applied
  local out no; out=$(A shell "for h in $hosts; do nc -w 15 \$h 443 </dev/null & done; wait")
  no=$(for h in $hosts; do grep -q "$h:443: No address associated with hostname" <<< "$out" || echo "$h"; done)
  [ -z "$no" ] && echo "blocked: $hosts" || { echo "NOT BLOCKED (resolves or connects):" $no; return 1; }
}
down() {  # judges what the harness put on the device (the default plugins, the debug props) against its baseline; the
  # app's own writes (settings.properties, cache) are not checked: they differ after any login and the read-only AVD drops them
  s=$(serial) || return 0; need "$adb" ANDROID_SDK
  bad=()
  A shell rm -f "$files/plugins/devtools.jar" >/dev/null
  for p in devtools probe set; do A shell setprop "debug.osrsrl.$p" '""' >/dev/null; done
  [ "$(A shell ls -1 "$files/plugins" | LC_ALL=C sort | paste -sd' ')" = "$defaults" ] || bad+=("plugins dir is not the three defaults")
  for p in netallow probe set where say wdump keylog projlog camlog selflog tmodelog netlog netwrite agent devtools soundlog; do
    v=$(A shell getprop "debug.osrsrl.$p" | tr -d '[:space:]'); [ -z "$v" ] || [ "$v" = 0 ] || bad+=("prop debug.osrsrl.$p = $v")
  done
  A emu kill >/dev/null; rm -f "$state/serial"
  [ ${#bad[@]} = 0 ] || { (IFS=';'; echo "RESTORE FAILED: ${bad[*]}"); return 1; }; echo restored
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
check_state_doc() { r=$(grep -oE '^# result: pass [0-9]+ / [0-9]+ = [0-9.]+%' "$ev/api-probe.tsv" 2>/dev/null | grep -oE '[0-9.]+%$' || true); [ -n "$r" ] || { echo "no probe result"; return 1; }; grep -q "$r" "${BUILD:-.}/docs/client-state.md" && echo "client-state.md carries the probe's $r" || { echo "docs/client-state.md does not carry the probe's result $r"; return 1; }; }
ship() { git -C "$repo" push -q origin "$arg:refs/heads/main" && echo "main at ${arg:0:9}"; }
measure() {
  tc=$(git -C "$repo" rev-parse "${arg:-origin/HEAD}^{commit}"); arg=$tc; T=$(git -C "$repo" rev-parse "$tc^{tree}"); build; up
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

case $verb in up|measure) trap '[ $? = 0 ] || down >&2 || true' EXIT ;; esac   # a failed boot leaves no emulator behind
case $verb in build|up|down|gate|check-blocked|check-log|check-evidence|check-probe|check-netqa|check-state-doc|ship|measure|log|netqa) "${verb//-/_}" ;;
  *) echo "verbs: build up down gate check-blocked check-log check-evidence check-probe check-netqa check-state-doc ship measure log netqa" >&2; exit 2 ;; esac
