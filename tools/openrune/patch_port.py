#!/usr/bin/env python3
"""OpenRune spike: changes the Android client's LIVE game port (43594) in liblibs.hal.system.osclient.so.

Build 242 picks its game port at vaddr 0x439180: `mov w11, #0xaa4a` (43594) when jav_config
param 15 (build type) is LIVE, else 40000 + world id; the default (43594, 443) pair the client
starts with is a .rodata word pair at file 0xa11c0. Both are patched. (param 15 = 2 also moves the
port to 40000 + world, but that run hit the cache crash later traced to OpenRune's custom scripts,
so it is untested on its own.)
Usage: patch_port.py IN.so OUT.so PORT
"""
import struct, sys

VADDR, OLD = 0x439180, 0x52800000 | (43594 << 5) | 11
src, dst, port = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = bytearray(open(src, "rb").read())
# ELF64 section headers: find the one containing VADDR to translate to a file offset.
shoff, = struct.unpack_from("<Q", data, 0x28)
shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
for i in range(shnum):
    o = shoff + i * shentsize
    addr, off, size = struct.unpack_from("<QQQ", data, o + 0x10)
    if addr <= VADDR < addr + size:
        fo = VADDR - addr + off
        break
else:
    sys.exit("vaddr not in any section")
cur, = struct.unpack_from("<I", data, fo)
if cur != OLD:
    sys.exit(f"unexpected instruction {cur:#x} at {fo:#x} (different client build?)")
struct.pack_into("<I", data, fo, 0x52800000 | (port << 5) | 11)
# The default (port1, port2) pair the client starts with: .rodata words (43594, 443) at file 0xa11c0.
PAIR = 0xA11C0
if struct.unpack_from("<II", data, PAIR) != (43594, 443):
    sys.exit("unexpected default port pair (different client build?)")
struct.pack_into("<I", data, PAIR, port)
open(dst, "wb").write(data)
print(f"patched game port 43594 -> {port} at file offset {fo:#x}")
