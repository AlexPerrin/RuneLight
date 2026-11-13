#!/usr/bin/env bash
# One-time setup of a clean clone: checks the toolchain, fetches the RuneLite and library jars the plugin host is built
# from (tools/deps.txt, each verified by SHA-256) and configures the native build in re/build.
#   tools/setup.sh
# Needs the official OSRS app next to this file as base.apk and split_config.arm64_v8a.apk (build 241.4; not distributed here).
# Environment (all optional, see tools/env.sh): ANDROID_SDK, ANDROID_NDK, APKTOOL_JAR, ADB.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
. tools/env.sh
fail=0
need() { command -v "$1" >/dev/null 2>&1 || { echo "setup: missing '$1' ($2)" >&2; fail=1; }; }
need java "JDK 17+"; need jar "JDK 17+"; need python3 "Python 3"; need curl curl; need unzip unzip
need cmake "CMake 3.22+"; need ninja ninja; need keytool "JDK"; need zipalign "Android build-tools"; need apksigner "Android build-tools"
[[ -f $APKTOOL_JAR ]] || { echo "setup: apktool not found at $APKTOOL_JAR (download apktool.jar from https://apktool.org, or set APKTOOL_JAR)" >&2; fail=1; }
[[ -f $ANDROID_SDK/platforms/android-37.0/android.jar ]] || { echo "setup: Android SDK platform android-37.0 not found under $ANDROID_SDK (set ANDROID_SDK)" >&2; fail=1; }
[[ -f $ANDROID_SDK/build-tools/36.0.0/lib/d8.jar ]] || { echo "setup: Android build-tools 36.0.0 not found under $ANDROID_SDK" >&2; fail=1; }
NDK=${ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
if [[ -z $NDK ]]; then NDK=$(ls -d "$ANDROID_SDK"/ndk/*/ "$HOME"/Android/Sdk/ndk/*/ 2>/dev/null | sort -V | tail -1 || true); NDK=${NDK%/}; fi
TOOLCHAIN=$NDK/build/cmake/android.toolchain.cmake
[[ -n $NDK && -f $TOOLCHAIN ]] || { echo "setup: Android NDK not found (install one under \$ANDROID_SDK/ndk, or set ANDROID_NDK)" >&2; fail=1; }
for f in base.apk split_config.arm64_v8a.apk; do
    [[ -f $f ]] || { echo "setup: $f (from the official OSRS app, build 241.4) is missing from $ROOT" >&2; fail=1; }
done
(( fail == 0 )) || exit 1

mkdir -p re/runelite
while read -r sum url; do
    f=re/runelite/${url##*/}
    if [[ ! -f $f ]] || [[ $(sha256sum "$f" | cut -d' ' -f1) != "$sum" ]]; then
        echo "setup: fetching ${url##*/}"
        curl -sfL -o "$f.part" "$url" || { rm -f "$f.part"; echo "setup: download failed: $url" >&2; exit 1; }
        [[ $(sha256sum "$f.part" | cut -d' ' -f1) == "$sum" ]] || { rm -f "$f.part"; echo "setup: checksum mismatch: ${url##*/}" >&2; exit 1; }
        mv "$f.part" "$f"
    fi
done < tools/deps.txt

cmake -S native -B re/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 > /dev/null
echo "setup: done. Build with: host/build.sh && ninja -C re/build && tools/repack.sh"
