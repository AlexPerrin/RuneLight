#!/usr/bin/env bash
# Converts a RuneLite plugin jar (e.g. from the Plugin Hub) for the Android host:
#   tools/dex-plugin.sh plugin.jar out.jar
# The output holds classes*.dex plus plugin-classes.txt (classes annotated with
# @PluginDescriptor). Push it to /sdcard/Android/data/com.jagex.oldscape.android/files/plugins/.
#
# Core RuneLite plugins are already inside host.jar (it embeds the client jar), so they need no
# dex; this writes a jar holding only plugin-classes.txt for one plugin package:
#   tools/dex-plugin.sh --core grounditems re/host/plugins/grounditems.jar
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RL=$ROOT/re/runelite

if [[ ${1:-} == --core ]]; then
    PKG=$2
    OUT=$(realpath -m "$3")
    WORK=$(mktemp -d)
    trap 'rm -rf "$WORK"' EXIT
    (cd "$WORK" && unzip -q "$RL/client-1.13.1.jar" "net/runelite/client/plugins/$PKG/*.class")
    (cd "$WORK" && grep -l 'Lnet/runelite/client/plugins/PluginDescriptor;' net/runelite/client/plugins/"$PKG"/*.class || true) \
        | sed -e 's|\.class$||' -e 's|/|.|g' > "$WORK/plugin-classes.txt"
    [[ -s $WORK/plugin-classes.txt ]] || { echo "no plugin class in net.runelite.client.plugins.$PKG"; exit 1; }
    echo "plugin classes: $(tr '\n' ' ' < "$WORK/plugin-classes.txt")"
    rm -f "$OUT"
    (cd "$WORK" && zip -q "$OUT" plugin-classes.txt)
    ls -la "$OUT"
    exit 0
fi
. "$(dirname "$0")/env.sh"
SDK=$ANDROID_SDK
ANDROID_JAR=${ANDROID_JAR:-$SDK/platforms/android-37.0/android.jar}
IN=$(realpath "$1")
OUT=$(realpath -m "$2")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Java 9+ calls Android 9 lacks (InputStream.readAllBytes, ...) -> net.runelite.host.compat.JdkCompat.
ASM=$ROOT/re/tools/asm-9.7.jar
[[ -f $ASM ]] || curl -sfL -o "$ASM" https://repo1.maven.org/maven2/org/ow2/asm/asm/9.7/asm-9.7.jar
java -cp "$ASM" "$ROOT/host/gen/CompatRewrite.java" "$IN" "$WORK/rewritten.jar"
IN_CLASSES=$IN
IN=$WORK/rewritten.jar

CLASSPATH_ARGS=()
for j in "$RL"/runelite-api-1.13.1.jar "$RL"/client-1.13.1.jar "$RL"/guice-4.1.0-no_aop.jar "$RL"/guava-23.2-jre.jar \
         "$RL"/javax.inject-1.jar "$RL"/slf4j-api-1.7.25.jar "$RL"/commons-text-1.2.jar "$RL"/commons-lang3-3.7.jar "$RL"/gson-2.8.5.jar "$RL"/jopt-simple-5.0.1.jar; do
    CLASSPATH_ARGS+=(--classpath "$j")
done
java -cp "${D8_JAR:-$SDK/build-tools/36.0.0/lib/d8.jar}" com.android.tools.r8.D8 --release --min-api 26 \
    --lib "$ANDROID_JAR" "${CLASSPATH_ARGS[@]}" --output "$WORK/dex.zip" "$IN" 2>&1 \
    | grep -v -E "^Warning|Missing class|referenced from" | head -20 || true

mkdir "$WORK/classes"
(cd "$WORK/classes" && unzip -q "$IN_CLASSES" '*.class')
(cd "$WORK/classes" && grep -rl --include='*.class' 'Lnet/runelite/client/plugins/PluginDescriptor;' . || true) \
    | sed -e 's|^\./||' -e 's|\.class$||' -e 's|/|.|g' > "$WORK/plugin-classes.txt"
echo "plugin classes: $(tr '\n' ' ' < "$WORK/plugin-classes.txt")"
cp "$WORK/dex.zip" "$OUT"
(cd "$WORK" && zip -q "$OUT" plugin-classes.txt)
# Resources (json, images, ...) stay loadable through the plugin's class loader.
mkdir "$WORK/res"
(cd "$WORK/res" && unzip -q "$IN_CLASSES" -x '*.class' 'META-INF/*' 2>/dev/null || true)
# The manifest too (plugins read their Implementation-Version), not signatures.
(cd "$WORK/res" && unzip -q "$IN_CLASSES" META-INF/MANIFEST.MF 2>/dev/null || true)
if [[ -n $(ls -A "$WORK/res") ]]; then (cd "$WORK/res" && zip -qr "$OUT" .); fi
ls -la "$OUT"
