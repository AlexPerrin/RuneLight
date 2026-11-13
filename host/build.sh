#!/usr/bin/env bash
# Builds the Android plugin host: re/host/host.jar (classes*.dex), embedded into libosrsrl.so.
# Inputs (re/runelite/): runelite-api and client 1.13.1 from repo.runelite.net, and the
# client's runtime dependencies from Maven Central (guice no_aop, guava, slf4j, ...).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RL=$ROOT/re/runelite
OUT=$ROOT/re/host
. "$ROOT/tools/env.sh"
SDK=$ANDROID_SDK
ANDROID_JAR=${ANDROID_JAR:-$SDK/platforms/android-37.0/android.jar}
D8="java -cp ${D8_JAR:-$SDK/build-tools/36.0.0/lib/d8.jar} com.android.tools.r8.D8"

API=$RL/runelite-api-1.13.1.jar
DEPS=($RL/guice-4.1.0-no_aop.jar $RL/guava-23.2-jre.jar $RL/javax.inject-1.jar $RL/aopalliance-1.0.jar
      $RL/slf4j-api-1.7.25.jar $RL/jsr305-3.0.2.jar $RL/commons-text-1.2.jar $RL/commons-lang3-3.7.jar $RL/gson-2.8.5.jar
      $RL/okhttp-3.14.9.jar $RL/okio-1.17.2.jar $RL/protobuf-javalite-3.21.12.jar $RL/http-api-1.2.25.jar $RL/jopt-simple-5.0.1.jar)
CP=$(IFS=:; echo "$API:$RL/client-1.13.1.jar:${DEPS[*]}")

rm -rf "$OUT/gen" "$OUT/classes" "$OUT/client-classes" "$OUT/awt-classes" "$OUT/awt-shim.jar" "$OUT/resources"
mkdir -p "$OUT/gen/net/runelite/host" "$OUT/classes" "$OUT/client-classes"

# 1. Throwing implementations of the API interfaces; host classes override what works.
for iface in Client Player NPC Projectile GraphicsObject WorldView MessageNode widgets.Widget ItemContainer ItemComposition NPCComposition ObjectComposition Scene Tile EnumComposition; do
    name=${iface##*.}
    java -cp "$API" "$ROOT/host/gen/GenStub.java" net.runelite.api.$iface net.runelite.host ${name}Stub \
        > "$OUT/gen/net/runelite/host/${name}Stub.java"
done
# Widgets are tolerant: plugins set many properties (and create children) that aren't written
# back into the game yet; the bag stores them instead of throwing.
java -cp "$API" "$ROOT/host/gen/GenStub.java" net.runelite.api.widgets.Widget net.runelite.host WidgetBag --bag \
    > "$OUT/gen/net/runelite/host/WidgetBag.java"
# The world map isn't read from the game yet; plugins get inert bags.
for iface in worldmap.WorldMap worldmap.WorldMapData; do
    name=${iface##*.}
    java -cp "$API" "$ROOT/host/gen/GenStub.java" net.runelite.api.$iface net.runelite.host ${name}Bag --bag \
        > "$OUT/gen/net/runelite/host/${name}Bag.java"
done

# 1b. Headless java.awt/javax.swing for everything the client and the plugin jars in re/hub (and
#     re/hub-build, Plugin Hub builds from host/plugins/hub/build.sh)
#     reference but the hand-written shim lacks (plugins build Swing panels in startUp).
#     Regenerated only when an input changes.
HUB_JARS=$(ls "$ROOT"/re/hub/*.jar "$ROOT"/re/hub-build/*.jar "$ROOT"/re/devtools-build/devtools.jar 2>/dev/null || true)
# host/gen/awt-baseline-*.txt: the refs and classes of the full set of core and Hub plugins this host supports. They are
# merged into what the jars above reference, so the host compiles the same with or without the Hub jars present.
STAMP=$( (cat "$RL/client-1.13.1.jar" $HUB_JARS "$ROOT/host/gen/GenHeadless.java" "$ROOT/host/tools/awt_refs.py" "$ROOT"/host/gen/awt-baseline-*.txt; find "$ROOT/host/awt" -name '*.java' | sort) | md5sum | cut -d' ' -f1)
if [[ ! -f $OUT/headless/.stamp || $(cat "$OUT/headless/.stamp") != "$STAMP" ]]; then
    python3 "$ROOT/host/tools/awt_refs.py" "$OUT/swing-refs.txt" "$OUT/swing-classes.txt" "$RL/client-1.13.1.jar" "$API" $HUB_JARS
    LC_ALL=C sort -u "$OUT/swing-refs.txt" "$ROOT/host/gen/awt-baseline-refs.txt" -o "$OUT/swing-refs.txt"
    LC_ALL=C sort -u "$OUT/swing-classes.txt" "$ROOT/host/gen/awt-baseline-classes.txt" -o "$OUT/swing-classes.txt"
    rm -rf "$OUT/headless"
    java "$ROOT/host/gen/GenHeadless.java" "$OUT/swing-refs.txt" "$OUT/swing-classes.txt" "$ROOT/host/awt" \
        "$OUT/headless" "$ANDROID_JAR" 2>&1 | grep -v "^skipped" || true
    echo "$STAMP" > "$OUT/headless/.stamp"
fi

# 2. Compile the host, the generated stubs and the java.awt shim (host/awt) together against
#    android.jar: -source 8 with a boot class path has no module system, so the shim's
#    java.awt classes are accepted and everything is checked against what ART provides.
rm -rf "$OUT/compile-stubs" && mkdir -p "$OUT/compile-stubs"
javac -nowarn -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d "$OUT/compile-stubs" \
    $(find "$ROOT/host/compile-stubs" -name '*.java') 2>&1 | grep -v -E "^warning|^[0-9]+ warnings?$" || true
javac -nowarn -Xlint:none -source 8 -target 8 -bootclasspath "$ANDROID_JAR:$OUT/compile-stubs" -cp "$CP" -d "$OUT/classes" \
    $(find "$ROOT/host/src" "$OUT/gen" "$ROOT/host/awt" "$OUT/headless" -name '*.java') > "$OUT/javac.log" 2>&1 || true
grep -v -E "^warning|^Note|^[0-9]+ warnings?$" "$OUT/javac.log" || true
# javac still writes some classes when others fail; a partial host.jar fails at runtime instead.
if grep -qE "^[0-9]+ errors?$" "$OUT/javac.log" || [[ ! -f $OUT/classes/net/runelite/host/Host.class ]]; then
    echo "host compile failed"; exit 1
fi

# 3. The RuneLite client jar, so plugins find the utility classes, events, components and
#    services they reference. Classes only load when used, so the Swing UI costs nothing until
#    touched. Same-name host replacements are removed below.
(cd "$OUT/client-classes" && unzip -q "$RL/client-1.13.1.jar" -x 'META-INF/*')
# Classes the host replaces with same-name sources (d8 rejects duplicates).
# Keep this list in sync with host/src/net/runelite/client.
for f in config/ConfigManager config/ConfigInvocationHandler callback/ClientThread eventbus/EventBus \
         ui/overlay/OverlayManager ui/FontManager callback/Hooks plugins/PluginManager ui/ClientToolbar \
         game/ItemManager game/SpriteManager game/WorldService chat/ChatMessageManager game/chatbox/ChatboxPanelManager game/chatbox/ChatboxTextInput game/chatbox/ChatboxTextMenuInput \
         ui/overlay/outline/ModelOutlineRenderer Notifier util/WildcardMatcher util/Text \
         ui/components/colorpicker/RuneliteColorPicker ui/components/colorpicker/ColorPickerManager \
         account/SessionManager audio/AudioPlayer util/LinkBrowser; do
    rm -f "$OUT"/client-classes/net/runelite/client/$f.class "$OUT"/client-classes/net/runelite/client/$f\$*.class
done
(cd "$OUT/client-classes" && jar cf ../client-subset.raw.jar .)
# Java 9+ calls in the client classes (Moonlight potion's toArray(IntFunction), Fairy Ring's
# Collectors.toUnmodifiableMap) -> JdkCompat, as tools/dex-plugin.sh does for plugin jars.
ASM=$ROOT/re/tools/asm-9.7.jar
[[ -f $ASM ]] || curl -sfL -o "$ASM" https://repo1.maven.org/maven2/org/ow2/asm/asm/9.7/asm-9.7.jar
java -cp "$ASM" "$ROOT/host/gen/CompatRewrite.java" "$OUT/client-subset.raw.jar" "$OUT/client-subset.jar"
# Resources (fonts, runelite.properties, icons, ...) go into host.jar beside the dex files.
mkdir -p "$OUT/resources"
(cd "$OUT/client-classes" && find . -type f ! -name '*.class' -exec cp --parents {} "$OUT/resources/" \;)
# For tools/api-probe.sh: the commit this host was built from, and the host methods that reach native hooks.
git -C "$ROOT" describe --always --dirty --abbrev=12 > "$OUT/resources/osrsrl-build.txt" 2>/dev/null || echo unknown > "$OUT/resources/osrsrl-build.txt"
java -cp "$ASM" "$ROOT/host/gen/NativeCalls.java" "$OUT/classes" > "$OUT/resources/api-native.txt"
(cd "$OUT/classes" && jar cf ../host-classes.jar .)

# 4. Dex everything into one jar. Missing desktop-only classes (java.awt, Swing) only warn;
#    code touching them fails when it runs, not when it loads.
$D8 --release --min-api 26 --lib "$ANDROID_JAR" --output "$OUT/host.jar" \
    "$OUT/host-classes.jar" "$OUT/client-subset.jar" "$API" "${DEPS[@]}" 2>&1 \
    | grep -v -E "^Warning|Missing class|referenced from" | head -20 || true
(cd "$OUT/resources" && zip -qr "$OUT/host.jar" .)

# 5. java.awt members the API or the reused client classes call but the shim lacks
#    (these would be NoSuchMethodErrors at runtime). Also run it on plugin jars.
python3 "$ROOT/host/tools/check_awt.py" "$OUT/classes" "$OUT/client-subset.jar" "$API" | sed 's/^/awt shim missing: /' 
ls -la "$OUT/host.jar"
