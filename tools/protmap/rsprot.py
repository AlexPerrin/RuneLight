"""RSProt sources: the desktop prots and sizes of a revision, and the field transforms each codec writes or
reads, in order (desktop encoders / decoders, and the Android module's own codecs)."""
import glob
import os
import re

VAR = {'Prot.VAR_BYTE': -1, 'Prot.VAR_SHORT': -2}
SPEC = {'p1': 'p1', 'p1a1': 'p1Alt1', 'p1a2': 'p1Alt2', 'p1a3': 'p1Alt3', 'p2': 'p2', 'p2a1': 'p2Alt1',
        'p2a2': 'p2Alt2', 'p2a3': 'p2Alt3', 'p3': 'p3', 'p3a1': 'p3Alt1', 'p3a2': 'p3Alt2', 'p3a3': 'p3Alt3',
        'p4': 'p4', 'p4a1': 'p4Alt1', 'p4a2': 'p4Alt2', 'p4a3': 'p4Alt3'}
CALL = re.compile(r'\b(?:buffer|buf|b)\.([pg](?:1|2|3|4|8|CombinedId)(?:Alt[123])?|[pg]1s|g2s|pjstr|gjstr|pdata\w*)\(')
SPECSTR = re.compile(r'"((?:\w+:p\d(?:a\d)?\s*)+)"')


def width(t):
    if t.startswith('CombinedId'):
        return 4
    m = re.match(r'(\d)', t)
    return int(m.group(1)) if m else None


def norm(call):
    """g2Alt1 / p2Alt1 / pCombinedIdAlt1 -> p2Alt1 / p4Alt1; None for a non-fixed-width call."""
    t = call[1:]
    if t.startswith('CombinedId'):
        return 'p4' + t[len('CombinedId'):]
    if t in ('1s',):
        return 'p1'
    if t == '2s':
        return 'p2'
    return 'p' + t if re.match(r'^[1-48](Alt[123])?$', t) else None


def prots(path):
    """[(name, opcode or None, size)] of an enum file (desktop: id from GameXProtId, opcode None)."""
    out = []
    for m in re.finditer(r'^\s+([A-Z][A-Z0-9_]*)\(\s*(?:\w+\.\w+|(\d+))\s*,\s*([\w.-]+)\s*\)', open(path).read(), re.M):
        sz = VAR.get(m.group(3), None)
        out.append((m.group(1), int(m.group(2)) if m.group(2) else None, sz if sz is not None else int(m.group(3))))
    return out


def units(text, ref):
    """Split Kotlin text at top-level declarations and Triple(...) rows; yield (prot names, text)."""
    parts = re.split(r'(?m)^(?=(?:public |internal |private )?(?:class|object|fun|val) )|(?=\bTriple\()', text)
    for p in parts:
        names = set(re.findall(r'\b%s\.([A-Z][A-Z0-9_]*)\b' % ref, p))
        if len(names) == 1:
            yield names.pop(), p


def layout(unit):
    """The fixed-width transforms of one codec, in order; spec strings count as reads in order."""
    specs = SPECSTR.findall(unit)
    if specs:
        return [SPEC[f.split(':')[1]] for f in specs[0].split()]
    return [norm(c) for c in CALL.findall(unit)]


def codecs(root, ref):
    """{prot: (layout, delegates)} of every codec under root that names exactly one prot of `ref`."""
    out = {}
    for f in sorted(glob.glob(os.path.join(root, '**', '*.kt'), recursive=True)):
        for prot, u in units(open(f).read(), ref):
            dele = re.search(r'AndroidDelegating(?:En|De)coder\(', u) is not None or 'readFields' in u and not SPECSTR.search(u)
            lay = layout(u)
            if prot in out and out[prot][0] and not lay:
                continue
            out[prot] = (lay, dele and not lay)
    return out


class Rev:
    """One revision's RSProt protocol tree (protocol/osrs-<rev>)."""

    def __init__(self, root, rev):
        self.base = os.path.join(root, 'protocol', 'osrs-%s' % rev)
        d = os.path.join(self.base, 'osrs-%s-desktop' % rev, 'src', 'main', 'kotlin')
        self.desktop = d
        self.s2c = {n: sz for n, _, sz in prots(glob.glob(d + '/**/outgoing/prot/GameServerProt.kt', recursive=True)[0])}
        self.c2s = {n: sz for n, _, sz in prots(glob.glob(d + '/**/incoming/prot/GameClientProt.kt', recursive=True)[0])}
        self.enc = codecs(os.path.join(d, 'net/rsprot/protocol/game/outgoing'), 'GameServerProt')
        self.dec = codecs(os.path.join(d, 'net/rsprot/protocol/game/incoming'), 'GameClientProt')
        self.android = os.path.join(self.base, 'osrs-%s-android' % rev, 'src', 'main', 'kotlin')

    def android_prots(self):
        f = glob.glob(self.android + '/**/AndroidGameServerProt.kt', recursive=True)
        g = glob.glob(self.android + '/**/AndroidGameClientProt.kt', recursive=True)
        if not f or not g:
            return None
        return prots(f[0]), prots(g[0])

    def android_codecs(self):
        """{('s2c'|'c2s', prot): layout} of the Android module; delegating codecs take desktop's."""
        out = {}
        for side, ref, desk in (('s2c', 'AndroidGameServerProt', self.enc), ('c2s', 'AndroidGameClientProt', self.dec)):
            for prot, (lay, dele) in codecs(self.android, ref).items():
                out[(side, prot)] = desk.get(prot, ([], False))[0] if dele else lay
            for alias in ('P',):
                for prot, (lay, dele) in codecs(self.android, alias).items():
                    if side == 'c2s' and lay:
                        out[(side, prot)] = lay
        return out
