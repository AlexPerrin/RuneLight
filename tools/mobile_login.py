#!/usr/bin/env python3
"""mobile_login.py <slug>: launches the app on $ANDROID_SERIAL and signs in through the stack's issuer; prints LOGGED_IN or FAILED: <why>.
Each step acts once on the screen it reads (the host's GameStateChanged and LoginIndex lines, the focused window, the view
labels), then waits for the next screen; a screen it does not know, a refused login or the app's death is FAILED.
"""
import os, re, subprocess, sys, time

ADB = os.environ.get("ADB", "/mnt/c/Users/Alex/AppData/Local/Android/Sdk/platform-tools/adb.exe")
PKG = "com.jagex.oldscape.android"
CANVAS = {0: ("Existing User", 1370, 625), 12: ("Accept", 1027, 668), 10: ("Play Now", 1197, 540)}   # LoginIndex: the button that leaves it, on the AVD's 2400x1080 landscape canvas
WAIT = {-1, 30, 37}   # no screen yet, the browser's sign-in, the connection to the world: the next screen comes from the app, the browser or the server
REFUSED = 9           # the server's refusal ("Unexpected server response", Try again)
LABELS = ("Use without an account", "Stay signed out", "No thanks", "Close", "Accept")   # Chrome's first run and prompts before the issuer's /oauth2/auth


def adb(*a, retries=5):
    for _ in range(retries):
        r = subprocess.run([ADB, "-s", os.environ["ANDROID_SERIAL"], *a], capture_output=True, text=True)
        if "accept4 failed" not in r.stderr:
            return r.stdout.replace("\r", "")
        time.sleep(1)
    return ""


def labels():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
    xml = adb("exec-out", "cat", "/sdcard/ui.xml")
    return {m[0]: ((int(m[1]) + int(m[3])) // 2, (int(m[2]) + int(m[4])) // 2)
            for m in re.findall(r'text="([^"]+)"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)}


def screen():
    """(game state, LoginIndex, focused package, the labels of a window not the app's)."""
    log = adb("logcat", "-d", "-s", "osrsrl-host:I")
    st = re.findall(r"GameStateChanged (\w+)", log); ix = re.findall(r"LoginIndex (\d+)", log)
    focus = re.search(r"mCurrentFocus=Window\{\S+ \S+ ([^/}\s]+)", adb("shell", "dumpsys", "window"))
    pkg = focus.group(1) if focus else ""
    return st[-1] if st else "", int(ix[-1]) if ix else -1, pkg, ({} if pkg in (PKG, "") else labels())


def step(st, ix, pkg, lab):
    """The action on one screen: ('done',), ('wait',), ('tap', name, x, y) or ('fail', why)."""
    if st == "LOGGED_IN":
        return ("done",)
    if pkg == "com.android.chrome":
        err = next((t for t in lab if re.search(r"ERR_[A-Z_]+", t)), None)
        if err:
            return ("fail", f"the browser's sign-in: {err}")
        hit = next((l for l in LABELS if l in lab), None)
        return ("tap", hit, *lab[hit]) if hit else ("wait",)   # the issuer's page redirects back to the app by itself
    if pkg not in (PKG, ""):
        return ("fail", f"an unknown window {pkg}: {', '.join(lab) or 'no labels'}")
    if st != "LOGIN_SCREEN":
        return ("wait",)   # starting, LOGGING_IN, LOADING: the next state comes from the app or the server
    if ix == REFUSED:
        return ("fail", "the server refused the login (LoginIndex 9)")
    if ix in CANVAS:
        return ("tap", *CANVAS[ix])
    return ("wait",) if ix in WAIT else ("fail", f"an unknown login screen (LoginIndex {ix})")


def main():
    adb("logcat", "-c")
    adb("shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1")
    started, acted = False, None
    while True:
        st, ix, pkg, lab = screen()
        alive = bool(adb("shell", "pidof", PKG).strip())
        if started and not alive:
            return "FAILED: the app died"
        started |= alive   # monkey returns before the process exists
        act = step(st, ix, pkg, lab)
        if act[0] == "done":
            return "LOGGED_IN"
        if act[0] == "fail":
            return "FAILED: " + act[1]
        here = (st, ix, pkg, act[1:2])
        if act[0] == "tap" and here != acted:   # one tap per screen, then the next screen is read: a second tap on one screen toggles panels
            print(f"{st} LoginIndex {ix} {pkg}: {act[1]}", file=sys.stderr, flush=True)
            adb("shell", "input", "tap", str(act[2]), str(act[3])); acted = here
        time.sleep(1)


if __name__ == "__main__":
    r = main()
    print(r)
    sys.exit(0 if r == "LOGGED_IN" else 1)
