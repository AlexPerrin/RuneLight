#!/usr/bin/env bash
# tools/track.sh <verb> <slug> [arg]: the client track (docs/ARCHITECTURE.md of osrs-harness). Env from hd: SLUG HEAD T
# TCOMMIT BASE CLONE EVIDENCE HD_ROOT. The Windows programs (adb, the emulator) run through WSL interop, by absolute path. build,
# check-log and tree are tools/build/build.sh's (the code a build runs, keyed alone); gate and the other check-* rows but
# check-blocked tools/gate/gate.sh's (the code a gate runs). Verbs:
#   up                refuses an APK not carrying the live stack's modulus (state/live/tree.json, written by the server track's ship), naming both; boots the change's emulator (no console window, its log in state/<slug>/emulator.log) on the network
#                     allowlist (its own DNS relay on Windows answering first, check-blocked after), installs the APK once no install of the app is pending, logs
#                     in through the stack's issuer (tools/mobile_login.py: each screen read, acted on once; an unknown one or a refusal fails); prints LOGGED_IN <serial> <devtools port>;
#                     a failed up or measure runs down (state/<slug>/serial is written as the emulator starts)
#   down              stops every login the change's ups started (mobile_login.py <slug>), removes devtools, clears the debug props, checks the plugins dir (when up made one) is the three defaults;
#                     stops the emulator and its DNS relay
#   check-blocked     the running mobile gets a TLS answer from api.runelite.net and static.runelite.net (resolved to the
#                     relay of tools/allow.py), resolves none of the Jagex, RuneScape, telemetry and Google hosts and connects
#                     to no public address (the Windows Firewall group osrs-harness); up runs it
#   ship TCOMMIT      pushes the batch's merge commit (base and its heads) to origin main
#   measure GOAL [TCOMMIT]  build and up at the target (origin/HEAD by default), the API probe; state/<goal>/measure.json
#   log <slug> PATTERN  logcat since the run's marker, filtered
#   netqa start|stop  samples the app's sockets and DNS between the two calls
# Windows locations, run through WSL interop (override in the environment):
#   ANDROID_SDK       /mnt/c/Users/Alex/AppData/Local/Android/Sdk   platform-tools/adb.exe (ADB overrides adb alone), emulator/emulator.exe
#   WINDOWS_SYSTEM32  /mnt/c/Windows/System32                      netstat.exe, nslookup.exe, tasklist.exe, taskkill.exe, WindowsPowerShell/v1.0/powershell.exe
#   DOCKER            docker                                       the allowlist container (mobile-allow)
set -euo pipefail
verb=$1; slug=${2:-${SLUG:-}}; arg=${3:-}   # a contract row passes no slug: hd exports SLUG
self=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$(git -C "${TOOLS_SRC:-$self}" rev-parse --path-format=absolute --git-common-dir)")  # the main checkout (re/ lives there), also when run from a worktree
root=${HD_ROOT:-$HOME/osrs-harness}
[ -f "$root/harness/changes/$slug.md" ] || [ -f "$root/harness/goals/$slug.md" ] || case $slug in live|integration|ship-client) ;; *) echo "no change or goal \"$slug\": tools/track.sh <verb> <slug> [arg]" >&2; exit 2 ;; esac
# a run reads its own copy of tools/, one per content in state/tools/<hash>/tools (exported: TOOLS_ROOT, its parent, where hd
# runs a gate's rows; TOOLS_SRC, the checkout): a ship of the tools branch moves the checkout under a running script, and hd's
# repairs remove a copy once no process or container uses it, and a .<hash>.<pid> being copied once its pid is gone
case $self in "$root"/state/tools/*) ;; *)
  k=$(cd "$self" && find . -type f ! -path '*/__pycache__/*' -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -c1-12); c=$root/state/tools/$k
  [ -d "$c" ] || { d=$root/state/tools/.$k.$$; rm -rf "$d"; mkdir -p "$d/tools"; cp -a "$self/." "$d/tools/"; mv -T "$d" "$c" 2>/dev/null || rm -rf "$d"; }
  TOOLS_SRC=$self TOOLS_ROOT=$c exec bash "$c/tools/track.sh" "$@" ;;
esac
case ${1:-} in build|check-log|tree) exec "$self/build/build.sh" "$@" ;; gate|check-evidence|check-probe|check-netqa|check-state-doc) exec "$self/gate/gate.sh" "$@" ;; esac
state=$root/state/$slug; mkdir -p "$state"
ev=${EVIDENCE:-$root/evidence/$slug}; mkdir -p "$ev"
copy=$repo/.worktrees/$slug-build   # measure probes in it
sdk=${ANDROID_SDK:-/mnt/c/Users/Alex/AppData/Local/Android/Sdk}; sys32=${WINDOWS_SYSTEM32:-/mnt/c/Windows/System32}; adb=${ADB:-$sdk/platform-tools/adb.exe}; ps=$sys32/WindowsPowerShell/v1.0/powershell.exe
need() { [ -x "$1" ] || { echo "$1 not found: set $2" >&2; exit 2; }; }   # need <program> <the variable that locates it>
pkg=com.jagex.oldscape.android; files=/sdcard/Android/data/$pkg/files; defaults="grounditems.jar opponentinfo.jar quest-helper.jar"
addrs="8.8.8.8 1.1.1.1 142.250.80.46 104.26.8.233"   # public addresses (Google DNS, Cloudflare DNS, google.com, runelite.net): none connects
allowed="api.runelite.net static.runelite.net"   # tools/allow.py: these resolve to its relay, which reaches them on 443
hosts="legal.jagex.com auth.jagex.com oldschool.runescape.com www.runescape.com sdk.iad-01.braze.com cdn.cookielaw.org app-measurement.com firebaseinstallations.googleapis.com sdk-api-v1.singular.net google.com connectivitycheck.gstatic.com"
docker=${DOCKER:-docker}
atomic() { cat > "$1.$$.tmp"; if [ -s "$1.$$.tmp" ]; then mv -f "$1.$$.tmp" "$1"; else rm -f "$1.$$.tmp"; return 1; fi; }   # a state record whole or not at all: hd reads it at any moment
serial() { cat "$state/serial" 2>/dev/null || { echo "no mobile for $slug: run up"; return 1; }; }
win() { wslpath -w "$1"; }
A() { local o; for _ in 1 2 3 4 5; do o=$({ "$adb" -s "$s" "$@" 2>&1 || true; } | tr -d '\r'); [[ $o == *"accept4 failed"* ]] || break; sleep 1; done; printf '%s\n' "$o"; }   # adb on the run's mobile ($s), again while WSL interop drops its stdio ("accept4 failed", as mobile_login.py); callers judge its output
pm_idle() {  # the package manager is up (its dump lists the install sessions) and no active or legacy session installs $pkg
  local d; d=$(A shell "dumpsys package | sed -n '/^Active install sessions:/,/^Finalized install sessions:/p;/^Legacy install sessions:/,+1p'" | tr -d ' \n')
  [[ $d == "Activeinstallsessions:"* && $d != *"$pkg"* ]]   # the dump wraps long lines: joined, the package name is whole
}
# a Windows process a run started through WSL interop outlives the run's Linux processes: each is recorded, its Windows pid
# and image, under the run's systemd slice (osrs-harness R5: hd ends them with the run); down drops the record
winpid() { local sl; sl=$(grep -o 'hd-[^/]*\.slice' /proc/self/cgroup) || return 0; mkdir -p "$root/state/winpids/$sl" && echo "$2" > "$root/state/winpids/$sl/$1"; }
listening() { "$sys32/netstat.exe" -anp TCP | tr -d '\r' | awk '$4=="LISTENING"{n=split($2,a,":");print a[n]}'; }   # the Windows listening ports

up() {
  T=${T:-$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["T"])' "$state/build.json")}
  [ -f "$ev/$T.apk" ] && [ -f "$ev/$T.devtools.jar" ] || { echo "no APK or devtools jar for $T: build first"; return 1; }
  EVIDENCE=$ev "$self/build/build.sh" check-log "$slug" >/dev/null
  live=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("modulus", ""))' "$root/state/live/tree.json" 2>/dev/null || true)
  mine=$(unzip -p "$ev/$T.apk" lib/arm64-v8a/liblibs.hal.system.osclient.so 2>/dev/null | grep -aoE '[0-9a-f]{256}' | grep -vE '^(00010203|80818283)' | head -1 || true)   # the patched modulus (the library's other 256-hex runs are byte tables)
  [ -n "$live" ] && [ "$mine" = "$live" ] || { l=${live:0:8}; echo "refused: $T.apk carries modulus ${mine:0:8}, the live stack (state/live/tree.json) ${l:-none, deployed before its key pair}: its login would be refused 'Invalid RSA check'"; return 1; }
  boot
  ANDROID_SERIAL=$(serial) python3 "$self/mobile_login.py" "$slug"   # its command line names the change: down stops it when up's caller died
  date -u +%FT%T.000 > "$state/log-mark"
  echo "LOGGED_IN $(serial) 7780"
}
udp53() { "$sys32/netstat.exe" -anop UDP | tr -d '\r' | awk '$1=="UDP" && $2 ~ /:53$/ {print $2, $NF}'; }   # each Windows udp 53 bind: address:53 pid
allow() {  # the mobile's resolver and relay (tools/allow.py), one container for every mobile, outliving the run, and the mobile's own
  # DNS relay on Windows (tools/allow-relay.ps1) at the first 127.0.0.N no Windows process holds udp 53 on (Internet Connection
  # Sharing holds 0.0.0.0:53, which a more specific bind overrides; the mirrored loopback carries no UDP from Windows to WSL, nor
  # TCP on port 53, so the relay asks the container over TCP on 127.0.0.1:8053); a Windows DNS query there must get its answer
  # (the emulator asks from Windows), else the container is replaced once, else the holders of udp 53 are named
  local n h held
  held=$(udp53 | cut -d' ' -f1); dns=$(for n in $(seq 2 254); do grep -qxF "127.0.0.$n:53" <<< "$held" || { echo "127.0.0.$n"; break; }; done)
  [ -n "$dns" ] || { echo "no 127.0.0.N free of a Windows udp 53 bind" >&2; return 1; }
  : > "$state/allow-relay.log"   # a previous up's log is not this relay's
  (cd /mnt/c && ALLOW_DNS=$dns WSLENV=${WSLENV:+$WSLENV:}ALLOW_DNS exec setsid "$ps" -NoProfile -NonInteractive -EncodedCommand "$(iconv -t UTF-16LE "$self/allow-relay.ps1" | base64 -w0)") \
    < /dev/null > "$state/allow-relay.log" 2>&1 9>&- &   # its text, never a file Windows runs from the UNC path; its log: "listening", or why it could not bind
  until grep -q '^listening' "$state/allow-relay.log" || ! kill -0 $! 2>/dev/null; do sleep 1; done   # it prints the line once bound, or ends
  w=$(udp53 | awk -v a="$dns:53" '$1==a{print $2; exit}')
  [ -n "$w" ] || { echo "the DNS relay did not bind $dns:53: $(head -c 500 "$state/allow-relay.log")" >&2; return 1; }
  winpid "$w" "${ps##*/}"; echo "$w" > "$state/allow-relay.winpid"
  for n in 1 2; do
    if [ "$($docker inspect -f '{{.State.Running}}' mobile-allow 2>/dev/null)" != true ]; then
      $docker run -d --rm --name mobile-allow -p 127.0.0.1:8053:53/tcp -p 127.0.0.1:443:443 -v "$self/allow.py:/allow.py:ro" python:3.13-alpine python /allow.py >/dev/null
      until $docker logs mobile-allow 2>&1 | grep '^listening' >/dev/null; do [ "$($docker inspect -f '{{.State.Running}}' mobile-allow 2>/dev/null)" = true ] || break; sleep 1; done
    fi
    "$sys32/nslookup.exe" api.runelite.net "$dns" 2>/dev/null | tr -d '\r' | grep '10\.0\.2\.2' >/dev/null && return 0
    [ $n = 2 ] || $docker rm -f mobile-allow >/dev/null 2>&1 || true
  done
  h=$(for p in $(udp53 | cut -d' ' -f2 | sort -u); do
    "$sys32/tasklist.exe" /SVC /FO CSV /NH /FI "PID eq $p" | tr -d '\r"' | awk -F, '{printf "%s (pid %s, %s) ", $1, $2, $3}'; done)
  echo "mobile-allow does not answer DNS at $dns from Windows (nslookup api.runelite.net $dns; the relay's log: $state/allow-relay.log); Windows udp 53 held by: ${h:-none}" >&2
  return 1
}
boot() {  # an Agent_Phone emulator on a free even console port, the APK in place of the official app, Chrome granted the
  # local network, the change's own devtools jar and the three default plugins pushed; the serial goes to state/<slug>/serial as the emulator starts
  need "$adb" ANDROID_SDK; need "$sdk/emulator/emulator.exe" ANDROID_SDK; need "$sys32/netstat.exe" WINDOWS_SYSTEM32; need "$sys32/nslookup.exe" WINDOWS_SYSTEM32; need "$sys32/tasklist.exe" WINDOWS_SYSTEM32; need "$sys32/taskkill.exe" WINDOWS_SYSTEM32; need "$ps" WINDOWS_SYSTEM32
  exec 9> "$root/state/emulator-port.lock"; flock 9   # one boot at a time picks a port, until adb sees its emulator there
  allow || return 1
  port=$(comm -23 <(seq 5560 2 5598 | sort) <(listening | sort -u) | sort -n | head -1); s=emulator-$port; [ -n "$port" ] || { echo "no free even port in 5560-5598" >&2; return 1; }
  (cd /mnt/c && setsid "$sdk/emulator/emulator.exe" -avd Agent_Phone -read-only -gpu swiftshader_indirect -port "$port" -dns-server "$dns" < /dev/null > "$state/emulator.log" 2>&1 &) 9>&-   # the lock is not inherited; no console window: its log is a file, down is the only stop
  echo "$s" > "$state/serial"
  "$adb" -s "$s" wait-for-device; exec 9>&-
  w=$("$sys32/netstat.exe" -ano -p TCP | tr -d '\r' | awk -v p="$port" '$4=="LISTENING"{n=split($2,a,":"); if (a[n]==p) {print $5; exit}}' || true)   # the emulator's Windows pid: its console port's holder
  img=$([ -z "$w" ] || "$sys32/tasklist.exe" /FO CSV /NH /FI "PID eq $w" | tr -d '\r"' | awk -F, -v p="$w" '$2==p{print $1}' || true)
  [ -z "$img" ] || { winpid "$w" "$img"; echo "$w" > "$state/emulator.winpid"; }
  until [ "$(A shell getprop sys.boot_completed)" = 1 ]; do sleep 2; done
  A uninstall "$pkg" >/dev/null
  until pm_idle; do sleep 1; done   # an install while one of $pkg is pending fails INSTALL_FAILED_DUPLICATE_PACKAGE
  r=$(A install -r "$(win "$ev/$T.apk")"); case $r in *Success*) ;; *) echo "install failed: $r" >&2; return 1 ;; esac
  path=$(A shell pm path "$pkg"); path=${path#package:}
  [ "$(A shell "unzip -l $path | grep -c libosrsrl")" = 1 ] || { echo "the APK carries no libosrsrl" >&2; return 1; }
  A shell pm grant com.android.chrome android.permission.ACCESS_LOCAL_NETWORK >/dev/null
  A shell svc wifi disable >/dev/null   # the wifi path (netsimd) resolves through Windows; the cellular one asks -dns-server
  cell='ni\{MOBILE[^ ]* CONNECTED'; until c=$(A shell dumpsys connectivity); [[ $c =~ $cell && $c != *"ni{WIFI"* ]]; do sleep 1; done
  A shell settings put secure immersive_mode_confirmations confirmed >/dev/null   # SystemUI's "Viewing full screen" prompt never shows: no tap
  blocked >&2 || return 1
  A shell monkey -p "$pkg" -c android.intent.category.LAUNCHER 1 >/dev/null   # a first start creates files/
  until [ "$(A shell "[ -d $files ] && echo y")" = y ]; do sleep 1; done; A shell am force-stop "$pkg" >/dev/null
  A shell mkdir -p "$files/plugins" >/dev/null; A shell chmod 777 "$files/plugins" >/dev/null
  for j in $defaults; do A push "$(win "$repo/re/host/plugins/$j")" "$files/plugins/$j" >/dev/null; done
  [ "$(A shell ls -1 "$files/plugins" | LC_ALL=C sort | paste -sd' ')" = "$defaults" ] || { echo "the three default plugins are not in place" >&2; return 1; }
  A push "$(win "$ev/$T.devtools.jar")" "$files/plugins/devtools.jar" >/dev/null
  A shell setprop debug.osrsrl.devtools 1 >/dev/null
}
check_blocked() { s=$(serial) && need "$adb" ANDROID_SDK && blocked; }
blocked() {  # each allowed host answers a TLS ClientHello with a handshake record (0x16); every other host fails to resolve
  local out no h fail=0   # (a connect by name needs a resolve first) and no public address connects
  for h in $allowed; do
    python3 -c 'import ssl,sys;o=ssl.MemoryBIO();t=ssl.create_default_context().wrap_bio(ssl.MemoryBIO(),o,server_hostname=sys.argv[1])
try: t.do_handshake()
except ssl.SSLWantReadError: sys.stdout.buffer.write(o.read())' "$h" > "$state/hello-$h"
    A push "$(win "$state/hello-$h")" "/data/local/tmp/hello-$h" >/dev/null
  done
  out=$(A shell "for h in $allowed; do echo \"\$h \$(nc -q 10 -w 15 \$h 443 < /data/local/tmp/hello-\$h | head -c 1 | od -An -tx1)\" & done; wait")
  no=$(for h in $allowed; do grep -qE "^$h +16$" <<< "$out" || echo "$h"; done)
  [ -z "$no" ] || { echo "NOT REACHED (no TLS answer on 443):" $no; fail=1; }
  out=$(A shell "for h in $hosts; do nc -w 15 \$h 443 </dev/null & done; wait")
  no=$(for h in $hosts; do grep -q "$h:443: No address associated with hostname" <<< "$out" || echo "$h"; done)
  no+=$(A shell "for a in $addrs; do nc -w 5 \$a 443 </dev/null 2>/dev/null && echo \" \$a\" & done; wait")   # by address, no lookup
  [ -z "$no" ] || { echo "NOT BLOCKED (resolves or connects):" $no; fail=1; }
  [ $fail = 0 ] && echo "allowed: $allowed; blocked: $hosts $addrs"
}
down() {  # judges what the harness put on the mobile (the default plugins, the debug props) against its baseline; the
  # app's own writes (settings.properties, cache) are not checked: they differ after any login and the read-only AVD drops them
  for p in $(pgrep -f "mobile_login\.py $slug\$" || true); do kill $p $(pgrep -P "$p") 2>/dev/null || true; done   # every login of the change's ups, with its adb children; never another change's
  if w=$(cat "$state/allow-relay.winpid" 2>/dev/null); then   # the mobile's DNS relay, by its pid and image (a reused pid is left alone)
    "$sys32/taskkill.exe" /F /PID "$w" /FI "IMAGENAME eq ${ps##*/}" >/dev/null 2>&1 || true; rm -f "$root"/state/winpids/*/"$w" "$state/allow-relay.winpid"
  fi
  s=$(serial) || return 0; need "$adb" ANDROID_SDK
  bad=()
  A shell rm -f "$files/plugins/devtools.jar" >/dev/null
  for p in devtools probe set; do A shell setprop "debug.osrsrl.$p" '""' >/dev/null; done
  [ "$(A shell "[ -d $files/plugins ] && echo y")" != y ] \
    || [ "$(A shell ls -1 "$files/plugins" | LC_ALL=C sort | paste -sd' ')" = "$defaults" ] || bad+=("plugins dir is not the three defaults")   # no dir: up never got the app to its plugins
  for p in netallow probe set where say wdump keylog projlog camlog selflog tmodelog netlog netwrite agent devtools soundlog; do
    v=$(A shell getprop "debug.osrsrl.$p" | tr -d '[:space:]'); [ -z "$v" ] || [ "$v" = 0 ] || bad+=("prop debug.osrsrl.$p = $v")
  done
  A emu kill >/dev/null; rm -f "$state/serial"
  if w=$(cat "$state/emulator.winpid" 2>/dev/null); then rm -f "$root"/state/winpids/*/"$w" "$state/emulator.winpid"; fi
  [ ${#bad[@]} = 0 ] || { (IFS=';'; echo "RESTORE FAILED: ${bad[*]}"); return 1; }; echo restored
}
ship() { git -C "$repo" push -q origin "$arg:refs/heads/main" && echo "main at ${arg:0:9}"; rm -f "$state"/gate-*.json; }   # the kept gates were of trees on the old main
measure() {
  tc=$(git -C "$repo" rev-parse "${arg:-origin/HEAD}^{commit}"); arg=$tc; T=$(git -C "$repo" rev-parse "$tc^{tree}"); T=$T "$self/build/build.sh" build "$slug" "$tc"; up
  (cd "$copy" && ANDROID_SERIAL=$(serial) tools/api-probe.sh "$ev")
  v=$(grep -oE '^# result: pass [0-9]+ / [0-9]+ = [0-9.]+%' "$ev/api-probe.tsv" | grep -oE '[0-9.]+%$' | tr -d %)
  python3 -c 'import json,sys;print(json.dumps({"tree":sys.argv[1],"value":float(sys.argv[2]),"report":sys.argv[3]}))' "${TREE:-$T}" "$v" "$ev/api-probe.tsv" | atomic "$state/measure.json"
  down; echo "client-api: $v"
}
log() { "$adb" -s "$(serial)" logcat -d -v threadtime -T "$(cat "$state/log-mark" 2>/dev/null || echo 1)" 2>/dev/null | tr -d '\r' | grep -E "${arg:-.}" || true; }
netqa() {  # start: a sampler on the mobile; stop: the distinct remote addresses (app uid 10071) and the DNS lookups
  s=$(serial); f=/sdcard/Download/netqa-$slug.txt
  if [ "$arg" = start ]; then "$adb" -s "$s" logcat -c; "$adb" -s "$s" shell "nohup sh -c 'while :; do date +%s; cat /proc/net/tcp /proc/net/tcp6; sleep 0.5; done' > $f 2>/dev/null &"; echo "sampling"
  else "$adb" -s "$s" shell pkill -f "/proc/net/tcp" || true
    "$adb" -s "$s" exec-out cat "$f" | python3 -c '
import sys, socket, struct
seen = set()
for l in sys.stdin:
    p = l.split()
    if len(p) > 7 and p[3] == "01" and p[7] == "10071":
        a, port = p[2].split(":"); ip = socket.inet_ntoa(struct.pack("<I", int(a[-8:], 16))) if len(a) <= 8 else socket.inet_ntop(socket.AF_INET6, b"".join(struct.pack("<I", int(a[i:i+8], 16)) for i in range(0, 32, 8)))
        seen.add(f"{ip}:{int(port, 16)}")
print("\n".join(sorted(seen)))'
    "$adb" -s "$s" shell rm -f "$f"; echo "--- dns"; "$adb" -s "$s" logcat -d | tr -d '\r' | grep -i "asked for" || true
  fi
}

case $verb in up|measure) trap '[ $? = 0 ] || down >&2 || true' EXIT ;; esac   # a failed boot leaves no mobile behind
case $verb in up|down|check-blocked|ship|measure|log|netqa) "${verb//-/_}" ;;
  *) echo "verbs: build up down gate check-blocked check-log check-evidence check-probe check-netqa check-state-doc tree ship measure log netqa" >&2; exit 2 ;; esac
