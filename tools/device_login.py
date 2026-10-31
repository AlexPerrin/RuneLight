#!/usr/bin/env python3
"""device_login.py: launches the app on $ANDROID_SERIAL and signs in through LOCAL_AUTH; prints LOGGED_IN or FAILED: <screen>.
Every wait is on a fact read from the device (the host's GameStateChanged line, the focused window, the view labels).
"""
import os, re, subprocess, sys, time

ADB = os.environ.get("ADB", "/mnt/c/Users/Alex/AppData/Local/Android/Sdk/platform-tools/adb.exe")
PKG = "com.jagex.oldscape.android"
TAPS = {"SIGN_IN": (1200, 617), "ACCEPT": (1032, 645), "PLAY_NOW": (1197, 540), "TRY_AGAIN": (1197, 620)}   # canvas points, 2400x1080 landscape
LABELS = ("Got it", "Stay signed out", "Close", "Accept", "No thanks")


def adb(*a, retries=5):
    for _ in range(retries):
        r = subprocess.run([ADB, "-s", os.environ["ANDROID_SERIAL"], *a], capture_output=True, text=True)
        if "accept4 failed" not in r.stderr:
            return r.stdout.replace("\r", "")
        time.sleep(1)
    return ""


def game_state():
    m = re.findall(r"GameStateChanged (\w+)", adb("logcat", "-d", "-s", "osrsrl-host:I"))
    return m[-1] if m else ""


def tap_label():
    adb("shell", "uiautomator", "dump", "/sdcard/ui.xml")
    xml = adb("exec-out", "cat", "/sdcard/ui.xml")
    for label in LABELS:
        m = re.search(rf'text="{label}"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', xml)
        if m:
            x1, y1, x2, y2 = map(int, m.groups())
            adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
            return label
    return None


def main():
    adb("logcat", "-c")
    adb("shell", "monkey", "-p", PKG, "-c", "android.intent.category.LAUNCHER", "1")
    seen = set()
    while (st := game_state()) != "LOGGED_IN":
        if not adb("shell", "pidof", PKG).strip():
            return "FAILED: the app died"
        if st == "LOGIN_SCREEN" or st == "LOGGING_IN":
            label = tap_label()
            if label is None:
                tap = "SIGN_IN" if "SIGN_IN" not in seen else "PLAY_NOW"
                if "com.jagex.mobilesdk" in adb("logcat", "-d", "-s", "ActivityTaskManager:I"):
                    tap = "ACCEPT" if "ACCEPT" not in seen else "PLAY_NOW"
                seen.add(tap)  # shortcut: a tap is never retried (a second tap toggles panels); a missed tap waits here, upgrade when the screen can be read
                adb("shell", "input", "tap", str(TAPS[tap][0]), str(TAPS[tap][1]))
        time.sleep(3)
    return "LOGGED_IN"


if __name__ == "__main__":
    r = main()
    print(r)
    sys.exit(0 if r == "LOGGED_IN" else 1)
