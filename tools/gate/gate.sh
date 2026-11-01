#!/usr/bin/env bash
# tools/gate/gate.sh <verb> <slug> [arg]: the client track's build and gate, and every verb a contract row runs (tools/track.sh
# dispatches them here). hd keys a build and a gate by the hash of tools/gate/ and the files the contract's rows name. Env from hd:
# SLUG HEAD T TCOMMIT BASE CLONE EVIDENCE BUILD HD_ROOT. Verbs:
#   build [TCOMMIT]   the devtools plugin (host/plugins/devtools/build.sh on the copy's own re/devtools, a shared clone of the
#                     main one, re/devtools-build and re/host/plugins-extra), host build, ninja and the repack with OPENRUNE=1
#                     LOCAL_AUTH=1 in the build copy; the APK is kept only when its repack log passes check-log; cached by T
#                     (evidence/<slug>/<T>.apk, <T>.devtools.jar, the jar boot pushes, and <T>.devtools-desktop.jar, its
#                     classes before dexing, the jar the server track's desktops load from the build of slug live)
#   gate TCOMMIT      every check: and gate: row of the contract from the build copy (`hd rows`)
#   check-log | check-evidence | check-probe | check-netqa | check-state-doc   the seed rows
#   tree              prints TCOMMIT and T (a dev: the working copy snapshotted)
set -euo pipefail
verb=$1; slug=${2:-${SLUG:-}}; arg=${3:-}   # a contract row passes no slug: hd exports SLUG
self=$(cd "$(dirname "$0")" && pwd)   # tools/gate
repo=$(dirname "$(git -C "$self" rev-parse --path-format=absolute --git-common-dir)")  # the main checkout (re/ lives there), also when run from a worktree
root=${HD_ROOT:-$HOME/osrs-harness}
[ -f "$root/harness/changes/$slug.md" ] || [ -f "$root/harness/goals/$slug.md" ] || case $slug in live|integration) ;; *) echo "no change or goal \"$slug\": tools/track.sh <verb> <slug> [arg]" >&2; exit 2 ;; esac
state=$root/state/$slug; mkdir -p "$state"
ev=${EVIDENCE:-$root/evidence/$slug}; mkdir -p "$ev"
dev=$repo/.worktrees/$slug; copy=$repo/.worktrees/$slug-build
ndk=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/android-ndk-r27d}
atomic() { cat > "$1.$$.tmp"; if [ -s "$1.$$.tmp" ]; then mv -f "$1.$$.tmp" "$1"; else rm -f "$1.$$.tmp"; return 1; fi; }   # a state record whole or not at all: hd reads it at any moment
record() { python3 -c 'import json,sys;print(json.dumps({"T":sys.argv[1],"ok":sys.argv[2]=="true","detail":sys.stdin.read()[-2000:]}))' "$T" "$1" | atomic "$state/$2.json"; }

tree() {
  if [ -n "$arg" ]; then tc=$arg; T=${T:-$(git -C "$repo" rev-parse "$tc^{tree}")}; return; fi
  [ -d "$dev" ] || git -C "$repo" worktree add -q "$dev" -B "client/$slug" "$(git -C "$repo" rev-parse -q --verify "origin/client/$slug" || echo origin/HEAD)"
  idx=$(git -C "$dev" rev-parse --git-path snapshot-index); rm -f "$idx"
  GIT_INDEX_FILE=$idx git -C "$dev" add -A . && snap=$(git -C "$dev" commit-tree "$(GIT_INDEX_FILE=$idx git -C "$dev" write-tree)" -p HEAD -m snapshot)
  read -r tc T < <(HD_ROOT=$root python3 "$root/hd.py" tree "$slug" "$snap")
}
build() {
  tree
  if [ -f "$ev/$T.apk" ] && [ -f "$ev/$T.devtools.jar" ] && [ -f "$ev/$T.devtools-desktop.jar" ]; then echo "cached $ev/$T.apk"; record true build <<< "cached"; return; fi
  if [ -d "$copy" ]; then git -C "$copy" reset -q --hard "$tc" && git -C "$copy" clean -fdq; else git -C "$repo" worktree add -q --detach "$copy" "$tc"; fi
  mkdir -p "$copy/re/host"; for f in base.apk split_config.arm64_v8a.apk; do ln -sfn "$repo/$f" "$copy/$f"; done  # the ignored store APKs
  for e in "$repo"/re/*; do n=$(basename "$e"); case $n in host|build|out|devtools|devtools-build) ;; *) ln -sfn "$e" "$copy/re/$n" ;; esac; done
  for n in devtools devtools-build; do [ -L "$copy/re/$n" ] && rm "$copy/re/$n"; done   # a copy made before the change's own devtools linked them
  [ -d "$copy/re/devtools/.git" ] || git clone -q --shared "$repo/re/devtools" "$copy/re/devtools"   # its own checkout; the objects stay shared
  mkdir -p "$copy/re/devtools-build"; ln -sfn "$repo/re/devtools-build/lombok-1.18.30.jar" "$copy/re/devtools-build/lombok-1.18.30.jar"
  [ -d "$copy/re/host/plugins-extra" ] || cp -a "$repo/re/host/plugins-extra" "$copy/re/host/"
  [ -f "$copy/re/build/build.ninja" ] || cmake -S "$copy/native" -B "$copy/re/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 >/dev/null
  log=$ev/repack-$T.log
  (cd "$copy" && host/plugins/devtools/build.sh && host/build.sh && ninja -C re/build && OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh) > "$log" 2>&1 || { record false build < "$log"; echo "build failed: $log"; return 1; }
  EVIDENCE=$ev check_log >/dev/null || { record false build <<< "repack log fails the privacy check: $log"; rm -f "$copy/re/out/osrs-runelite.apk"; return 1; }
  cp "$copy/re/host/plugins-extra/devtools.jar" "$ev/$T.devtools.jar"; cp "$copy/re/devtools-build/devtools.jar" "$ev/$T.devtools-desktop.jar"; cp "$copy/re/out/osrs-runelite.apk" "$ev/$T.apk"; record true build < "$log"; echo "built $ev/$T.apk"
}
gate() { tc=$arg; build; git -C "$copy" reset -q --hard "$tc"
  if out=$(HD_ROOT=$root python3 "$root/hd.py" rows "$slug" "$tc" "$copy" 2>&1); then record true gate <<< "$out"; else record false gate <<< "$out"; fi; }
check_log() {
  l=$(ls -t "$ev"/repack*.log 2>/dev/null | head -1 || true); [ -n "$l" ] || { echo "no repack log in $ev"; return 1; }
  [ "$(grep -c '^telemetry:' "$l")" = 7 ] && grep -q '^patched RSA modulus' "$l" && grep -q '^patched /jav_config\.ws' "$l" && grep -qE '^patched \S*/net/openid/appauth/' "$l" \
    && echo "repack log: 7 telemetry lines, RSA modulus, jav_config and appauth patched" || { echo "$l: the APK talks to Jagex (OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh)"; return 1; }
}
check_evidence() { for p in "$ev"/*logcat*.txt "$ev"/*.dump.* "$ev"/*devtools*.json; do [ -e "$p" ] && { echo "mobile evidence: $(basename "$p")"; return 0; }; done; echo "no logcat extract, dump or devtools JSON in $ev"; return 1; }
check_probe() {
  f=$ev/api-probe.tsv; [ -f "$f" ] || { echo "no api-probe.tsv in $ev"; return 1; }
  b=$(grep -oE '^# build: \S*-g[0-9a-f]+' "$f" | grep -oE '[0-9a-f]+$' || true); [ -n "$b" ] || { echo "$f has no '# build:' line"; return 1; }
  case $HEAD in "$b"*) echo "probe built at $HEAD"; return 0 ;; esac
  d=$(git -C "$repo" diff --name-only "$b" "$HEAD" | grep -v '^docs/' || true); [ -z "$d" ] && echo "probe at $b, only docs differ" || { echo "probe built at $b, not $HEAD: rerun it"; return 1; }
}
check_netqa() { grep -qE '^network: true' "$root/harness/changes/$slug.md" 2>/dev/null || { echo "no network QA needed"; return 0; }
  grep -lq 'PASS' "$ev"/netqa*.md 2>/dev/null && echo "network QA PASS" || { echo "network: true and no netqa*.md with PASS in $ev"; return 1; }; }
check_state_doc() { r=$(grep -oE '^# result: pass [0-9]+ / [0-9]+ = [0-9.]+%' "$ev/api-probe.tsv" 2>/dev/null | grep -oE '[0-9.]+%$' || true); [ -n "$r" ] || { echo "no probe result"; return 1; }; grep -q "$r" "${BUILD:-.}/docs/client-state.md" && echo "client-state.md carries the probe's $r" || { echo "docs/client-state.md does not carry the probe's result $r"; return 1; }; }

case $verb in build|gate|check-log|check-evidence|check-probe|check-netqa|check-state-doc) "${verb//-/_}" ;; tree) tree; echo "$tc $T" ;;
  *) echo "verbs: build gate check-log check-evidence check-probe check-netqa check-state-doc tree" >&2; exit 2 ;; esac
