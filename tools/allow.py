"""The mobile's only resolver and only way out (tools/track.sh up runs it in a container on the Windows loopback, the
emulator started with -dns-server 127.0.0.1): DNS on udp 53 answers the allowed names with the address the mobile sees
the host at, every other name NXDOMAIN; tcp 443 relays a TLS connection to its SNI name when that name is allowed and
closes any other. python3 allow.py [dns port] [tls port]"""
import socket, struct, sys, threading

ALLOWED = {"api.runelite.net", "static.runelite.net"}
HOST = bytes([10, 0, 2, 2])   # the emulator's alias of the Windows loopback, where this listens


def qname(q):  # the question's name and the offset after it
    i, parts = 12, []
    while q[i]:
        parts.append(q[i + 1:i + 1 + q[i]].decode("ascii", "replace")); i += 1 + q[i]
    return ".".join(parts).lower(), i + 1


def answer(q):  # one A record for an allowed name, no record for its other types, NXDOMAIN for any other name
    name, end = qname(q); qtype = struct.unpack(">H", q[end:end + 2])[0]; end += 4
    ok = name in ALLOWED; a = ok and qtype == 1
    head = q[:2] + struct.pack(">HHHHH", 0x8180 if ok else 0x8183, 1, int(a), 0, 0)
    return head + q[12:end] + (b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + HOST if a else b"")


def sni(hello):  # the server name of a TLS ClientHello record, or None
    try:
        if hello[0] != 0x16 or hello[5] != 1: return None
        i = 9 + 2 + 32; i += 1 + hello[i]; i += 2 + struct.unpack(">H", hello[i:i + 2])[0]; i += 1 + hello[i]
        end = i + 2 + struct.unpack(">H", hello[i:i + 2])[0]; i += 2
        while i + 4 <= end:
            t, n = struct.unpack(">HH", hello[i:i + 4]); i += 4
            if t == 0: return hello[i + 5:i + 5 + struct.unpack(">H", hello[i + 3:i + 5])[0]].decode("ascii").lower()
            i += n
    except (IndexError, struct.error, UnicodeDecodeError): return None


def pipe(a, b):  # a's bytes to b; a's end (or error) ends b's writing, the other direction runs on
    try:
        while d := a.recv(65536): b.sendall(d)
    except OSError: pass
    try: b.shutdown(socket.SHUT_WR)
    except OSError: pass


def record(c):  # the first TLS record whole: a ClientHello with a post-quantum key share spans TCP segments
    r = c.recv(16384)
    while 5 <= len(r) < 5 + struct.unpack(">H", r[3:5])[0] and (d := c.recv(16384)): r += d
    return r


def relay(c):
    with c:
        hello = record(c); name = sni(hello)
        if name not in ALLOWED: return
        with socket.create_connection((name, 443)) as u:
            u.sendall(hello); t = threading.Thread(target=pipe, args=(u, c)); t.start(); pipe(c, u); t.join()


def main(dns=53, tls=443):
    d = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); d.bind(("0.0.0.0", dns))
    l = socket.create_server(("0.0.0.0", tls))
    threading.Thread(target=lambda: [threading.Thread(target=relay, args=(l.accept()[0],), daemon=True).start() for _ in iter(int, 1)], daemon=True).start()
    while True:
        q, peer = d.recvfrom(512)
        try: d.sendto(answer(q), peer)
        except (IndexError, struct.error): pass


if __name__ == "__main__":
    main(*map(int, sys.argv[1:]))
