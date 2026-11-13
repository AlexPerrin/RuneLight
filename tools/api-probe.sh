#!/usr/bin/env bash
# Runtime probe of the RuneLite API in the Android app (goal client-api):
#   ANDROID_SERIAL=<your own instance> tools/api-probe.sh [out-dir]   (default re/out/api-probe)
# The app must run this repo's build (host.jar carries osrsrl-build.txt and api-native.txt) and be logged in on
# the test server. Builds the probe jar (host/plugins/api-probe), pushes it to plugins/, has the running host
# load it (`debug.osrsrl.set 'load api-probe.jar <n>'`), waits for its report, pulls it to
# <out-dir>/api-probe.tsv, removes the jar and the report from the device by name and prints one line:
# pass / denominator. The denominator is every abstract method of every net.runelite.api interface in
# re/runelite/runelite-api-1.13.1.jar, less the interfaces the report's header lists as excluded.
# The probe stays loaded in the app until it restarts; it has unregistered itself by then.
# State the probe reaches only when set up first: a fight running (Hitsplat; a wielded scythe of vitur adds a
# map spotanim per swing for GraphicsObject; `::npc 1000 poh_combat_dummy_npc` is a target that lasts; while the
# player is interacting and no hitsplat has come, the probe waits for one), the world map open while it runs
# (WorldMapRegion/Icon, setWorldMapPositionTarget), a song playing (MidiRequest) and ambient sounds in range
# (Lumbridge's goblin field). The menu draw rows pass with a long-press menu open or with none.
# Instances that appear only while it runs are waited for when named in `debug.osrsrl.probewait` (comma-separated
# interface names): PendingLogin (log a listed friend in or out on another client while it runs: the notice lives
# ~5 s), Script (open a tab whose script raises a RuneLite callback, e.g. the skills tab: skillTabTotalLevel).
# HealthBarConfig comes from the fight's head bars, DynamicObject from an animated loc in the scene.
# Last, one at a time: closeInterface closes the open modal client-side ("modal" in probewait: it waits for one, e.g.
# equipment stats), menuAction walks onto the player's own tile (the server closes the modal too; the fight stops),
# setGameState(LOADING) reloads the scene (a ground object it removed comes back). The session and world stay as they are.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/re/out/api-probe}
: "${ANDROID_SERIAL:?set ANDROID_SERIAL to your own instance (127.0.0.1:<adb port>)}"
. "$(dirname "$0")/env.sh"
PKG=com.jagex.oldscape.android
F=/sdcard/Android/data/$PKG/files
adb() { "$ADB" -s "$ANDROID_SERIAL" "$@"; }
alive() { adb shell pidof $PKG >/dev/null 2>&1 || adb shell pidof $PKG >/dev/null 2>&1; }  # adb.exe from WSL fails now and then

"$ROOT/host/plugins/api-probe/build.sh" >/dev/null
mkdir -p "$OUT"
alive || { echo "api-probe: $PKG is not running on $ANDROID_SERIAL (start it and log in first)"; exit 1; }
adb shell rm -f $F/api-probe.tsv $F/api-probe.tsv.part
adb push "$(wslpath -w "$ROOT/re/host/plugins-extra/api-probe.jar")" $F/plugins/api-probe.jar >/dev/null 2>&1
adb shell setprop debug.osrsrl.set "'load api-probe.jar $(date +%s%N)'"
status=0
until adb shell ls $F/api-probe.tsv >/dev/null 2>&1; do
    alive || { echo "api-probe: the app exited during the probe; the last row it started is in logcat (rl.ApiProbePlugin)"; status=1; break; }
    sleep 3
done
[[ $status == 0 ]] && adb exec-out cat $F/api-probe.tsv > "$OUT/api-probe.tsv"
adb shell rm $F/plugins/api-probe.jar
adb shell rm -f $F/api-probe.tsv $F/api-probe.tsv.part
adb shell setprop debug.osrsrl.set "''"
[[ $status == 0 ]] || exit 1

R=$OUT/api-probe.tsv
! grep '^# error' "$R" || exit 1
denominator=$(sed -n 's/^# denominator: //p' "$R" | tr -d '\r')
rows=$(grep -vc '^#' "$R")
rows=$((rows - 1))  # the column header
result=$(sed -n 's/^# result: //p' "$R" | tr -d '\r')
build=$(sed -n 's/^# build: //p' "$R" | tr -d '\r')
[[ $rows == "$denominator" ]] || { echo "api-probe: $rows rows but denominator $denominator in $R"; exit 1; }
echo "api-probe: $result (build $build, $rows rows) -> $R"
