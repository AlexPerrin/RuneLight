"""Read and write sequences from Ghidra's decompiled C (the client's own packet code).

A packet buffer is a base B and a position P. Every load or store whose address is B + P + k is a wire
byte at offset k; how the code combines and transforms those bytes gives the fields: offset, width and
Jagex transform (p1 .. p4Alt3, the names RSProt's encoders use). Straight-line symbolic substitution
over the C text: no control flow, so a case that branches before a read yields a partial layout, which
validate.py reports instead of trusting.
"""
import re

CAST = re.compile(r'\((?:u?int|u?long|byte|char|u?short|undefined\d?|uint3|int3|bool|long long|ulonglong|longlong)\)')
PTRCAST = re.compile(r'\(((?:u?int|u?long|byte|char|u?short|undefined\d?|uint3|int3)) \*\)')
WIDTH = {'byte': 1, 'char': 1, 'undefined1': 1, 'undefined': 1, 'ushort': 2, 'short': 2, 'undefined2': 2,
         'uint3': 3, 'int3': 3, 'uint': 4, 'int': 4, 'undefined4': 4, 'ulong': 8, 'long': 8, 'undefined8': 8}
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')


class Lin:
    """A linear form over the symbols B and P, enough to evaluate an address expression."""
    def __init__(self, c=None, k=0):
        self.c, self.k = dict(c or {}), k

    def __add__(self, o):
        o = o if isinstance(o, Lin) else Lin(k=o)
        c = dict(self.c)
        for s, v in o.c.items():
            c[s] = c.get(s, 0) + v
        return Lin({s: v for s, v in c.items() if v}, self.k + o.k)
    __radd__ = __add__

    def __sub__(self, o):
        return self + (o * -1 if isinstance(o, Lin) else -o)

    def __mul__(self, n):
        if isinstance(n, Lin):
            raise TypeError
        return Lin({s: v * n for s, v in self.c.items()}, self.k * n)
    __rmul__ = __mul__


def offset(addr, sym):
    """k when addr evaluates to B + P + k under sym (name -> 'B' / 'P' / 'P+k' text), else None."""
    e = CAST.sub('', PTRCAST.sub('', addr))
    names = {n: Lin({'B': 1}) if v == 'B' else Lin({'P': 1}, int(v[1:] or 0)) for n, v in sym.items()}
    if set(IDENT.findall(e)) - set(names):
        return None
    try:
        v = eval(e, {'__builtins__': {}}, names)
    except Exception:
        return None
    return v.k if isinstance(v, Lin) and v.c == {'B': 1, 'P': 1} else None


def statements(body):
    """The body's statements in text order, control-flow keywords dropped."""
    text = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    text = re.sub(r'\b(?:if|while|for|switch)\s*\(', '(', text)
    for s in re.split(r'[;{}]', text):
        s = ' '.join(s.split())
        if s and not re.match(r'^(?:else|do|break|return|goto|case|default|LAB_|switchD|code|continue)\b', s):
            yield s


def _top_split(s, sep=','):
    out, depth, cur = [], 0, ''
    for ch in s:
        depth += ch in '([' and 1 or ch in ')]' and -1 or 0
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    return out + [cur]


class Seq:
    """One packet's accesses: sym tracks B / P aliases, ptr tracks pointers into the buffer, reads the
    byte tokens R<k>w<n>; uses collects every expression that consumes a token (or is stored at k)."""

    def __init__(self, sym, write=False):
        self.sym, self.ptr, self.val, self.write = dict(sym), {}, {}, write
        self.uses, self.stores, self.loads = [], {}, {}

    def _access(self, m_type, addr):
        k = offset(addr, self.sym)
        if k is None:
            return None
        return k, WIDTH.get(m_type, 1)

    def subst(self, e):
        """e with buffer loads replaced by tokens and known values substituted."""
        def load(m):
            acc = self._access(m.group(1), m.group(2))
            if acc is None:
                return m.group(0)
            self.loads.setdefault(acc[0], acc[1])
            return ' R%dw%d ' % acc
        for _ in range(3):
            e = re.sub(r'\*\((\w+) \*\)\(([^()]*(?:\([^()]*\)[^()]*)*)\)', load, e)
        def pidx(m):
            n, i = m.group(1), int(m.group(2), 0) if m.re.groups > 1 else 0
            if n not in self.ptr:
                return m.group(0)
            k, w = self.ptr[n]
            self.loads.setdefault(k + i * w, w)
            return ' R%dw%d ' % (k + i * w, w)
        def inl(m):
            acc = self._access(m.group(1), m.group(2))
            if acc is None:
                return m.group(0)
            k = acc[0] + int(m.group(3), 0) * acc[1]
            self.loads.setdefault(k, acc[1])
            return ' R%dw%d ' % (k, acc[1])
        e = re.sub(r'\(\((\w+) \*\)\(([^()]*)\)\)\[(0x[0-9a-f]+|\d+)\]', inl, e)
        e = re.sub(r'\b(\w+)\[(0x[0-9a-f]+|\d+)\]', pidx, e)
        e = re.sub(r'\*(\w+)\b', lambda m: pidx(m) if m.group(1) in self.ptr else m.group(0), e)
        return IDENT.sub(lambda m: '(%s)' % self.val[m.group(0)] if m.group(0) in self.val else m.group(0), e)

    def step(self, s):
        m = re.match(r'^(.*?[^=!<>])=([^=].*)$', s)
        if not m:
            for a in _top_split(re.sub(r'^\w+\((.*)\)$', r'\1', s)):
                self._use(self.subst(a))
            return
        lhs, rhs = m.group(1).strip(), m.group(2).strip()
        if lhs.endswith(('+', '-', '*', '|', '&', '^')):
            return
        mt = re.match(r'^\*\((\w+) \*\)\((.*)\)$', lhs) or re.match(r'^\*\((\w+) \*\)(\w+)$', lhs)
        if mt or re.match(r'^\w+\[', lhs) or lhs.startswith('*'):
            acc = None
            if mt:
                acc = self._access(mt.group(1), mt.group(2))
                if acc is None and self.write and mt.group(2) in self.sym:
                    acc = (int(self.sym[mt.group(2)][1:] or 0), WIDTH.get(mt.group(1), 1)) if self.sym[mt.group(2)] != 'B' else None
            mp = re.match(r'^(\w+)\[(0x[0-9a-f]+|\d+)\]$', lhs) or re.match(r'^\*(\w+)$', lhs)
            if mp and mp.group(1) in self.ptr:
                k, w = self.ptr[mp.group(1)]
                i = int(mp.group(2), 0) if mp.lastindex == 2 else 0
                acc = (k + i * w, w)
            v = self.subst(rhs)
            if acc and self.write:
                self.stores.setdefault(acc[0], (acc[1], v))
            else:
                self._use(v)
            return
        lhs = re.sub(r'^\w[\w ]*\s+\**(?=\w+$)', '', lhs)
        if not re.match(r'^\w+$', lhs):
            return
        prevval = self.val.get(lhs, '')
        self.ptr.pop(lhs, None)
        self.val.pop(lhs, None)
        mp = re.match(r'^\((\w+) \*\)\((.*)\)$', rhs) or re.match(r'^\((\w+) \*\)(\w+)$', rhs)
        if mp and offset(mp.group(2), self.sym) is not None:
            self.ptr[lhs] = (offset(mp.group(2), self.sym), WIDTH.get(mp.group(1), 1))
            self.sym.pop(lhs, None)
            return
        if self.write and self.sym.get(lhs) == 'B':
            return
        if self.write and lhs in self.sym and self._pos(rhs) is None:
            self.sym[lhs] = 'P0'
            self.stores.clear()
            return
        if lhs in self.sym or self._symbolic(rhs):
            k = offset(rhs, self.sym)
            pos = self._pos(rhs)
            self.sym.pop(lhs, None)
            if pos is not None:
                self.sym[lhs] = 'P%d' % pos
                return
            if k is None and rhs.strip() in self.sym and self.sym[rhs.strip()] == 'B':
                self.sym[lhs] = 'B'
                return
        v = self.subst(rhs)
        if self.write:
            prev = prevval
            if re.search(r'<<\s*8\b', prev) and re.search(r'>>\s*8\b|\|', prev) and len(v) < len(prev):
                self.val[lhs] = prev
                return  # the endianness check's swap branch, then its no-swap branch: the wire is the swap
            if len(v) < 400:
                self.val[lhs] = v.strip()
            return
        if 'R' in v and re.search(r'R\d+w\d+', v):
            if re.fullmatch(r'[\s()]*R\d+w\d+[\s()]*', CAST.sub('', v)):
                self.val[lhs] = v.strip()
            else:
                self.val[lhs] = v.strip()
                self._use(v)

    def _symbolic(self, rhs):
        return any(n in self.sym for n in IDENT.findall(rhs))

    def _pos(self, rhs):
        e = CAST.sub('', rhs)
        names = {n: Lin({'P': 1}, int(v[1:] or 0)) for n, v in self.sym.items() if v != 'B'}
        if set(IDENT.findall(e)) - set(names) or not names:
            return None
        try:
            v = eval(e, {'__builtins__': {}}, names)
        except Exception:
            return None
        return v.k if isinstance(v, Lin) and v.c == {'P': 1} else None

    def _use(self, v):
        v = chr_ints(v)
        if re.search(r'R\d+w\d+', v):
            self.uses.append(CAST.sub('', v))


def chr_ints(v):
    """Ghidra prints byte constants as char literals ('\\x7f', '\\0', 'a'): as ints."""
    def one(m):
        t = m.group(1)
        if t.startswith('\\x'):
            n = int(t[2:], 16)
        elif t == '\\0':
            n = 0
        elif t.startswith('\\'):
            n = {'n': 10, 't': 9, 'r': 13, "'": 39, '\\': 92}.get(t[1], 0)
        else:
            n = ord(t)
        return str(n - 256 if n > 127 else n) if n != 0x7f else '0x7f'
    return re.sub(r"'(\\x[0-9a-f]{1,2}|\\.|[^'\\])'", one, v)


def byte_role(expr, tok):
    """(shift, xform) of one byte token inside a combining expression."""
    t = re.escape(tok)
    expr = re.sub(r'\*\s*0x100\b', '<< 8', re.sub(r'\*\s*0x10000\b', '<< 0x10', re.sub(r'\*\s*0x1000000\b', '<< 0x18', expr)))
    sh = re.search(r'%s\s*\)*\s*<<\s*(0x[0-9a-f]+|\d+)' % t, expr)
    shift = int(sh.group(1), 0) // 8 if sh else 0
    if re.search(r'%s\s*\)*\s*\^\s*0x80\b|%s\s*\)*\s*[+-]\s*0x80\b' % (t, t), expr) and not re.search(r'-\s*\(*\s*%s' % t, expr):
        x = 'a1'
    elif re.search(r'0x80\s*-\s*\(*\s*%s|-\s*\(*\s*%s\s*\)*\s*-\s*0x80' % (t, t), expr):
        x = 'a3'
    elif re.search(r'-\s*\(*\s*%s' % t, expr):
        x = 'a2'
    else:
        x = ''
        c = re.search(r'%s\s*\)*\s*[!=]=\s*(-?(?:0x[0-9a-f]+|\d+))' % t, expr)
        if c:
            x = {-1: 'a2', -0x80: 'a1', -0x7f: 'a1', 0x7f: 'a3'}.get(int(c.group(1), 0), '')
    return shift, x


def name(width, wire):
    """The Jagex transform of a field from its wire bytes, each (significance, xform), in wire order."""
    sig = [s for s, _ in wire]
    xf = [x for _, x in wire]
    if width == 1:
        return 'p1' + {'': '', 'a1': 'Alt1', 'a2': 'Alt2', 'a3': 'Alt3'}[xf[0]]
    pats = {2: {(1, 0, '', ''): 'p2', (0, 1, '', ''): 'p2Alt1', (1, 0, '', 'a1'): 'p2Alt2', (0, 1, 'a1', ''): 'p2Alt3'},
            3: {(2, 1, 0): 'p3', (0, 1, 2): 'p3Alt1', (2, 0, 1): 'p3Alt2', (1, 2, 0): 'p3Alt3'},
            4: {(3, 2, 1, 0): 'p4', (0, 1, 2, 3): 'p4Alt1', (1, 0, 3, 2): 'p4Alt2', (2, 3, 0, 1): 'p4Alt3'}}
    key = tuple(sig) + (tuple(xf) if width == 2 else ())
    if width != 2 and any(xf):
        return None
    return pats.get(width, {}).get(key)


def fields(seq):
    """Fields of a read sequence, in wire order: (offset, width, transform or None)."""
    done, out = set(), []
    norm = lambda u: re.sub(r'\*\s*0x100\b', '<< 8', re.sub(r'\*\s*0x10000\b', '<< 0x10', re.sub(r'\*\s*0x1000000\b', '<< 0x18', u)))
    comb = []
    for u in map(norm, seq.uses):
        b = sorted({int(k) for k in re.findall(r'R(\d+)w1\b', u)})
        for i in range(len(b)):
            for j in range(min(len(b), i + 4), i + 1, -1):
                run = b[i:j]
                if run == list(range(run[0], run[0] + len(run))) and len(run) > 1 and '<<' in u:
                    roles = [byte_role(u, 'R%dw1' % k) for k in run]
                    if len({r[0] for r in roles}) == len(run) and max(r[0] for r in roles) == len(run) - 1:
                        comb.append((len(run), run, roles))
    for n, run, roles in sorted(comb, key=lambda c: -c[0]):
        if not done & set(run):
            out.append((run[0], n, name(n, roles)))
            done.update(run)
    for u in map(norm, seq.uses):
        toks = sorted(set(re.findall(r'R(\d+)w(\d+)', u)), key=lambda t: int(t[0]))
        toks = [(int(k), int(w)) for k, w in toks if int(k) not in done]
        if not toks:
            continue
        multi = [t for t in toks if t[1] > 1]
        for k, w in multi:
            swapped = bool(re.search(r'0xff00ff00|0xff00ff\b|>> 8\b', u))
            nm = {2: 'p2' if swapped else 'p2Alt1', 4: 'p4' if swapped else 'p4Alt1'}.get(w)
            if w == 3:
                nm = 'p3Alt1'
            out.append((k, w, nm))
            done.add(k)
        bytes_ = [k for k, w in toks if w == 1]
        if not bytes_:
            continue
        roles = {k: byte_role(u, 'R%dw1' % k) for k in bytes_}
        # a combining expression: shifts tie consecutive bytes into one field
        if len(bytes_) > 1 and any(r[0] for r in roles.values()) and re.search(r'<<', u):
            run = sorted(bytes_)
            if run == list(range(run[0], run[0] + len(run))) and len(run) in (2, 3, 4):
                out.append((run[0], len(run), name(len(run), [roles[k] for k in run])))
                done.update(run)
                continue
        for k in bytes_:
            out.append((k, 1, name(1, [roles[k]])))
            done.add(k)
    for k, w in seq.loads.items():
        if k not in done:
            out.append((k, w, {1: 'p1', 2: 'p2Alt1', 4: 'p4Alt1'}.get(w)))
    return sorted(out)


def write_fields(seq):
    """Fields of a write sequence: stores grouped by their source value, wire order."""
    groups = {}
    for k, (w, v) in sorted(seq.stores.items()):
        src = re.sub(r'\s+', '', CAST.sub('', v))
        root = re.sub(r'>>(0x[0-9a-f]+|\d+)|\^0x80|0x80-|-0x80|\+0x80|^-|[()]', '', src)
        groups.setdefault(root, []).append((k, w, src))
    out = []
    for root, g in groups.items():
        if len(g) == 1 and g[0][1] > 1:
            k, w, src = g[0]
            swapped = re.search(r'0xff00ff00|>>8\||<<8\||<<0x18', re.sub(r'[()]', '', src))
            out.append((k, w, {2: 'p2', 4: 'p4'}.get(w) if swapped else {2: 'p2Alt1', 4: 'p4Alt1'}.get(w)))
            continue
        run = [k for k, _, _ in g]
        if len(g) > 1 and run == list(range(run[0], run[0] + len(run))) and len(run) in (2, 3, 4):
            wire = []
            for k, _, src in g:
                sh = re.search(r'>>(0x[0-9a-f]+|\d+)', src)
                x = 'a1' if re.search(r'\^0x80|\+0x80|\|0x80|-0x80$', src) and not src.startswith('-') else \
                    'a3' if re.search(r'0x80-', src) or re.match(r'^-.*-0x80', src) else 'a2' if src.startswith('-') else ''
                wire.append(((int(sh.group(1), 0) // 8) if sh else 0, x))
            out.append((run[0], len(run), name(len(run), wire)))
            continue
        for k, w, src in g:
            x = 'a1' if re.search(r'\^0x80|\+0x80|\|0x80', src) and not src.startswith('-') else \
                'a3' if re.search(r'0x80-', src) or re.match(r'^-.*-0x80', src) else 'a2' if src.startswith('-') else ''
            out.append((k, 1, name(1, [(0, x)])))
    return sorted(out)


def cases(text):
    """{opcode: body} of the dispatcher's switch; shared bodies go to every label."""
    out, cur, body = {}, [], []
    for line in text.split('\n'):
        m = re.match(r'  case (0x[0-9a-f]+|\d+):', line)
        if m:
            if body and cur:
                for c in cur:
                    out[c] = '\n'.join(body)
                cur, body = [], []
            cur.append(int(m.group(1), 0))
            continue
        if re.match(r'  default:|^switchD|^}', line) and cur:
            for c in cur:
                out[c] = '\n'.join(body)
            cur, body = [], []
        if cur:
            body.append(line)
    return out


def prologue_sym(text, base, pos):
    """The B / P aliases assigned between the function's start and its switch."""
    sym = {base: 'B', pos: 'P'}
    head = text.split('switch(')[0]
    for m in re.finditer(r'^\s*(\w+) = (\w+);', head, re.M):
        if m.group(2) in sym and m.group(1) not in sym:
            sym[m.group(1)] = sym[m.group(2)]
    return sym


def read_layout(body, sym):
    s = Seq(sym)
    for st in statements(body):
        s.step(st)
    return fields(s)


def senders(text, ctor):
    """[(opcode, write fields)] for each literal-opcode call to the packet constructor in one function.
    The buffer's B / P are the pair most stores use after the call (`*(T *)(B + P [+ k]) = v`)."""
    out = []
    parts = re.split(r'\b%s\(' % ctor, text)
    for part in parts[1:]:
        m = re.match(r'[^,]*,\s*(0x[0-9a-f]+|\d+)\s*,', part)
        if not m:
            continue
        pairs = {}
        for b, p in re.findall(r'\*\(\w+ \*\)\((\w+) \+ (\w+)\) =', part):
            pairs[(b, p)] = pairs.get((b, p), 0) + 1
        if not pairs:
            out.append((int(m.group(1), 0), []))
            continue
        b, p = max(pairs, key=pairs.get)
        s = Seq({b: 'B', p: 'P'}, write=True)
        for st in statements(re.split(r'\b%s\(' % ctor, text)[0] if False else ''.join(parts[:parts.index(part)])):
            s.step(st)
        s.stores.clear()
        s.sym = {b: 'B', p: 'P'}
        for st in statements(part.split(';', 1)[1] if ';' in part else part):
            s.step(st)
        out.append((int(m.group(1), 0), write_fields(s)))
    return out
