#!/usr/bin/env bash
# Automated dev build and GitHub pre-release of the client (semver, https://semver.org).
#   tools/release-dev.sh              build the client repo's master, check it, tag <OSRS build>-v<VERSION>-dev.<N>, publish a GitHub pre-release
#   tools/release-dev.sh --dry          the same build and checks, without tagging or publishing
# A release is named <OSRS build>-v<our version> (docs/releasing.md): the OSRS build is the repacked base.apk's
# versionName (242.3), never typed; VERSION (the client repo's root) holds our next version only (0.1, 0.1.1, 1.0.1),
# above every released one; N counts the dev builds of that name (1, 2, ...). Dev builds are automated; an official
# release needs the maintainer's sign-off. The checks here are machine checks only:
#   - master is clean and equal to origin/main (the build is of a pushed commit);
#   - the APK is not debuggable (tools/not-debuggable.sh);
#   - the repack log has all 7 `telemetry:` lines (patch_telemetry.py asserts exactly one match each);
#   - the APKs carry no secrets or personal data (private keys, the server's RSA private exponent, the
#     user's name or email, home paths, keystores).
# The release attaches the single universal repacked APK (re/out/osrs-runelite.apk; user's decisions 2026-10-19 and
# 2026-10-24) and THIRD_PARTY_NOTICES.md (the licence notices for the third-party code in libosrsrl.so and its
# host.jar; also embedded in the APK as assets/THIRD_PARTY_NOTICES.md), plus docs/offsets.md and docs/android-protocol.md
# (the mapping of this build, published with every release). The split pair stays buildable, not attached.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
DRY=; [[ ${1:-} == --dry ]] && DRY=1
die() { echo "release-dev: $*" >&2; exit 1; }
# release_name <apktool.yml of the repacked base.apk> <VERSION> < the repo's tags: <OSRS build>-v<VERSION>, or fails naming why
release_name() {
    local osrs v last
    osrs=$(sed -n 's/^  versionName: *//p' "$1" | tr -d "'\"")
    [[ $osrs =~ ^[0-9]+\.[0-9]+$ ]] || { echo "the repacked base.apk's versionName '$osrs' is not <major>.<minor>" >&2; return 1; }
    v=$(tr -d '[:space:]' < "$2")
    [[ $v =~ ^[0-9]+(\.[0-9]+){1,2}$ ]] || { echo "VERSION '$v' is not our version (0.1, 0.1.1, 1.0.1)" >&2; return 1; }
    last=$(grep -v -- '-dev\.' | sed -n 's/^\(.*-\)\{0,1\}v\([0-9.]*\)$/\2/p' | sort -V | tail -1)   # released: vX.Y.Z and <build>-vX.Y[.Z]
    [[ -z $last || $(printf '%s\n%s\n' "$last" "$v" | sort -V | tail -1) == "$v" && $v != "$last" ]] \
        || { echo "VERSION $v is not above the released $last: set our next version" >&2; return 1; }
    echo "$osrs-v$v"
}

[[ $(git branch --show-current) == master ]] || die "not on master"
[[ -z $(git status --porcelain --untracked-files=no) ]] || die "the working tree has changes"
git fetch -q origin
[[ $(git rev-parse HEAD) == $(git rev-parse origin/main) ]] || die "master is not origin/main (push first)"
sha=$(git rev-parse HEAD)
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
echo "release-dev: master at ${sha:0:9}"

# Build (host jar, native lib, repack for the OpenRune test server with local sign-in).
host/build.sh > "$work/host.log" 2>&1 || { tail -20 "$work/host.log"; die "host/build.sh failed"; }
ninja -C re/build > "$work/ninja.log" 2>&1 || { tail -20 "$work/ninja.log"; die "ninja failed"; }
# The APK's login modulus is the test server's public RSA modulus (256 hex digits): MODULUS in the environment, or the
# Modulus line of $KEY_PAIR/client.key (the server's key pair; its private exponent never goes into the APK).
pair=${KEY_PAIR:-${HD_ROOT:-$HOME/osrs-harness}/state/live/key}
MODULUS=${MODULUS:-$(sed -n 's/^Modulus: //p' "$pair/client.key" 2>/dev/null)}
[[ ${#MODULUS} -eq 256 ]] || die "no login modulus: set MODULUS (256 hex digits) or KEY_PAIR (a directory holding client.key)"
env -u DEBUGGABLE MODULUS=$MODULUS SERVER=${SERVER:-127.0.0.1:8443} OPENRUNE=1 LOCAL_AUTH=1 tools/repack.sh > "$work/repack.log" 2>&1 || { tail -20 "$work/repack.log"; die "repack failed"; }
t=$(grep -c '^telemetry:' "$work/repack.log" || true)
[[ $t -eq 7 ]] || die "expected 7 telemetry: lines in the repack log, found $t"
name=$(git tag -l | release_name re/apk/base/apktool.yml VERSION) || die "no release name"
osrs=${name%-v*}
n=$(( $(git tag -l "$name-dev.*" | wc -l) + 1 ))
tag="$name-dev.$n"
echo "release-dev: $tag"

# Secrets and personal data, in what we add or change only: a file identical to the official APK's is skipped;
# for a changed file only the strings the official one lacks are checked (the game library and the official
# app already carry OpenSSL's "PRIVATE KEY" labels and public CA bundles).
mkdir -p "$work/x/base" "$work/x/split" "$work/x/single" "$work/o/base" "$work/o/split" "$work/o/single"
unzip -q re/out/base.apk -d "$work/x/base"; unzip -q re/out/split.apk -d "$work/x/split"
unzip -q re/out/osrs-runelite.apk -d "$work/x/single"
unzip -q base.apk -d "$work/o/base"; unzip -q split_config.arm64_v8a.apk -d "$work/o/split"
# the single APK is the base's files plus the split's lib/: its "official" counterpart is the two merged
unzip -q base.apk -d "$work/o/single"; unzip -q -o split_config.arm64_v8a.apk -d "$work/o/single"
patterns=("PRIVATE KEY" "privateExponent" "alexperrin" "Alex Perrin" "@gmail.com" 'C:\Users' "/home/alex" "/mnt/c/Users")
unzip -p re/out/osrs-runelite.apk lib/arm64-v8a/liblibs.hal.system.osclient.so | grep -aF "$MODULUS" >/dev/null || die "the APK does not carry the live pair's modulus ${MODULUS:0:8}"
d=$(openssl pkey -in "$pair/game.key" -text -noout 2>/dev/null | awk '/privateExponent:/{f=1;next} /^[a-zA-Z]/{f=0} f' | tr -d ' :\n' | sed 's/^00//' | cut -c1-24)
[[ -n $d ]] && patterns+=("$d")   # when the server's game.key is at hand, its private exponent must not be in the APK
hits=0 keys=0 scanned=0
while IFS= read -r -d '' f; do
    rel=${f#"$work/x/"}; orig="$work/o/$rel"
    [[ $rel == */META-INF/* ]] && continue   # our own signature
    # the embedded notices are the committed file (cmp'd below); third parties' own copyright lines carry their emails
    [[ $rel == single/assets/THIRD_PARTY_NOTICES.md ]] && continue
    if [[ -f $orig ]] && cmp -s "$f" "$orig"; then continue; fi
    scanned=$((scanned + 1))
    case $f in *.jks|*.keystore|*.p12|*.pem|*.key) echo "release-dev: key file $rel" >&2; keys=$((keys + 1)) ;; esac
    if [[ -f $orig ]]; then
        comm -23 <(LC_ALL=C strings -n 6 "$f" | LC_ALL=C sort -u) <(LC_ALL=C strings -n 6 "$orig" | LC_ALL=C sort -u) > "$work/new.txt"
    else
        LC_ALL=C strings -n 6 "$f" > "$work/new.txt"
    fi
    for p in "${patterns[@]}"; do
        if LC_ALL=C grep -i -q -F -- "$p" "$work/new.txt"; then
            echo "release-dev: '$p' in $rel" >&2; hits=$((hits + 1))
        fi
    done
done < <(find "$work/x" -type f -print0)
echo "release-dev: secrets check on $scanned added or changed files"
[[ $hits -eq 0 && $keys -eq 0 ]] || die "secrets check failed ($hits pattern hits, $keys key files)"

tools/not-debuggable.sh re/out/osrs-runelite.apk || die "the APK is debuggable (DEBUGGABLE=1 is for dev builds only)"
[[ -s THIRD_PARTY_NOTICES.md ]] || die "THIRD_PARTY_NOTICES.md is missing"
[[ -s docs/offsets.md && -s docs/android-protocol.md ]] || die "docs/offsets.md or docs/android-protocol.md is missing"
# The single universal APK is the release artifact (user, 2026-10-24); the split pair is no longer attached.
unzip -p re/out/osrs-runelite.apk assets/THIRD_PARTY_NOTICES.md | cmp -s - THIRD_PARTY_NOTICES.md \
    || die "the single APK does not carry this THIRD_PARTY_NOTICES.md as assets/THIRD_PARTY_NOTICES.md"
assets=("$work/osrs-runelite-$tag.apk" "$work/THIRD_PARTY_NOTICES.md" "$work/offsets.md" "$work/android-protocol.md")
cp re/out/osrs-runelite.apk "${assets[0]}"; cp THIRD_PARTY_NOTICES.md "${assets[1]}"
cp docs/offsets.md "${assets[2]}"; cp docs/android-protocol.md "${assets[3]}"
sums=$(cd "$work" && sha256sum "${assets[@]##*/}")
prev=$(git tag -l --sort=-creatordate | sed -n 1p)
changes=$(git log -n 40 --no-merges --format='- %s (%h)' ${prev:+"$prev"..}HEAD)
cat > "$work/notes.md" <<EOF
Automated dev build of \`master\` at \`${sha:0:9}\`. Not a gated release: the official releases (\`<OSRS build>-v<our version>\`)
are checked by client QA on a device; dev builds pass only the build's machine checks (telemetry patches
present, no secrets in the APKs).

**What it is.** The official Old School RuneScape Android app (build $osrs), repacked to load this repo's
\`libosrsrl.so\` and RuneLite plugin host. It connects only to an OpenRune test server on the device's own
\`127.0.0.1\` (game 43594, Central 8443: \`adb reverse tcp:43594 tcp:43594\` and \`adb reverse tcp:8443 tcp:8084\` map them
to the server's machine) and signs in with its Central (LOCAL_AUTH); it cannot sign in to Jagex. It speaks game revision ${osrs%%.*}: it needs an OpenRune server of revision ${osrs%%.*}. The app's analytics (Singular, Firebase, Crashlytics) and the
consent sync are patched out (docs/app-telemetry.md). Signed with the repo's debug key.

**Licences.** \`THIRD_PARTY_NOTICES.md\` (attached) lists the third-party code inside \`libosrsrl.so\` and its
embedded plugin host (Dear ImGui, the RuneLite client subset and API, Guice, Guava, OkHttp, protobuf-javalite, ...)
with their copyright notices and licence texts. The rest of the APK is Jagex's Old School RuneScape app, which
those licences don't cover.

**Mapping.** \`offsets.md\` (the game's addresses and struct layouts for this build) and \`android-protocol.md\`
(the Android game protocol) are attached; they are the reverse-engineering map the client was built from.

**Install** (one universal APK, arm64-v8a only: a 64-bit ARM device, or an emulator with ARM translation):
\`\`\`
adb install $(basename "${assets[0]}")
\`\`\`
It is signed with the repo's debug key, so an installed official or Play copy of the app has to be uninstalled
first (\`INSTALL_FAILED_UPDATE_INCOMPATIBLE\` otherwise). \`THIRD_PARTY_NOTICES.md\` is also inside the APK as
\`assets/THIRD_PARTY_NOTICES.md\`.

**SHA-256**
\`\`\`
$sums
\`\`\`

**Changes**${prev:+ since $prev}
$changes

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
if [[ -n $DRY ]]; then
    cp "$work/notes.md" "$ROOT/re/out/release-notes-$tag.md"
    echo "release-dev: dry run OK; notes in re/out/release-notes-$tag.md"; exit 0
fi
git tag -a "$tag" -m "Dev build $tag" "$sha"
git push -q origin "refs/tags/$tag"
gh release create "$tag" "${assets[@]}" --prerelease --title "osrs-runelite $tag (dev build)" --notes-file "$work/notes.md" --verify-tag
