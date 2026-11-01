#!/usr/bin/env python3
"""Points the OSRS Android game library at a private test server.

Rewrites, in place and at equal length:
  * the login RSA modulus (256 hex digits, parsed by a static initializer at load time),
  * the jav_config.ws and slr.ws URLs (copied into std::strings with a hard-coded length of 56,
    so replacements are padded to exactly that length with a dummy query parameter). The padding
    contains "jagex.com" because the client only trusts a live config (param 15 = 0) whose URL
    contains jagex.com or runescape.com (checked by a substring search).

Usage: patch_server.py IN.so OUT.so --endpoint SCHEME://HOST:PORT --modulus HEX
(the endpoint is $OPENRUNE_ENDPOINT, our server's HTTP front; a Jagex, RuneScape or Google host is refused)
"""
import argparse
import re
import sys

ORIG_MODULUS = (
    b"8094586bf7a3e7d9e8afb5a9de28542e1bef242b85c3e08c731e50c5292240ca1e934c22a79acc59ca5bf57690e74e66"
    b"56c8f9081285825d79e89f8898e272bcbe245905839a0e41f430b074e5418af3ab508f3f74e68a939a4a0cce116b4ee6"
    b"ac5863561b519baf9608c2b07bed62d7b87432f694068ae4072cfacd03cc7ecb"
)
URLS = {
    b"https://oldschool.config.runescape.com/jav_config.ws?m=0": "/jav_config.ws?m=0",
    b"https://oldschool.config.runescape.com/slr.ws?order=LPWM": "/slr.ws?order=LPWM",
}


FOREIGN = re.compile(r"(?:^|\.)(?:jagex|runescape|google[a-z]*)\.[a-z]{2,}$", re.I)


def padded_url(endpoint: str, path: str, length: int) -> bytes:
    m = re.fullmatch(r"(https?)://([^/:\s]+):(\d+)", endpoint)
    if not m:
        sys.exit(f"endpoint must be SCHEME://HOST:PORT: {endpoint!r}")
    if FOREIGN.search(m.group(2)):
        sys.exit(f"endpoint {endpoint!r} is a Jagex, RuneScape or Google host")
    url = f"{endpoint}{path}&p=jagex.com"
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
    ap.add_argument("--endpoint", required=True, help="SCHEME://HOST:PORT of the test server's HTTP front ($OPENRUNE_ENDPOINT)")
    ap.add_argument("--modulus", required=True, help="server RSA modulus, hex")
    args = ap.parse_args()

    modulus = args.modulus.strip().lower().encode()
    if len(modulus) != len(ORIG_MODULUS) or any(c not in b"0123456789abcdef" for c in modulus):
        sys.exit(f"modulus must be {len(ORIG_MODULUS)} hex digits (1024-bit key)")

    data = bytearray(open(args.input, "rb").read())
    replace_once(data, ORIG_MODULUS, modulus, "RSA modulus")
    for old, path in URLS.items():
        replace_once(data, old + b"\0", padded_url(args.endpoint, path, len(old)) + b"\0", path.split("?")[0])
    open(args.output, "wb").write(data)


if __name__ == "__main__":
    main()
