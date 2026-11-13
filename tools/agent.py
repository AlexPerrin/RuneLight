#!/usr/bin/env python3
"""Host-side client for the in-app introspection agent (native/src/agent.cpp).

Talks to the agent through `adb shell nc` (works with Windows adb from WSL).
Setup on the device:  adb shell setprop debug.osrsrl.agent 1  (then restart the game)

  agent.py base                      game library load base
  agent.py read <addr> <len>         hexdump (addr may be lib+off, e.g. lib+aa6090)
  agent.py counters [seconds]        find u32s in .data/.bss growing at tick/cycle rate
  agent.py findptr <value> [--all]   addresses holding a u64 (e.g. a vtable lib+a69fe0)
  agent.py watch <addr> [n]          print a u32 (and neighbours) once a second

Env: ADB (default: the SDK's platform-tools adb, see tools/env.sh), ANDROID_SERIAL (required: your emulator, emulator-<port>).
"""
import os
import re
import struct
import subprocess
import sys
import time

import numpy as np

def _find_adb():
    # ANDROID_SDK / ANDROID_HOME / ~/Android/Sdk / the Windows SDK under WSL, as tools/env.sh
    import glob, shutil
    for d in [os.environ.get("ANDROID_SDK"), os.environ.get("ANDROID_HOME"), os.path.expanduser("~/Android/Sdk")] + glob.glob("/mnt/c/Users/*/AppData/Local/Android/Sdk"):
        for n in ("adb.exe", "adb"):
            p = os.path.join(d or "", "platform-tools", n)
            if d and os.access(p, os.X_OK):
                return p
    return shutil.which("adb") or "adb"


ADB = os.environ.get("ADB") or _find_adb()
SERIAL = os.environ.get("ANDROID_SERIAL") or sys.exit("set ANDROID_SERIAL to your emulator")
PORT = 27183
GAME_LIB = "liblibs.hal.system.osclient.so"
# ELF layout of the 241.4 build (readelf -S): .data + .bss are contiguous.
DATA_VADDR, DATA_END = 0xA8BBD0, 0xAA6090 + 0x151E000


def adb(*args):
    return subprocess.run([ADB, "-s", SERIAL, *args], capture_output=True, text=True, check=True).stdout


class Agent:
    def __init__(self):
        tokens = re.findall(r"agent token ([0-9a-f]{32})", adb("logcat", "-d", "-s", "osrsrl"))
        if not tokens:
            sys.exit("no agent token in logcat: setprop debug.osrsrl.agent 1 and restart the game")
        # Under WSL, an `adb forward` made by Windows adb binds Windows' loopback,
        # which WSL can't reach; tunnel through netcat on the device instead.
        self.proc = subprocess.Popen([ADB, "-s", SERIAL, "shell", "-T", "nc", "127.0.0.1", str(PORT)],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        self.buf = b""
        if self.cmd(f"auth {tokens[-1]}") != "ok":
            sys.exit("auth rejected (stale token? restart the game)")
        self.base = int(self.cmd(f"base {GAME_LIB}"), 16)

    def _line(self):
        while b"\n" not in self.buf:
            chunk = os.read(self.proc.stdout.fileno(), 1 << 20)
            if not chunk:
                raise ConnectionError("agent closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode().rstrip("\r")

    def cmd(self, line):
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()
        reply = self._line()
        if reply.startswith("err"):
            raise RuntimeError(f"{line!r}: {reply}")
        return reply

    def read(self, addr, length, chunk=8 << 20):
        return b"".join(bytes.fromhex(self.cmd(f"read {a:x} {min(chunk, addr + length - a):x}"))
                        for a in range(addr, addr + length, chunk))

    def addr(self, spec):
        """'lib+off' (ELF vaddr, as in readelf/objdump) or an absolute hex address."""
        if spec.startswith("lib+"):
            return self.base + int(spec[4:], 16)
        return int(spec, 16)


def find_counters(agent, seconds=6.0, samples=4):
    start, size = agent.base + DATA_VADDR, DATA_END - DATA_VADDR
    reads, times = [], []
    for i in range(samples):
        times.append(time.monotonic())
        reads.append(np.frombuffer(agent.read(start, size & ~3), dtype=np.uint32).astype(np.int64))
        if i + 1 < samples:
            time.sleep(seconds / (samples - 1))
    steps = np.diff(np.stack(reads), axis=0)
    monotonic = (steps > 0).all(axis=0)
    rate = (reads[-1] - reads[0]) / (times[-1] - times[0])
    for name, hz in (("game tick (600 ms)", 1 / 0.6), ("client cycle (20 ms)", 50.0)):
        hits = np.nonzero(monotonic & (np.abs(rate - hz) < hz * 0.15))[0]
        print(f"{name}: {len(hits)} candidates")
        for idx in hits[:40]:
            off = DATA_VADDR + idx * 4
            print(f"  lib+{off:x}  value {reads[-1][idx]}  rate {rate[idx]:.2f}/s")


def rw_regions(agent):
    """Writable, private mappings (heap, .data/.bss, anonymous) from /proc/self/maps."""
    agent.proc.stdin.write(b"maps\n"); agent.proc.stdin.flush()
    regions = []
    while (line := agent._line()) != ".":
        parts = line.split()
        lo, hi = (int(x, 16) for x in parts[0].split("-"))
        name = parts[5] if len(parts) > 5 else ""
        if parts[1].startswith("rw") and not name.startswith("/dev/") and hi - lo <= 512 << 20:
            regions.append((lo, hi, name))
    return regions


def find_u64(agent, value, everywhere=False):
    """Addresses holding the 8-byte little-endian `value` (8-aligned)."""
    regions = rw_regions(agent) if everywhere else [(agent.base + DATA_VADDR, agent.base + DATA_END, GAME_LIB)]
    hits = []
    for lo, hi, name in regions:
        try:
            data = np.frombuffer(agent.read(lo, hi - lo), dtype=np.uint64)
        except RuntimeError:
            continue
        hits += [(lo + int(i) * 8, name) for i in np.nonzero(data == value)[0]]
    return hits


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    agent = Agent()
    cmd = sys.argv[1]
    if cmd == "base":
        print(f"{agent.base:x}")
    elif cmd == "read":
        addr, length = agent.addr(sys.argv[2]), int(sys.argv[3], 16)
        data = agent.read(addr, length)
        for off in range(0, len(data), 16):
            row = data[off:off + 16]
            print(f"{addr + off:x}  {row.hex(' ')}")
    elif cmd == "counters":
        find_counters(agent, float(sys.argv[2]) if len(sys.argv) > 2 else 6.0)
    elif cmd == "findptr":
        value = agent.addr(sys.argv[2])
        for addr, name in find_u64(agent, value, everywhere="--all" in sys.argv):
            off = f"  (lib+{addr - agent.base:x})" if name == GAME_LIB else f"  {name}"
            print(f"{addr:x}{off}")
    elif cmd == "watch":
        addr, n = agent.addr(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 4
        while True:
            vals = struct.unpack(f"<{n}i", agent.read(addr, 4 * n))
            print(time.strftime("%H:%M:%S"), " ".join(str(v) for v in vals), flush=True)
            time.sleep(1)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
