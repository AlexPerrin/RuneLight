#!/usr/bin/env bash
# tools/not-debuggable.sh <apk>: exits 0 when the APK's manifest is not android:debuggable="true", 1 when it is.
# release-dev.sh refuses a debuggable APK (DEBUGGABLE=1 repacks are the agents' dev builds only).
set -euo pipefail
[[ -f ${1:-} ]] || { echo "usage: tools/not-debuggable.sh <apk>" >&2; exit 2; }
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
java -jar "$HOME/tools/apktool.jar" d -q -s --no-assets -f "$1" -o "$d/x"
if grep -q '<application [^>]*android:debuggable="true"' "$d/x/AndroidManifest.xml"; then echo "$1: debuggable"; exit 1; fi
echo "$1: not debuggable"
