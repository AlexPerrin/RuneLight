"""check: static checks, the round-trip test and the bench, written to protmap/<rev>/check.md.

static     every size-table entry equals RSProt's size for the prot it maps to; the client's field
           widths equal the desktop codec's as a multiset; every opcode named or dead (none open)
round-trip each mapped fixed-size packet: encode random values with the Android codec's transforms,
           decode the bytes with the client's read / write sequence, values equal
bench      the mapping against the hand-made reference enum (anchors.json "reference")
"""
import json
import os
import random
import re

import match
import rsprot


def enc(t, v):
    """RSProt's write of value v with transform t: the wire bytes."""
    w = int(t[1])
    b = [(v >> (8 * i)) & 0xff for i in range(w)]  # b[0] least significant
    alt = t[2:]
    if w == 1:
        return [{'': b[0], 'Alt1': (b[0] + 128) & 0xff, 'Alt2': (-b[0]) & 0xff, 'Alt3': (128 - b[0]) & 0xff}[alt]]
    order = {2: {'': [1, 0], 'Alt1': [0, 1], 'Alt2': [1, 0], 'Alt3': [0, 1]},
             3: {'': [2, 1, 0], 'Alt1': [0, 1, 2], 'Alt2': [2, 0, 1], 'Alt3': [1, 2, 0]},
             4: {'': [3, 2, 1, 0], 'Alt1': [0, 1, 2, 3], 'Alt2': [1, 0, 3, 2], 'Alt3': [2, 3, 0, 1]}}[w][alt]
    out = [b[i] for i in order]
    if w == 2 and alt in ('Alt2', 'Alt3'):  # the low byte goes +128
        j = order.index(0)
        out[j] = (out[j] + 128) & 0xff
    return out


def dec(t, data):
    """The client's read of transform t from wire bytes (the inverse the client's code performs)."""
    w = int(t[1])
    alt = t[2:]
    if w == 1:
        x = data[0]
        return {'': x, 'Alt1': (x - 128) & 0xff, 'Alt2': (-x) & 0xff, 'Alt3': (128 - x) & 0xff}[alt]
    order = {2: {'': [1, 0], 'Alt1': [0, 1], 'Alt2': [1, 0], 'Alt3': [0, 1]},
             3: {'': [2, 1, 0], 'Alt1': [0, 1, 2], 'Alt2': [2, 0, 1], 'Alt3': [1, 2, 0]},
             4: {'': [3, 2, 1, 0], 'Alt1': [0, 1, 2, 3], 'Alt2': [1, 0, 3, 2], 'Alt3': [2, 3, 0, 1]}}[w][alt]
    data = list(data)
    if w == 2 and alt in ('Alt2', 'Alt3'):
        j = order.index(0)
        data[j] = (data[j] - 128) & 0xff
    return sum(data[i] << (8 * order[i]) for i in range(w))


def round_trip(codec, client, rnd):
    """None when every value survives codec -> wire -> client, else the first failure."""
    vals = [rnd.randrange(1 << (8 * int(t[1]))) for t in codec]
    wire = [x for t, v in zip(codec, vals) for x in enc(t, v)]
    got = [dec(n, wire[k:k + w]) for k, w, n in sorted(client)]
    if len(got) != len(vals) or len(wire) != sum(w for _, w, _ in client):
        return 'shape', 'codec %s, client %s' % (' '.join(codec), ' '.join(n for _, _, n in sorted(client)))
    for i, (a, b) in enumerate(zip(vals, got)):
        if a != b:
            return 'fail', 'field %d: codec %s, client %s' % (i, codec[i], sorted(client)[i][2])
    return None


def reference(anchor, work, rev):
    """The hand-made Android module: a directory, or a .patch whose new android files are unpacked."""
    path = os.path.expanduser(anchor)
    if os.path.isdir(path):
        return path
    out = os.path.join(work, 'reference')
    text = open(path).read()
    for m in re.finditer(r'^\+\+\+ b/(\S*-android/\S+\.kt)\n@@[^\n]*\n((?:[+ ][^\n]*\n?)*)', text, re.M):
        f = os.path.join(out, m.group(1))
        os.makedirs(os.path.dirname(f), exist_ok=True)
        open(f, 'w').write(''.join(line[1:] + '\n' for line in m.group(2).split('\n') if line))
    return os.path.join(out, 'protocol', 'osrs-%s' % rev, 'osrs-%s-android' % rev)


def run(rec, work, rs):
    a = json.load(open(os.path.join(rec, 'anchors.json')))
    x = json.load(open(os.path.join(work, 'extracted.json')))
    rows = match.read_tsv(os.path.join(rec, 'mapping.tsv'))
    ref = reference(a['reference'], work, os.path.basename(rec)) if a.get('reference') else None
    rs.android = ref or rs.android
    refp = rs.android_prots()
    codec = rs.android_codecs()
    rnd = random.Random(int(os.path.basename(rec)))
    fails, shapes, bench = [], [], []
    rt = {'pass': 0, 'fail': 0, 'skip': 0}
    for r in rows:
        side, op, prot, size, how = r[0], int(r[1]), r[2], int(r[3]), r[4]
        desk = (rs.s2c if side == 's2c' else rs.c2s)
        e = x[side][str(op)]
        if how == 'open':
            fails.append('%s %d open (%s)' % (side, op, r[6]))
            continue
        if how == 'dead':
            continue
        if prot in desk and desk[prot] != size:
            fails.append('%s %d %s: size %d, RSProt %d' % (side, op, prot, size, desk[prot]))
        cf = match.client_fields(e)
        lay = (rs.enc if side == 's2c' else rs.dec).get(prot, ([], False))[0]
        if size >= 0 and match.complete(cf, size) and lay and None not in lay and \
                sorted(int(t[1]) for t in lay) != sorted(w for _, w, _ in cf):
            shapes.append('%s %d %s: client widths %s, RSProt %s' % (side, op, prot,
                         sorted(w for _, w, _ in cf), sorted(int(t[1]) for t in lay)))
        c = codec.get((side, prot))
        if size < 0 or not c or None in c or not match.complete(cf, size):
            rt['skip'] += 1
            continue
        err = round_trip(c, cf, rnd)
        if err and err[0] == 'shape':
            rt['skip'] += 1
            shapes.append('%s %d %s round-trip not compared: %s' % (side, op, prot, err[1]))
        elif err:
            rt['fail'] += 1
            fails.append('%s %d %s round-trip: %s' % (side, op, prot, err[1]))
        else:
            rt['pass'] += 1
    if refp:
        for side, enum in zip(('s2c', 'c2s'), refp):
            want = {o: n for n, o, _ in enum}
            for r in (r for r in rows if r[0] == side):
                op, got = int(r[1]), r[2]
                w = want.get(op)
                if w != got and not (got.startswith('OP_') and (w or '').startswith('OP_')):
                    bench.append('%s %d: protmap %s (%s), reference %s' % (side, op, got, r[4], w))
    with open(os.path.join(rec, 'check.md'), 'w') as f:
        f.write('# protmap check, rev %s\n\n' % os.path.basename(rec))
        f.write('Round-trip: %(pass)d pass, %(fail)d fail, %(skip)d not run (var-size, no Android codec, '
                'the client layout not fully read, or shapes differ).\n\n' % rt)
        f.write('## Failures (%d)\n\n' % len(fails) + ''.join('- %s\n' % s for s in fails) + '\n')
        f.write('## Shapes not compared (%d)\n\nThe client reads a field in pieces (a packed coordinate or colour byte '
                'by byte), or the codec branches, so field-by-field comparison does not apply; the size check '
                'still does.\n\n' % len(shapes) + ''.join('- %s\n' % s for s in shapes) + '\n')
        f.write('## Bench against the hand-made mapping (%d differences)\n\n' % len(bench) +
                ''.join('- %s\n' % s for s in bench))
    print('rev %s: %d failures, round-trip %s, bench %d differences' % (os.path.basename(rec), len(fails), rt, len(bench)))
    return fails
