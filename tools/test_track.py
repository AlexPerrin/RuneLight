"""The client track's check rows against fixture evidence: python3 -m unittest tools/test_track.py"""
import re, os, shutil, subprocess, tempfile, unittest
from pathlib import Path

TRACK = Path(__file__).resolve().parent / "track.sh"
GOOD = "".join(f"telemetry: line {i}\n" for i in range(7)) + "patched RSA modulus at file offset 0x1\npatched /jav_config.ws at file offset 0x2\npatched smali/net/openid/appauth/X.smali\n"


class Rows(unittest.TestCase):
    def setUp(s):
        s.ev = Path(tempfile.mkdtemp())
        s.env = {**os.environ, "HD_ROOT": tempfile.mkdtemp(), "EVIDENCE": str(s.ev), "HEAD": "abcdef1234567890", "T": "t"}
        (Path(s.env["HD_ROOT"]) / "harness/changes").mkdir(parents=True); (Path(s.env["HD_ROOT"]) / "harness/changes/x.md").write_text("")

    def tearDown(s):
        shutil.rmtree(s.ev, ignore_errors=True)

    def row(s, verb):
        r = subprocess.run([str(TRACK), verb, "x"], env=s.env, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def test_log(s):
        s.assertEqual(s.row("check-log")[0], 1)
        (s.ev / "repack-t.log").write_text(GOOD)
        s.assertEqual(s.row("check-log")[0], 0)
        (s.ev / "repack-u.log").write_text(GOOD.replace("telemetry: line 6\n", ""))   # the newest log is the one checked
        rc, out = s.row("check-log")
        s.assertEqual(rc, 1); s.assertIn("talks to Jagex", out)

    def test_evidence_and_probe(s):
        s.assertEqual(s.row("check-evidence")[0], 1)
        (s.ev / "login.logcat.txt").write_text("x")
        s.assertEqual(s.row("check-evidence")[0], 0)
        s.assertEqual(s.row("check-probe")[0], 1)
        (s.ev / "api-probe.tsv").write_text("# build: 0.1.2-gabcdef12\n# result: pass 1 / 2 = 50.0%\n")
        rc, out = s.row("check-probe"); s.assertEqual((rc, "probe built at" in out), (0, True))
        (s.ev / "api-probe.tsv").write_text("# result: pass 1 / 2 = 50.0%\n")
        rc, out = s.row("check-probe"); s.assertEqual((rc, "no '# build:' line" in out), (1, True))

    def test_down_checks_the_restore(s):  # down judges the harness's baseline, not the app's writes after a first login
        st = Path(s.env["HD_ROOT"]) / "state/x"; st.mkdir(parents=True)
        (st / "serial").write_text("emulator-5560\n")
        adb = s.ev / "adb.exe"; log = s.ev / "adb.log"
        adb.write_text(f"""#!/bin/sh
echo "$*" >> {log}
case "$*" in *"echo y"*) [ -n "$NOAPP" ] || echo y ;; *"ls -1"*) printf 'quest-helper.jar\\r\\ngrounditems.jar\\r\\nopponentinfo.jar\\r\\n'; printf "${{EXTRA:-}}" ;; *"emu kill"*) exit 1 ;; esac
""")
        adb.chmod(0o755); s.env["ADB"] = str(adb)
        rc, out = s.row("down"); s.assertEqual((rc, out.strip()), (0, "restored"))
        s.assertIn("emu kill", log.read_text()); s.assertNotIn("settings.properties", log.read_text()); s.assertFalse((st / "serial").exists())
        (st / "serial").write_text("emulator-5560\n"); s.env["EXTRA"] = "probe.jar\\n"
        rc, out = s.row("down"); s.assertEqual(rc, 1); s.assertIn("RESTORE FAILED: plugins dir is not the three defaults", out)
        (st / "serial").write_text("emulator-5560\n"); s.env["NOAPP"] = "1"   # an install that failed made no plugins dir: nothing to restore
        rc, out = s.row("down"); s.assertEqual((rc, out.strip()), (0, "restored"))

    def fake_windows(s):  # adb, the emulator and netstat as scripts; a started emulator's port lists as LISTENING
        sdk = s.ev / "sdk"; sys32 = s.ev / "sys32"; log = s.ev / "win.log"
        for d in (sdk / "platform-tools", sdk / "emulator", sys32): d.mkdir(parents=True)
        HOSTS = re.search(r'^hosts="([^"]+)"', TRACK.read_text(), re.M).group(1)
        progs = {sdk / "platform-tools/adb.exe": f"""printf "%s\\n" "adb $*" >> {log}
case "$*" in *wait-for-device*) sleep 0.5 ;; *boot_completed*) echo 1 ;;
  *"dumpsys package"*) n=$(cat "$PENDING" 2>/dev/null || echo 0); [ "$n" = 0 ] || {{ echo $((n - 1)) > "$PENDING"; [ "$n" -gt 2 ] && exit 0; }}
    printf 'Active install sessions:\\n'; [ "$n" = 0 ] || printf '  Session 1:\\n    mAppPackageName=com.jagex.oldscape.andro\\n    id\\n'; printf 'Finalized install sessions:\\n' ;;
  *" install "*) echo "${{INSTALL:-Failure [INSTALL_FAILED_DUPLICATE_PACKAGE]}}" ;; *libosrsrl*) echo 1 ;;
  *dumpsys*) echo '  NetworkAgentInfo{{network{{101}}  ni{{MOBILE[NR] CONNECTED extra: }}' ;;
  *"hello-"*) [ -z "$BOOTS" ] || for h in api.runelite.net static.runelite.net; do echo "$h  16"; done ;;
  *"nc -w 15"*) [ -z "$BOOTS" ] || for h in {HOSTS}; do echo "nc: $h:443: No address associated with hostname"; done ;;
  *"echo y"*) [ -z "$BOOTS" ] || echo y ;; *"ls -1"*) printf 'grounditems.jar\\nopponentinfo.jar\\nquest-helper.jar\\n' ;; esac""",
                 sdk / "emulator/emulator.exe": f'echo "emulator $*" >> {log}; echo "emulator: booting on $PWD"',
                 sys32 / "netstat.exe": f"sed -n 's/.*-port \\([0-9]*\\).*/  TCP 127.0.0.1:\\1 0.0.0.0:0 LISTENING/p' {log}"}
        for f, body in progs.items(): f.write_text("#!/bin/sh\n" + body + "\n"); f.chmod(0o755)
        (s.ev / "t.apk").write_text(""); (s.ev / "t.devtools.jar").write_text(""); (s.ev / "repack-t.log").write_text(GOOD)
        docker = s.ev / "docker"; docker.write_text(f'#!/bin/sh\necho "docker $*" >> {log}\n'); docker.chmod(0o755)
        s.env.update(ANDROID_SDK=str(sdk), WINDOWS_SYSTEM32=str(sys32), DOCKER=str(docker)); s.env.pop("ADB", None)
        return log

    def test_a_failed_up_stops_its_emulator(s):  # the serial is known from the start, and down runs on the failure
        log = s.fake_windows()
        rc, out = s.row("up"); s.assertNotEqual(rc, 0); s.assertIn("install failed", out)
        s.assertIn("adb -s emulator-5560 emu kill", log.read_text())
        s.assertFalse((Path(s.env["HD_ROOT"]) / "state/x/serial").exists())

    def test_install_waits_for_a_pending_install(s):  # INSTALL_FAILED_DUPLICATE_PACKAGE: install once the dump shows no session of the app
        log = s.fake_windows(); s.env["INSTALL"] = "Success"
        pending = s.ev / "pending"; pending.write_text("4"); s.env["PENDING"] = str(pending)   # 2 dumps before the manager is up, 2 with a session
        s.row("up")
        calls = log.read_text().splitlines(); i = next(n for n, c in enumerate(calls) if c.startswith("adb -s emulator-5560 install "))
        s.assertEqual(sum("dumpsys package" in c for c in calls[:i]), 5)

    def test_boot_pushes_the_changes_own_devtools_jar(s):  # built with the APK, kept beside it by T; never the main checkout's
        log = s.fake_windows(); s.env.update(INSTALL="Success", BOOTS="1")
        m = Path(tempfile.mkdtemp()); (m / "tools").mkdir(); shutil.copy(TRACK, m / "tools/track.sh")   # a stub login: never the real adb
        (m / "tools/mobile_login.py").write_text("raise SystemExit(1)\n"); subprocess.run(["git", "init", "-q", str(m)], check=True)
        subprocess.run([str(m / "tools/track.sh"), "up", "x"], env=s.env, capture_output=True)
        pushes = [c for c in log.read_text().splitlines() if "push" in c and "devtools.jar" in c]
        s.assertEqual(len(pushes), 1); s.assertIn("t.devtools.jar", pushes[0]); s.assertNotIn("plugins-extra", pushes[0])
        (s.ev / "t.devtools.jar").unlink()
        rc, out = s.row("up"); s.assertEqual(rc, 1); s.assertIn("no APK or devtools jar for t", out)

    def test_emulator_has_no_console_window(s):  # started directly, not through cmd's start; its output is the run's log
        log = s.fake_windows(); s.row("up")
        s.assertNotIn("cmd.exe", TRACK.read_text())
        s.assertIn("emulator -avd Agent_Phone", log.read_text())
        s.assertIn("emulator: booting", (Path(s.env["HD_ROOT"]) / "state/x/emulator.log").read_text())

    def test_immersive_prompt_confirmed_before_launch(s):  # the setting replaces a "Got it" tap: no tap, no wait
        log = s.fake_windows(); s.env["INSTALL"] = "Success"; s.row("up")   # fails at blocked (nc fakes nothing), before any launch
        calls = log.read_text()
        s.assertIn("shell settings put secure immersive_mode_confirmations confirmed", calls)
        s.assertNotIn("monkey", calls)
        s.assertNotIn('"Got it"', (TRACK.parent / "mobile_login.py").read_text())

    def test_concurrent_boots_take_distinct_ports(s):
        log = s.fake_windows(); (Path(s.env["HD_ROOT"]) / "harness/changes/y.md").write_text("")
        ps = [subprocess.Popen([str(TRACK), "up", g], env=s.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for g in ("x", "y")]
        for p in ps: p.wait()
        s.assertEqual(sorted(re.findall(r"-port (\d+)", log.read_text())), ["5560", "5562"])
        s.assertEqual(log.read_text().count("emu kill"), 2)

    def test_a_running_emulator_holds_no_lock(s):  # the port lock ends when adb sees the emulator, not when it exits
        log = s.fake_windows(); emu = s.ev / "sdk/emulator/emulator.exe"
        emu.write_text(emu.read_text() + "exec sleep 5\n")
        subprocess.run([str(TRACK), "up", "x"], env=s.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   # returns while it runs
        r = subprocess.run(["flock", "-n", str(Path(s.env["HD_ROOT"]) / "state/emulator-port.lock"), "true"])
        s.assertEqual(r.returncode, 0)

    def test_check_blocked(s):  # up's check: the two RuneLite hosts answer TLS, every other host fails to resolve, no public address connects
        st = Path(s.env["HD_ROOT"]) / "state/x"; st.mkdir(parents=True); (st / "serial").write_text("emulator-5560\n")
        src = TRACK.read_text(); var = lambda n: re.search(rf'^{n}="([^"]+)"', src, re.M).group(1).split()
        hosts, allowed = var("hosts"), var("allowed")
        s.assertEqual(set(allowed), {"api.runelite.net", "static.runelite.net"})
        s.assertTrue({"legal.jagex.com", "oldschool.runescape.com", "sdk.iad-01.braze.com", "cdn.cookielaw.org", "app-measurement.com", "sdk-api-v1.singular.net", "google.com"} <= set(hosts))
        adb = s.ev / "adb.exe"
        adb.write_text(f"""#!/bin/sh
case "$*" in *push*) ;;
  *"hello-"*) for h in {" ".join(allowed)}; do [ "$h" = "$SHUT" ] && echo "$h " || echo "$h  16"; done ;;
  *"8.8.8.8"*) [ -z "$OPEN_ADDR" ] || echo " $OPEN_ADDR" ;;
  *"nc -w"*) for h in {" ".join(hosts)}; do [ "$h" = "$OPEN" ] || echo "nc: $h:443: No address associated with hostname"; done ;; esac
""")
        adb.chmod(0o755); s.env["ADB"] = str(adb)
        rc, out = s.row("check-blocked"); s.assertEqual(rc, 0, out); s.assertIn("allowed: api.runelite.net static.runelite.net; blocked:", out)
        s.assertEqual((st / "hello-api.runelite.net").read_bytes()[:1], b"\x16")   # a real ClientHello is pushed
        s.env["SHUT"] = "static.runelite.net"
        rc, out = s.row("check-blocked"); s.assertEqual(rc, 1); s.assertIn("NOT REACHED (no TLS answer on 443): static.runelite.net", out)
        s.env.pop("SHUT"); s.env["OPEN"] = "auth.jagex.com"
        rc, out = s.row("check-blocked"); s.assertEqual(rc, 1); s.assertIn("NOT BLOCKED (resolves or connects): auth.jagex.com", out)
        s.env.pop("OPEN"); s.env["OPEN_ADDR"] = "8.8.8.8"   # a public address connects with no lookup
        rc, out = s.row("check-blocked"); s.assertEqual(rc, 1); s.assertIn("NOT BLOCKED (resolves or connects): 8.8.8.8", out)

    def test_up_puts_the_mobile_on_the_allowlist(s):  # the allowlist container, the emulator asking it, the wifi path off
        log = s.fake_windows(); s.env["INSTALL"] = "Success"; s.row("up")
        calls = log.read_text()
        s.assertIn("docker run -d --rm --name mobile-allow -p 127.0.0.1:53:53/udp -p 127.0.0.1:443:443", calls)
        s.assertIn("-dns-server 127.0.0.1", calls); s.assertIn("shell svc wifi disable", calls)
        s.assertNotIn("private_dns", TRACK.read_text())

    def test_allow_answers_only_the_allowed_names(s):  # tools/allow.py: DNS and the TLS relay admit api/static.runelite.net alone
        import ssl, struct, sys; sys.path.insert(0, str(TRACK.parent)); import allow
        q = lambda n, t=1: b"\x12\x34\x01\x00\x00\x01\0\0\0\0\0\0" + b"".join(bytes([len(p)]) + p.encode() for p in n.split(".")) + b"\0" + struct.pack(">HH", t, 1)
        a = allow.answer(q("Static.RuneLite.net")); s.assertEqual((a[:2], a[3] & 15, a[7], a[-4:]), (b"\x12\x34", 0, 1, bytes([10, 0, 2, 2])))
        a = allow.answer(q("api.runelite.net", 28)); s.assertEqual((a[3] & 15, a[7]), (0, 0))   # no AAAA: IPv4 alone reaches the relay
        for n in ("auth.jagex.com", "sdk.iad-01.braze.com", "app-measurement.com", "runelite.net", "x.api.runelite.net"):
            a = allow.answer(q(n)); s.assertEqual((a[3] & 15, a[7]), (3, 0), n)   # NXDOMAIN
        def hello(n):
            o = ssl.MemoryBIO(); t = ssl.create_default_context().wrap_bio(ssl.MemoryBIO(), o, server_hostname=n)
            try: t.do_handshake()
            except ssl.SSLWantReadError: return o.read()
        s.assertEqual(allow.sni(hello("api.runelite.net")), "api.runelite.net"); s.assertEqual(allow.sni(hello("auth.jagex.com")), "auth.jagex.com")
        s.assertIsNone(allow.sni(b"GET / HTTP/1.1\r\n")); s.assertIsNone(allow.sni(hello("api.runelite.net")[:40]))
        import socket; x, y = socket.socketpair(); h = hello("static.runelite.net"); x.sendall(h[:100])
        threading = __import__("threading"); threading.Timer(0.2, x.sendall, (h[100:],)).start()
        s.assertEqual(allow.record(y), h); x.close(); y.close()   # a hello split across segments is read whole
        l = socket.create_server(("127.0.0.1", 0)); c = socket.create_connection(l.getsockname())
        t = threading.Thread(target=allow.relay, args=(l.accept()[0],)); t.start()
        c.sendall(hello("auth.jagex.com")); s.assertEqual(c.recv(5), b""); t.join(); c.close(); l.close()   # closed with no connect out

    def test_network_qa_allows_three_hosts(s):  # the skill's allowed list: the local server and the two RuneLite hosts, nothing else
        skill = (TRACK.parent.parent / ".claude/skills/network-qa/SKILL.md").read_text()
        allowed = skill[skill.index("- Allowed"):skill.index("- Any other host is a FAIL")]
        s.assertEqual(set(re.findall(r"10\.0\.2\.2|[\w.-]+\.(?:net|com|io)", allowed)), {"10.0.2.2", "api.runelite.net", "static.runelite.net"})
        for t in ("Braze", "OneTrust", "Google", "singular", "jagex", "runescape"): s.assertNotIn(t, allowed)

    def test_windows_programs_by_absolute_path(s):  # no .ps1 (Windows refuses it from the UNC path); no bare .exe (a lost PATH)
        s.assertEqual(list(TRACK.parent.glob("*.ps1")), [])
        src = "\n".join(l for l in TRACK.read_text().splitlines() if not l.lstrip().startswith("#"))
        s.assertNotIn("powershell", src.lower()); s.assertEqual(re.findall(r'(?:^|[\s(|;&])[\w.-]+\.exe\b', src), [])

    def test_a_missing_windows_program_names_its_variable(s):
        st = Path(s.env["HD_ROOT"]) / "state/x"; st.mkdir(parents=True); (st / "serial").write_text("emulator-5560\n")
        s.env.pop("ADB", None); s.env["ANDROID_SDK"] = "/nonexistent"
        rc, out = s.row("down"); s.assertEqual(rc, 2); s.assertIn("/nonexistent/platform-tools/adb.exe not found: set ANDROID_SDK", out)

    def test_slug_from_env(s):  # hd rows runs a contract row with no slug argument, SLUG in the env
        s.env["SLUG"] = "x"; (s.ev / "login.logcat.txt").write_text("x")
        r = subprocess.run([str(TRACK), "check-evidence"], env=s.env, capture_output=True, text=True)
        s.assertEqual(r.returncode, 0, r.stderr)

    def test_slug_names_a_change(s):  # `log PATTERN` would read the pattern as the slug
        r = subprocess.run([str(TRACK), "log", "Have a nice day"], env=s.env, capture_output=True, text=True)
        s.assertEqual(r.returncode, 2); s.assertIn('no change or goal "Have a nice day"', r.stderr)
        s.assertFalse((Path(s.env["HD_ROOT"]) / "state").exists())

    def test_repo_from_worktree(s):  # run from a worktree, the script's repo is the main checkout
        m = Path(tempfile.mkdtemp()); w = m / ".worktrees" / "tools-wt"
        g = lambda *a, c=m: subprocess.run(["git", "-C", str(c), *a], check=True, capture_output=True)
        g("init", "-q"); g("config", "user.name", "t"); g("config", "user.email", "t@t"); g("commit", "-q", "--allow-empty", "-m", "c")
        g("update-ref", "refs/remotes/origin/HEAD", "HEAD"); g("worktree", "add", "-q", "--detach", str(w))
        (w / "tools").mkdir(); shutil.copy(TRACK, w / "tools" / "track.sh")
        Path(s.env["HD_ROOT"], "hd.py").write_text("print('HEAD t')\n"); (s.ev / "t.apk").write_text(""); (s.ev / "t.devtools.jar").write_text("")
        r = subprocess.run([str(w / "tools" / "track.sh"), "build", "x"], env=s.env, capture_output=True, text=True)
        s.assertIn("cached", r.stdout, r.stderr)
        s.assertTrue((m / ".worktrees" / "x").is_dir()); s.assertFalse((w / ".worktrees").exists())
        shutil.rmtree(m, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
