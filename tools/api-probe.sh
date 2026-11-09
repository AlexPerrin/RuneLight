#!/usr/bin/env bash
# Runtime probe of the RuneLite API in the Android app (goal client-api):
#   ANDROID_SERIAL=<your own instance> tools/api-probe.sh [out-dir]   (default ~/osrs-harness/evidence/client-api)
# The app must run this repo's build (host.jar carries osrsrl-build.txt and api-native.txt), be logged in on the test
# server with an account of its own (it is given items, levels and a friend, and fights) and serve osrs-devtools
# (tools/devtools_mcp.py: every set-up step below is one of its calls). Builds the probe jar (host/plugins/api-probe),
# pushes it to plugins/, has the running host load it (`debug.osrsrl.set 'load api-probe.jar <n>'`), waits for its
# report, pulls it to <out-dir>/api-probe.tsv, removes the jar and the report from the device by name and prints one
# line: pass / denominator. The denominator is every abstract method of every net.runelite.api interface in
# re/runelite/runelite-api-1.13.1.jar, less the interfaces the report's header lists as excluded.
#
# The state the rows need is set up here, no human step:
#  - a friend whose presence changes while the probe waits (PendingLogin; Nameable/ChatPlayer from the list): a
#    RuneLite desktop on the live stack (the harness's fork track live-desktop, $HD_ROOT/tracks/fork/track.sh, as
#    SLUG's own account N=1), added to the mobile's list; its private chat starts at "Friends" (the mobile is not on its
#    list, so the server shows it offline) and is set to "Show all" once the probe waits: a login notice
#  - a fight (Hitsplat, GraphicsObject, HealthBarConfig): `::item scythe_of_vitur` (ItemContainer), the levels to wield
#    it (::setxp), wielded, and `::npc 6000 poh_combat_dummy_npc` attacked: a hitsplat and the scythe's map spotanim per swing
#  - the world map open (WorldMapRegion/Icon, setWorldMapPositionTarget); the skills tab opened once the probe waits (its
#    "Total level" settext raises skillTabTotalLevel: Script)
#  - a modal for closeInterface once it waits for one (logcat `closeInterface waits`): auto retaliate off and a
#    teleport onto the player's own tile end the fight (the server closes modals during one), then `::interface 84`
#    (equipment stats), opened by a chat command: no devtools touch moves the virtual mouse before the menuAction walk
# The game's idle logout is moved to 10 hours first (`debug.osrsrl.set 'afk 600 <n>'`): the desktop takes minutes to start.
# The probe waits for each in `debug.osrsrl.probewait`. Last it writes its report with Client.stopNow "called"; the
# set-up is undone (the scythe unwielded and dropped, the xp and auto retaliate restored, the friend deleted, the map
# closed, the desktop stopped; the dummy despawns after its 6000 ticks), "stopNow" in probewait has the probe call
# Client.stopNow, and the row is pass once the app process ended (mismatch if the call returned). The app is started
# again (signed out) for the caller's down.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$HOME/osrs-harness/evidence/client-api}
: "${ANDROID_SERIAL:?set ANDROID_SERIAL to your own instance (127.0.0.1:<adb port>)}"
ADB=${ADB:-/mnt/c/Users/Alex/AppData/Local/Android/Sdk/platform-tools/adb.exe}
HD=${HD_ROOT:-$HOME/osrs-harness}; FORK=$HD/tracks/fork/track.sh
SLUG=${SLUG:-$(basename "$OUT")}   # the desktop's account is this slug's (the harness names the evidence dir by it)
PKG=com.jagex.oldscape.android
F=/sdcard/Android/data/$PKG/files
WAIT=PendingLogin,Script,Hitsplat,GraphicsObject,HealthBarConfig,modal
SCYTHE=22325; INV_TAB=39387233; WORN_TAB=39387234; SKILLS_TAB=39387207; FRIENDS_TAB=39387211   # 601: the mobile toplevel's side tabs
ADD_FRIEND=28114958; DEL_FRIEND=28114960; MAP_ORB=10485809; WORN_WEAPON=25362450   # 429:14, 429:16, 160:49, 387:18
adb() { "$ADB" -s "$ANDROID_SERIAL" "$@"; }
alive() { adb shell pidof $PKG >/dev/null 2>&1 || adb shell pidof $PKG >/dev/null 2>&1; }  # adb.exe from WSL fails now and then
dt() {  # dt <tool> [json arguments]: one osrs-devtools call on the mobile; its text, or fails naming the refusal
  local a=${2-}; [ -n "$a" ] || a='{}'
  python3 "$ROOT/tools/devtools_mcp.py" --call tools/call "{\"name\":\"$1\",\"arguments\":$a}" | python3 -c '
import json, sys
m = json.load(sys.stdin); r = m.get("result") or {}
t = "".join(c.get("text", "") for c in r.get("content", []))
e = m.get("error") or (t if r.get("isError") or t.startswith("Error") else None)
if e: sys.exit(f"api-probe: {sys.argv[1]}: {e}")
print(t)' "$1"
}
field() { python3 -c 'import json,sys; v=json.load(sys.stdin)
for k in sys.argv[1:]: v=v[int(k)] if isinstance(v, list) else v[k]
print(v)' "$@"; }
desk() {  # desk <tool> [json arguments]: one osrs-devtools call on the desktop (its token is the up's)
  local a=${2-}; [ -n "$a" ] || a='{}'
  python3 -c 'import json, sys; sys.path.insert(0, sys.argv[1]); import desktop_login as d; print(d.call(sys.argv[2], sys.argv[3], **json.loads(sys.argv[4])))' \
    "$HD/tracks/fork/server" "$port" "$1" "$a"
}
chat() { dt type_chat "{\"text\":\"$1\"}" >/dev/null; }
said() { dt get_chat_history | grep -qF "$1"; }   # a game message seen since login
worn() { dt get_inventory '{"inventoryId":94}' 2>/dev/null | python3 -c 'import json,sys; print(" ".join(str(i["itemId"]) for i in json.load(sys.stdin)["items"]))' || true; }
held() { dt get_inventory '{"inventoryId":93}' | python3 -c 'import json,sys; print(" ".join(str(i["itemId"]) for i in json.load(sys.stdin)["items"]))'; }
fighting() { dt list_players | python3 -c 'import json,sys; print(next((p.get("interacting") or "") for p in json.load(sys.stdin)["players"] if p.get("isLocal")))'; }
xp() { dt get_skills | python3 -c 'import json,sys; print(next(s["xp"] for s in json.load(sys.stdin)["skills"] if s["skill"].lower() == sys.argv[1]))' "$1"; }
shown() { [[ $(dt get_widget "{\"componentId\":$(($1 << 16)),\"depth\":0}" 2>/dev/null) == *'"hidden":false'* ]]; }
tab() {  # tab <side tab> <its interface>: shown (a click on the open tab closes it); its widgets exist once it is
  shown "$2" && return; dt click_component "{\"componentId\":$1}" >/dev/null; until shown "$2"; do sleep 1; done; }
plog() { adb logcat -d -v brief -T "$mark" 2>/dev/null | tr -d '\r' | grep -F "api-probe: $1" || true; }

alive || { echo "api-probe: $PKG is not running on $ANDROID_SERIAL (start it and log in first)"; exit 1; }
adb shell setprop debug.osrsrl.set "'afk 600 $(date +%s%N)'"   # the game's idle logout after 10 hours, not 5 minutes: the set-up waits on the desktop's start
"$ROOT/host/plugins/api-probe/build.sh" >/dev/null
mkdir -p "$OUT"
adb shell rm -f $F/api-probe.tsv $F/api-probe.tsv.part

# the friend: a desktop on the live stack, logged in
port=; desktop=; cleaned=
undo() {  # the set-up undone while the mobile plays; the desktop stopped
  [ -n "$cleaned" ] && return; cleaned=1
  if alive && [ "$(dt get_client_state | field gameState)" = LOGGED_IN ]; then
    ! shown 595 || dt click_component "{\"componentId\":$MAP_ORB}" >/dev/null || true
    while [[ " $(worn) " == *" $SCYTHE "* ]]; do tab $WORN_TAB 387; dt widget_action "{\"componentId\":$WORN_WEAPON,\"option\":\"Remove\"}" >/dev/null || true; sleep 1; done
    [[ " $(held) " != *" $SCYTHE "* ]] || tab $INV_TAB 149
    while [[ " $(held) " == *" $SCYTHE "* ]]; do   # dropped, each step on the screen it reads: Drop, the valuable prompt, "Put it down."
      case $(dt get_dialogue | field type) in
        none) dt item_action "{\"itemId\":$SCYTHE,\"option\":\"Drop\"}" >/dev/null || true; dt wait_for '{"condition":"dialogue","timeoutMs":60000}' >/dev/null ;;
        item) dt continue_dialogue >/dev/null ;;
        options) dt select_option '{"option":1}' >/dev/null ;;
      esac
    done
    for s in "${!xp0[@]}"; do chat "::setxp $s ${xp0[$s]}"; done
    [ -z "${retaliate0:-}" ] || chat "::varp 172 $retaliate0"
    if [ -n "${friend:-}" ]; then tab $FRIENDS_TAB 429; dt click_component "{\"componentId\":$DEL_FRIEND}" >/dev/null; dt enter_input "{\"text\":\"$friend\"}" >/dev/null; fi
  fi
  [ -z "$port$desktop" ] || "$FORK" live-desktop-down "$SLUG" >&2
}
declare -A xp0
trap 'undo || true' EXIT

# the mobile: the fight, the world map
chat "::item scythe_of_vitur"; until [[ " $(held) " == *" $SCYTHE "* ]]; do sleep 1; done
for s in attack:1986068 strength:5346332 hitpoints:13034431; do   # 80 and 90 wield it; 99 hitpoints outlast the dummy's hits
  xp0[${s%:*}]=$(xp "${s%:*}"); chat "::setxp ${s%:*} ${s#*:}"; until [ "$(xp "${s%:*}")" = "${s#*:}" ]; do sleep 1; done
done
tab $INV_TAB 149
until [[ " $(worn) " == *" $SCYTHE "* ]]; do dt item_action "{\"itemId\":$SCYTHE,\"option\":\"Wield\"}" >/dev/null || true; sleep 1; done
chat "::npc 6000 poh_combat_dummy_npc"
until npc=$(dt list_npcs | python3 -c 'import json,sys; n=[n for n in json.load(sys.stdin)["npcs"] if "Combat dummy" in (n.get("name") or "")]; n.sort(key=lambda n: n["distance"]); print(n[0]["index"])' 2>/dev/null); do sleep 1; done
dt interact_npc "{\"npcIndex\":$npc,\"option\":\"Attack\"}" >/dev/null
until dt get_combat_history '{"windowMs":10000}' | grep -q '"actorType":"npc"'; do sleep 1; done   # the attack has settled the player and hits
shown 595 || dt click_component "{\"componentId\":$MAP_ORB}" >/dev/null; dt wait_for '{"condition":"interface_open","interfaceId":595,"timeoutMs":60000}' >/dev/null

# the friend: the desktop logged in (it idles out after 5 minutes too: the probe starts at once), added to the mobile's list
desktop=1; d=$("$FORK" live-desktop "$SLUG" | tail -1); [[ $d == LIVE_DESKTOP* ]] || { echo "api-probe: no live desktop for the friend: $d"; exit 1; }
port=$(cut -d' ' -f2 <<< "$d")
friend=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["accounts"].split()[0])' "$HD/state/$SLUG/up.json")
tab $FRIENDS_TAB 429; dt click_component "{\"componentId\":$ADD_FRIEND}" >/dev/null; dt enter_input "{\"text\":\"$friend\"}" >/dev/null
! said "Unable to find player" || { echo "api-probe: the server found no player $friend to add"; exit 1; }

mark=$(adb shell date +%s.000 | tr -d '\r')
adb push "$(wslpath -w "$ROOT/re/host/plugins-extra/api-probe.jar")" $F/plugins/api-probe.jar >/dev/null 2>&1
adb shell setprop debug.osrsrl.probewait $WAIT
adb shell setprop debug.osrsrl.set "'load api-probe.jar $(date +%s%N)'"
status=0; waited=; modal=
until adb shell ls $F/api-probe.tsv >/dev/null 2>&1; do
    alive || { echo "api-probe: the app exited during the probe; the last row it started is in logcat (rl.ApiProbePlugin)"; status=1; break; }
    if [ -z "$waited" ] && [ -n "$(plog 'waiting for')" ]; then   # the rows that need no waited instance are done
        waited=1; tab $SKILLS_TAB 320
        desk widget_action '{"componentId":10616847,"option":"Show all"}' >/dev/null   # chatbox private filter (162:15): a login notice on the mobile
    fi
    if [ -z "$modal" ] && [ -n "$(plog 'closeInterface waits')" ]; then
        modal=1; retaliate0=$(dt get_var '{"type":"varp","id":172}' | field value)
        chat "::varp 172 1"; at=$(dt get_client_state); chat "::tele $(field worldX <<< "$at") $(field worldY <<< "$at") $(field plane <<< "$at")"
        until [ -z "$(fighting)" ]; do sleep 1; done   # the teleport ran (only it ends the attack): no later close of the modal
        chat "::interface 84"
    fi
    sleep 3
done
[[ $status == 0 ]] && adb exec-out cat $F/api-probe.tsv > "$OUT/api-probe.tsv"
adb shell rm $F/plugins/api-probe.jar
adb shell rm -f $F/api-probe.tsv $F/api-probe.tsv.part
adb shell setprop debug.osrsrl.set "''"
[[ $status == 0 ]] || { adb shell setprop debug.osrsrl.probewait "''"; exit 1; }
undo

R=$OUT/api-probe.tsv
! grep '^# error' "$R" || exit 1
if grep -qP '^net\.runelite\.api\.Client\tstopNow\(\)V\tcalled\t' "$R"; then   # the probe calls it now; its row is the app's end
    adb shell setprop debug.osrsrl.probewait stopNow
    until ! alive || [ -n "$(plog 'stopNow returned')" ]; do sleep 1; done
    if alive; then r=mismatch; e="the app process still runs: $(plog 'stopNow returned' | tail -1 | sed 's/.*api-probe: //')"; else r=pass; e="the app process ended (pidof $PKG empty) after the report was written"; fi
    python3 - "$R" "$r" "$e" <<'EOF'
import collections, re, sys
path, result, evidence = sys.argv[1:]
lines = open(path, encoding="utf-8").read().splitlines()
rows = [i for i, l in enumerate(lines) if not l.startswith("#") and "\t" in l][1:]
for i in rows:
    f = lines[i].split("\t")
    if f[0] == "net.runelite.api.Client" and f[1] == "stopNow()V": f[2], f[4] = result, evidence; lines[i] = "\t".join(f)
counts = collections.Counter(lines[i].split("\t")[2] for i in rows); n = len(rows); p = counts["pass"]
for i, l in enumerate(lines):
    if l.startswith("# counts: "): lines[i] = "# counts: {" + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) + "}"
    if l.startswith("# result: "): lines[i] = f"# result: pass {p} / {n} = {100 * p / n:.1f}%"
open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
EOF
    alive || adb shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1   # started again for the caller's down
fi
adb shell setprop debug.osrsrl.probewait "''"
denominator=$(sed -n 's/^# denominator: //p' "$R" | tr -d '\r')
rows=$(grep -vc '^#' "$R")
rows=$((rows - 1))  # the column header
result=$(sed -n 's/^# result: //p' "$R" | tr -d '\r')
build=$(sed -n 's/^# build: //p' "$R" | tr -d '\r')
[[ $rows == "$denominator" ]] || { echo "api-probe: $rows rows but denominator $denominator in $R"; exit 1; }
echo "api-probe: $result (build $build, $rows rows) -> $R"
