"""The client track's check rows against fixture evidence: python3 -m unittest tools/test_track.py"""
import os, shutil, subprocess, tempfile, unittest
from pathlib import Path

TRACK = Path(__file__).resolve().parent / "track.sh"
GOOD = "".join(f"telemetry: line {i}\n" for i in range(7)) + "patched RSA modulus at file offset 0x1\npatched /jav_config.ws at file offset 0x2\npatched smali/net/openid/appauth/X.smali\n"


class Rows(unittest.TestCase):
    def setUp(s):
        s.ev = Path(tempfile.mkdtemp())
        s.env = {**os.environ, "HD_ROOT": tempfile.mkdtemp(), "EVIDENCE": str(s.ev), "HEAD": "abcdef1234567890", "T": "t"}

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

    def test_repo_from_worktree(s):  # run from a worktree, the script's repo is the main checkout
        m = Path(tempfile.mkdtemp()); w = m / ".worktrees" / "tools-wt"
        g = lambda *a, c=m: subprocess.run(["git", "-C", str(c), *a], check=True, capture_output=True)
        g("init", "-q"); g("config", "user.name", "t"); g("config", "user.email", "t@t"); g("commit", "-q", "--allow-empty", "-m", "c")
        g("update-ref", "refs/remotes/origin/HEAD", "HEAD"); g("worktree", "add", "-q", "--detach", str(w))
        (w / "tools").mkdir(); shutil.copy(TRACK, w / "tools" / "track.sh")
        Path(s.env["HD_ROOT"], "hd.py").write_text("print('HEAD t')\n"); (s.ev / "t.apk").write_text("")
        r = subprocess.run([str(w / "tools" / "track.sh"), "build", "x"], env=s.env, capture_output=True, text=True)
        s.assertIn("cached", r.stdout, r.stderr)
        s.assertTrue((m / ".worktrees" / "x").is_dir()); s.assertFalse((w / ".worktrees").exists())
        shutil.rmtree(m, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
