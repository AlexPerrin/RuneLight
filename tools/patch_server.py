#!/usr/bin/env python3
"""Points the OSRS Android game library at a private test server.

Rewrites, in place and at equal length:
  * the login RSA modulus (256 hex digits, parsed by a static initializer at load time),
  * the jav_config.ws and slr.ws URLs (copied into std::strings with a hard-coded length of 56,
    so replacements are padded to exactly that length with a dummy query parameter). The padding
    contains "jagex.com" because the client only trusts a live config (param 15 = 0) whose URL
    contains jagex.com or runescape.com (checked by a substring search).

Usage: patch_server.py IN.so OUT.so --server HOST:PORT --modulus HEX
"""
import argparse
import sys

ORIG_MODULUS = (  # build 242
    b"bd19de446b73406a240bba065c8ed63398f499cbf59187c68acb6fa31647bebebd83d893ac132a602f1eb45d66ec377c"
    b"c005da7480fd6bec8ca737712d2c5e83ee13dcf5cbcf7de64e57794043eb11fb57efde72b910b66e6350c0f7c5a056ee"
    b"c6358d239f15e54ed15687877b3daea4edaeed128c8ac1bac23eb9e45b4438e1"
)
URLS = {
    b"https://oldschool.config.runescape.com/jav_config.ws?m=0": "/jav_config.ws?m=0",
    b"https://oldschool.config.runescape.com/slr.ws?order=LPWM": "/slr.ws?order=LPWM",
}


def padded_url(server: str, path: str, length: int) -> bytes:
    url = f"http://{server}{path}&p=jagex.com"
    if len(url) > length:
        sys.exit(f"server address too long: {url!r} exceeds {length} chars")
    return (url + "x" * (length - len(url))).encode()


def replace_once(data: bytearray, old: bytes, new: bytes, what: str) -> None:
    assert len(old) == len(new), what
    count = data.count(old)
    if count != 1:
        sys.exit(f"{what}: expected exactly one occurrence, found {count} (different client build?)")
    i = data.find(old)
    data[i:i + len(old)] = new
    print(f"patched {what} at file offset {i:#x}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--server", required=True, help="HOST:PORT of the test server's HTTP endpoint")
    ap.add_argument("--modulus", required=True, help="server RSA modulus, hex")
    args = ap.parse_args()

    modulus = args.modulus.strip().lower().encode()
    if len(modulus) != len(ORIG_MODULUS) or any(c not in b"0123456789abcdef" for c in modulus):
        sys.exit(f"modulus must be {len(ORIG_MODULUS)} hex digits (1024-bit key)")

    data = bytearray(open(args.input, "rb").read())
    replace_once(data, ORIG_MODULUS, modulus, "RSA modulus")
    for old, path in URLS.items():
        replace_once(data, old + b"\0", padded_url(args.server, path, len(old)) + b"\0", path.split("?")[0])
    open(args.output, "wb").write(data)


if __name__ == "__main__":
    main()
