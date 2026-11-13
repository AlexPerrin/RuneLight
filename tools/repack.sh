#!/usr/bin/env bash
# Rebuilds the official OSRS app with libosrsrl.so injected.
#   tools/repack.sh            -> re/out/osrs-runelite.apk (ONE universal arm64 APK, the release artifact) and
#                                 the split pair re/out/{base,split}.apk, all signed with re/debug.keystore
#   tools/repack.sh --install  -> also replaces the app on the connected device (the single APK, `adb install -r`)
#   tools/repack.sh --install-split -> the same with the split pair (`adb install-multiple -r`)
#   SERVER=10.0.2.2:8080 tools/repack.sh
#                              -> also points the game at the test server (config URLs +
#                                 login RSA modulus from $SERVER_TOOLS/sidecar/data/rsa.properties, or $MODULUS)
#   LOCAL_AUTH=1 SERVER=... tools/repack.sh
#                              -> also lets the app sign in against the server's local OpenID
#                                 provider (server LOCAL_AUTH=true) instead of a Jagex account
#   GAME_PORT=40255 SERVER=... tools/repack.sh
#                              -> also patches the game port the client connects to (it ignores
#                                 jav_config for that; tools/openrune/patch_port.py)
#   DEBUGGABLE=1 ... tools/repack.sh
#                              -> also marks the app android:debuggable (run-as reaches its data): the agents'
#                                 dev builds only; tools/release-dev.sh refuses a debuggable APK
#   OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh
#                            -> the build for OpenRune ($SERVER_TOOLS, the fork's local/server-tools):
#                                 SERVER=10.0.2.2:8080 (its jav_config sidecar), GAME_PORT=43594 and
#                                 the modulus from `$SERVER_TOOLS/scripts/openrune.sh modulus`, unless set explicitly
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RE=$ROOT/re
OUT=$RE/out
. "$ROOT/tools/env.sh"
APKTOOL="java -jar $APKTOOL_JAR"
HOOK_SO=$RE/build/libosrsrl.so
KEYSTORE=$RE/debug.keystore
KS_PASS=android

[[ -f $HOOK_SO ]] || { echo "build native/ first (missing $HOOK_SO)"; exit 1; }

if [[ -n ${OPENRUNE:-} ]]; then
    SERVER=${SERVER:-10.0.2.2:8080}
    GAME_PORT=${GAME_PORT:-43594}
    MODULUS=${MODULUS:-$("${SERVER_TOOLS:-$HOME/src/openrune-work/server-tools}/scripts/openrune.sh" modulus)}
fi
mkdir -p "$OUT"

# 1. Decode classes and resources (the manifest must be text for the telemetry switches; LOCAL_AUTH
#    also edits the network security config).
rm -rf "$RE/apk/base"
$APKTOOL d -q -f "$ROOT/base.apk" -o "$RE/apk/base"
if [[ -n ${LOCAL_AUTH:-} ]]; then
    python3 -I "$ROOT/tools/patch_local_auth.py" "$RE/apk/base"
fi
# 1a. DEBUGGABLE=1, the agents' dev builds only (never a release): run-as reaches the app's data.
if [[ -n ${DEBUGGABLE:-} ]]; then
    python3 -I - "$RE/apk/base/AndroidManifest.xml" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
if "android:debuggable=" in s: sys.exit("android:debuggable already set in " + p)
t = s.replace("<application ", '<application android:debuggable="true" ', 1)
if t == s: sys.exit("<application> not found in " + p)
open(p, "w").write(t)
print("patched", p, "debuggable")
EOF
fi
# 1b. Telemetry off, every build (docs/app-telemetry.md): Singular never starts, the consent sync
#     POST to account.jagex.com is disabled, Firebase Analytics deactivated, Crashlytics collection off.
python3 -I "$ROOT/tools/patch_telemetry.py" "$RE/apk/base"

# 2. Load our library right after the game's.
SMALI=$(grep -rl 'const-string [vp][0-9]*, "libs.hal.system.osclient"' "$RE/apk/base"/smali*/com/jagex/android/)
python3 -I - "$SMALI" <<'EOF'
import re, sys
path = sys.argv[1]
src = open(path).read()
pat = re.compile(r'(const-string ([vp]\d+), "libs\.hal\.system\.osclient"\n(?:\s*(?:\.line \d+)?\n)*\s*'
                 r'invoke-static \{\2\}, Ljava/lang/System;->loadLibrary\(Ljava/lang/String;\)V\n)')
m = pat.search(src)
if not m: sys.exit("loadLibrary call not found in " + path)
reg = m.group(2)
inject = (f'\n    const-string {reg}, "osrsrl"\n\n'
          f'    invoke-static {{{reg}}}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n')
open(path, "w").write(src[:m.end()] + inject + src[m.end():])
print("patched", path)
EOF
$APKTOOL b -q "$RE/apk/base" -o "$OUT/base-unsigned.apk"

# 2b. The single-APK variant of the base: the same decoded tree with the split-required flags removed
#     (android:requiredSplitTypes/splitTypes on <manifest>, android:isSplitRequired and the
#     com.android.vending.splits.required meta-data), so the base installs without the ABI split.
rm -rf "$RE/apk/base-single"
cp -a "$RE/apk/base" "$RE/apk/base-single"
python3 -I - "$RE/apk/base-single/AndroidManifest.xml" <<'EOF'
import re, sys
p = sys.argv[1]
s = open(p).read()
n = 0
for pat in (r' android:requiredSplitTypes="[^"]*"', r' android:splitTypes="[^"]*"', r' android:isSplitRequired="[^"]*"',
            r'\s*<meta-data android:name="com\.android\.vending\.splits\.required"[^>]*/>'):
    s, k = re.subn(pat, "", s); n += k
if "requiredSplitTypes" in s or "splits.required" in s or "isSplitRequired" in s or n < 3:
    sys.exit("split-required flags not removed as expected (%d edits)" % n)
open(p, "w").write(s)
print("single: removed %d split-required attributes/meta-data" % n)
EOF
$APKTOOL b -q "$RE/apk/base-single" -o "$OUT/single-unsigned.apk"

# 3. The game library: consent sync URLs zeroed in every build (patch_telemetry.py --native, so the
#    app never posts to account.jagex.com), then optionally retargeted at the test server.
GAME_SO=$OUT/liblibs.hal.system.osclient.so
unzip -p "$ROOT/split_config.arm64_v8a.apk" lib/arm64-v8a/liblibs.hal.system.osclient.so > "$GAME_SO.orig"
python3 -I "$ROOT/tools/patch_telemetry.py" --native "$GAME_SO.orig" "$GAME_SO"
rm "$GAME_SO.orig"
if [[ -n ${SERVER:-} ]]; then
    MODULUS=${MODULUS:-$(grep '^modulus=' "${SERVER_TOOLS:-$HOME/src/openrune-work/server-tools}/sidecar/data/rsa.properties" | cut -d= -f2)}
    mv "$GAME_SO" "$GAME_SO.orig"
    python3 -I "$ROOT/tools/patch_server.py" "$GAME_SO.orig" "$GAME_SO" --server "$SERVER" --modulus "$MODULUS"
    rm "$GAME_SO.orig"
    if [[ -n ${GAME_PORT:-} ]]; then
        python3 -I "$ROOT/tools/openrune/patch_port.py" "$GAME_SO" "$GAME_SO.port" "$GAME_PORT"
        mv "$GAME_SO.port" "$GAME_SO"
    fi
fi

# 4. Add the hook library (and patched game library) to the ABI split, dropping the old signature.
python3 -I - "$ROOT/split_config.arm64_v8a.apk" "$HOOK_SO" "$OUT/split-unsigned.apk" "$GAME_SO" <<'EOF'
import sys, zipfile
src, so, dst, game_so = sys.argv[1:]
with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w") as zout:
    for item in zin.infolist():
        if item.filename.startswith("META-INF/"): continue
        data = zin.read(item.filename)
        if game_so and item.filename == "lib/arm64-v8a/liblibs.hal.system.osclient.so":
            data = open(game_so, "rb").read()
        zout.writestr(item, data)
    zout.write(so, "lib/arm64-v8a/libosrsrl.so", compress_type=zipfile.ZIP_DEFLATED)
EOF

# 4b. The single APK: the single-variant base plus every native library of the arm64 split (the game library
#     as patched in step 3, the hook library, crashlytics, ...). The split's manifest, signature and stamp
#     files are not carried over.
#     THIRD_PARTY_NOTICES.md travels inside it as assets/THIRD_PARTY_NOTICES.md (next to assets/LICENCE.txt).
python3 -I - "$OUT/single-unsigned.apk" "$OUT/split-unsigned.apk" "$OUT/osrs-runelite-unsigned.apk" "$ROOT/THIRD_PARTY_NOTICES.md" <<'EOF'
import re, sys, zipfile
base, split, dst, notices = sys.argv[1:]
with zipfile.ZipFile(base) as zb, zipfile.ZipFile(split) as zs, zipfile.ZipFile(dst, "w") as zout:
    names = set()
    for item in zb.infolist():
        # signature files go (apksigner re-signs); META-INF/services and the rest stay (kotlinx's Main dispatcher
        # is found through META-INF/services, without it the consent library crashes at sign-in)
        if re.fullmatch(r"META-INF/(MANIFEST\.MF|[^/]+\.(SF|RSA|DSA|EC))", item.filename) or item.filename.startswith("lib/"): continue
        zout.writestr(item, zb.read(item.filename)); names.add(item.filename)
    for item in zs.infolist():
        if not item.filename.startswith("lib/arm64-v8a/") or item.filename in names: continue
        zout.writestr(item, zs.read(item.filename)); names.add(item.filename)
    zout.write(notices, "assets/THIRD_PARTY_NOTICES.md", compress_type=zipfile.ZIP_DEFLATED); names.add("assets/THIRD_PARTY_NOTICES.md")
    if not {"lib/arm64-v8a/libosrsrl.so", "lib/arm64-v8a/liblibs.hal.system.osclient.so"} <= names:
        sys.exit("hook or game library missing from the single APK")
    print("single: %d entries, %d native libraries" % (len(names), sum(n.startswith("lib/") for n in names)))
EOF
rm "$OUT/single-unsigned.apk"

# 5. Align + sign with one key (all splits must share a signer).
if [[ ! -f $KEYSTORE ]]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storepass $KS_PASS -keypass $KS_PASS -alias osrsrl \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=osrsrl" >/dev/null
fi
for name in base split osrs-runelite; do
    zipalign -p -f 4 "$OUT/$name-unsigned.apk" "$OUT/$name.apk"
    apksigner sign --ks "$KEYSTORE" --ks-pass pass:$KS_PASS "$OUT/$name.apk"
    rm "$OUT/$name-unsigned.apk"
done
echo "built $OUT/osrs-runelite.apk (single) and $OUT/base.apk $OUT/split.apk (split pair)"

if [[ ${1:-} == --install || ${1:-} == --install-split ]]; then
    # Under WSL the emulator is only visible to Windows adb: ADB=/mnt/c/.../adb.exe
    SERIAL_ARGS=(); [[ -n ${ANDROID_SERIAL:-} ]] && SERIAL_ARGS=(-s "$ANDROID_SERIAL")
    apk_path() { [[ $ADB == *.exe ]] && wslpath -w "$1" || echo "$1"; }
    # Update in place so app data (the Jagex sign-in) survives. Only when the installed app has a
    # different signer (e.g. the Play build) does it have to be uninstalled first.
    if [[ ${1:-} == --install ]]; then
        install() { "$ADB" "${SERIAL_ARGS[@]}" install -r "$(apk_path "$OUT/osrs-runelite.apk")"; }
    else
        install() { "$ADB" "${SERIAL_ARGS[@]}" install-multiple -r "$(apk_path "$OUT/base.apk")" "$(apk_path "$OUT/split.apk")"; }
    fi
    if ! out=$(install 2>&1); then
        echo "$out"
        if [[ $out == *INSTALL_FAILED_UPDATE_INCOMPATIBLE* ]]; then
            echo "signer differs: uninstalling the installed app (this signs it out)"
            "$ADB" "${SERIAL_ARGS[@]}" uninstall com.jagex.oldscape.android
            install
        else
            exit 1
        fi
    else
        echo "$out"
    fi
fi
