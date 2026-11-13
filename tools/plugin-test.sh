#!/usr/bin/env bash
# Client/plugin-host test loop on your own Agent_Phone emulator (Windows adb from WSL):
#   ANDROID_SERIAL=emulator-<port> tools/plugin-test.sh [N]
#                              push re/host/plugins/*.jar (not *-desktop.jar), restart the game and sign in
#                              (~/osrs-harness/bench/emulator_login.py), print the first N host log lines
#   EXTRA="xpdrop statusbars"  also push re/host/plugins-extra/<name>.jar
# Assumes the repack (OPENRUNE=1 LOCAL_AUTH=1 build) is installed.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$(dirname "$0")/env.sh"
S="-s ${ANDROID_SERIAL:?set ANDROID_SERIAL to your emulator (emulator-<port>)}"
PKG=com.jagex.oldscape.android
P=/sdcard/Android/data/$PKG/files/plugins

$ADB $S logcat -G 16M
$ADB $S shell mkdir -p $P
$ADB $S shell "rm -f $P/*.jar"
for j in "$ROOT"/re/host/plugins/*.jar; do
    [[ $j == *-desktop.jar ]] || $ADB $S push "$(wslpath -w "$j")" $P/ >/dev/null
done
for n in ${EXTRA:-}; do
    $ADB $S push "$(wslpath -w "$ROOT/re/host/plugins-extra/$n.jar")" $P/ >/dev/null
done
$ADB $S shell am force-stop $PKG
$ADB $S logcat -c
# Launches only the repack, signs in with "Sign in with Email / Username" only, exits 0 in game.
python3 -I ~/osrs-harness/bench/emulator_login.py || exit 2
$ADB $S logcat -d | grep -E "osrsrl-host|rl\.|System.err|FATAL" | head -${1:-40}
