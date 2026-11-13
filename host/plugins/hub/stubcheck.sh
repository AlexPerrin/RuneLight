#!/usr/bin/env bash
# Static API check for a plugin jar: lists the RuneLite API interface methods it calls that every
# host implementation leaves to a generated *Stub/*Bag (throws or inert), or that no host class
# implements at all (e.g. Projectile getters).
#   host/plugins/hub/stubcheck.sh re/hub-build/<name>.jar ...
# Needs host/build.sh output (re/host/classes). Prints "GAP Iface.method impls=[...]" per gap.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
. "$(dirname "$0")/../../../tools/env.sh"
SDK=$ANDROID_SDK
AJ=$(ls "$SDK"/platforms/*/android.jar | tail -1)
OUT=$ROOT/re/hub-build/stubcheck
mkdir -p "$OUT"
[[ $OUT/StubCheck.class -nt $ROOT/host/plugins/hub/StubCheck.java ]] || javac -d "$OUT" "$ROOT/host/plugins/hub/StubCheck.java"
CP="$OUT:$ROOT/re/host/classes:$(ls "$ROOT"/re/runelite/*.jar | tr '\n' ':')$AJ"
for jar in "$@"; do
    echo "== $jar"
    javap -c -p -cp "$jar" $(unzip -Z1 "$jar" | grep '\.class$' | sed 's/\.class$//' | tr / .) 2>/dev/null \
        | grep -oE "(Interface)?Method net/runelite/api/[A-Za-z0-9/\$]+\.[A-Za-z0-9_]+:[^ ]+" \
        | sed -E 's/^(Interface)?Method //; s/\.([A-Za-z0-9_]+):/ \1 /' | sort -u \
        | java -cp "$CP" StubCheck "$ROOT/re/host/classes"
done
