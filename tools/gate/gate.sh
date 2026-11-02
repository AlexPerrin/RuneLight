#!/usr/bin/env bash
# tools/gate/gate.sh <verb> <slug> [arg]: the client track's gate and every verb a contract row runs but check-log (tools/track.sh
# dispatches them here); the build is tools/build/build.sh's, sourced. hd keys a gate by the build key and the hash of tools/gate/
# and the files the contract's rows name. Env from hd: SLUG HEAD T (the build key) G (the gate key) TCOMMIT BASE CLONE EVIDENCE BUILD HD_ROOT. Verbs:
#   gate TCOMMIT      the build, then every check: and gate: row of the contracts of NAMES (the batch's changes) from the
#                     build copy (`hd rows`); state/<slug>/gate.json under G, kept as gate-<G>.json
#   check-evidence | check-probe | check-netqa | check-state-doc   the seed rows
. "$(dirname "$0")/../build/build.sh"   # verb slug arg self(tools/build) repo root state ev dev copy atomic record tree build check_log
gate() { tc=$arg  # hd gates the client's batch as ship-client, NAMES its changes; recorded under the gate key G, kept as gate-<G>
  if ! "$self/build.sh" build "$slug" "$tc"; then ok=false out="build failed at $T: $ev/repack-$T.log"
  else git -C "$copy" reset -q --hard "$tc"
    out=$(HD_ROOT=$root python3 "$root/hd.py" rows "${NAMES:-$slug}" "$tc" "$copy" 2>&1) && rc=0 || rc=$?
    [ $rc != 75 ] || { echo "$out"; exit 75; }   # a row held (its source down): no result, hd starts the gate again
    ok=$([ $rc = 0 ] && echo true || echo false)
  fi; T=${G:-$T}; record "$ok" gate <<< "$out"; cp "$state/gate.json" "$state/.gate.$$.tmp"; mv -f "$state/.gate.$$.tmp" "$state/gate-$T.json"; }
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

case $verb in gate|check-evidence|check-probe|check-netqa|check-state-doc) "${verb//-/_}" ;;
  *) echo "verbs: gate check-evidence check-probe check-netqa check-state-doc" >&2; exit 2 ;; esac
