"""extract: a revision's size tables, packet constructor, decompiled dispatcher and senders, and the
client's own read / write sequence of every opcode, into protmap/<rev>/extracted.json.

Addresses: ELF in the .so and objdump; Ghidra = ELF + 0x100000.
"""
import json
import os
import re
import struct
import subprocess
from collections import Counter

import cparse

GHIDRA = os.environ.get('GHIDRA_HEADLESS', os.path.expanduser('~/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless'))
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ghidra_scripts')
GBASE = 0x100000


def size_table(so, desktop, n):
    """(file offset, distance) of the n-entry int32 table nearest desktop's sizes as a multiset.
    n is the client's own bound (dispatcher cases / constructor opcodes). Ties go to a start some
    pointer in the file names (the GOT entry's relocation addend)."""
    data = open(so, 'rb').read()
    vals = struct.unpack_from('<%di' % (len(data) // 4), data)
    want, cands = Counter(desktop), []
    i = 0
    while i < len(vals):
        if not -2 <= vals[i] <= 255:
            i += 1
            continue
        j = i
        while j < len(vals) and -2 <= vals[j] <= 255:
            j += 1
        if j - i >= n:
            c = Counter(vals[i:i + n])
            for a in range(i, j - n + 1):
                if a > i:
                    c[vals[a - 1]] -= 1
                    c[vals[a + n - 1]] += 1
                d = sum((c - want).values()) + sum((want - c).values())
                cands.append((d, 4 * a))
        i = j
    d = min(c[0] for c in cands)
    tied = [a for e, a in cands if e == d]
    named = [a for a in tied if struct.pack('<Q', a) in data]
    return (named or tied)[0], d


def table(so, off, n):
    return list(struct.unpack_from('<%di' % n, open(so, 'rb').read(), off))


def dis(so):
    """objdump of the .so (cached beside it)."""
    out = so + '.protmap.dis'
    if not os.path.exists(out):
        with open(out, 'w') as f:
            subprocess.run(['aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn', so], stdout=f, check=True)
    return out


def call_sites(dis_path):
    """{target: [(site, w0 immediate or None)]} of every bl, the opcode found by walking back to mov w0."""
    lines = open(dis_path).read().split('\n')
    out = {}
    bl = re.compile(r'^\s*([0-9a-f]+):\s+bl\s+(?:0x)?([0-9a-f]+)\b')
    for i, line in enumerate(lines):
        m = bl.match(line)
        if not m:
            continue
        op = None
        for j in range(i - 1, max(0, i - 40), -1):
            w = re.search(r'\smov\s+w0,\s*(?:#(0x[0-9a-f]+|\d+)|wzr)', lines[j])
            if w:
                op = int(w.group(1), 0) if w.group(1) else 0
                break
            if re.search(r'\s(ret|bl|b)\s', lines[j]) or re.search(r'\s(mov|ldr|add|orr)\s+[wx]0,', lines[j]):
                break
        out.setdefault(int(m.group(2), 16), []).append((int(m.group(1), 16), op))
    return out


def constructor(sites, n):
    """The callee whose literal first arguments cover the most distinct opcodes below n."""
    def cover(t):
        return len({op for _, op in sites[t] if op is not None and op < n})
    return max(sites, key=cover)


def decompile(rev_dir, project, program, so, addrs):
    """Decompile.java over Ghidra addresses into <rev_dir>/re/decomp (read-only project)."""
    os.makedirs(rev_dir, exist_ok=True)
    cmd = [GHIDRA, os.path.dirname(project), os.path.basename(project), '-process', program, '-noanalysis',
           '-readOnly', '-scriptPath', SCRIPTS, '-postScript', 'Decompile.java'] + ['0x%x' % a for a in addrs]
    with open(os.path.join(rev_dir, 'decompile.log'), 'w') as log:
        subprocess.run(cmd, cwd=rev_dir, stdout=log, stderr=subprocess.STDOUT, check=True)


def buffer_vars(text):
    """B / P of the dispatcher: the base and position most loads use (`*(T *)(B + P)`)."""
    pairs = Counter(re.findall(r'\*\(\w+ \*\)\((\w+) \+ (\w+)\)', text))
    (b, p), _ = pairs.most_common(1)[0]
    return b, p


def fingerprint(text):
    """Code with addresses, large numbers and local names erased: the carry-over key. Numbers below 0x100
    stay (a model type, a flag's byte offset: what tells twin handlers apart); struct offsets and addresses
    shift between revisions. Comments, declarations and register shuffles (`v = v;`, CONCAT44) are dropped."""
    t = re.sub(r'/\*.*?\*/', '', text, flags=re.S)  # Ghidra's warnings
    t = re.sub(r'(FUN|DAT|PTR|LAB|switchD|caseD|code)_[0-9a-f_A-Za-z]+', 'X', t)
    t = re.sub(r'0x[0-9a-f]+|\b\d+\b', lambda m: m.group(0) if int(m.group(0), 16 if m.group(0).startswith('0x') else 10) < 0x100 else 'N', t)
    t = re.sub(r'\b[a-zA-Z]+Var\d+|\b[a-zA-Z]+Stack_[0-9a-f]+|\blocal_[0-9a-f]+|\bparam_\d+|\bp+[a-zA-Z]+\d+', 'v', t)
    t = re.sub(r'\b(?!goto\b|return\b)[a-zA-Z_]\w*\s+\**\s*\w+(?:\s*\[\w+\])?;', '', t)  # declarations
    t = ' '.join(t.split())
    return re.sub(r'v = (v|[^;]*CONCAT44\([^;]*\));\s?', '', t)


def extract(rev_dir, so, project, program, dispatcher, desktop_s2c, desktop_c2s, decomp=None):
    sites = call_sites(dis(so))
    ctor = constructor(sites, len(desktop_c2s) + 32)
    c_n = max(op for _, op in sites[ctor] if op is not None and op < len(desktop_c2s) + 32) + 1
    decomp = decomp or os.path.join(rev_dir, 're', 'decomp')
    disp_file = os.path.join(decomp, '%08x.c' % dispatcher)
    want = [dispatcher] + [s + GBASE for s, _ in sites[ctor]]
    if not os.path.exists(disp_file):
        decompile(rev_dir, project, program, so, want)
    text = open(disp_file).read()
    s_n = max(cparse.cases(text)) + 1
    s_off, s_d = size_table(so, desktop_s2c, s_n)
    c_off, c_d = size_table(so, desktop_c2s, c_n)
    s_sizes, c_sizes = table(so, s_off, s_n), table(so, c_off, c_n)
    b, p = buffer_vars(text)
    sym = cparse.prologue_sym(text, b, p)
    s2c, own = {}, {op: body.split('joined_r')[0] for op, body in cparse.cases(text).items() if op < s_n}
    callees = {op: re.findall(r'\bFUN_([0-9a-f]+)\(', body) for op, body in own.items()}
    cdec = os.path.join(rev_dir, 'callees', 're', 'decomp')
    need = sorted({a for c in callees.values() for a in c if not os.path.exists(os.path.join(cdec, a + '.c'))})
    if need:
        decompile(os.path.join(rev_dir, 'callees'), project, program, so, [int(a, 16) for a in need])

    def callee_fp(a):  # a stub case (`FUN_x(buf, len)`) differs from its twin only in what it calls
        f = os.path.join(cdec, a + '.c')
        return ' '.join(fingerprint(open(f).read()).split()[:300]) if os.path.exists(f) else ''
    for op, body in own.items():  # the tail after a joined_r label is code shared with other cases
        s2c[op] = {'size': s_sizes[op], 'fields': cparse.read_layout(body, sym),
                   'fp': ' | '.join([fingerprint(body)] + [callee_fp(a) for a in callees[op]])}
    for op in range(s_n):
        s2c.setdefault(op, {'size': s_sizes[op], 'fields': [], 'fp': '', 'nocase': True})
    ctor_name = 'FUN_%08x' % (ctor + GBASE)
    c2s = {op: {'size': c_sizes[op], 'senders': []} for op in range(c_n)}
    for f in sorted(os.listdir(decomp)):
        t = open(os.path.join(decomp, f)).read()
        if ctor_name + '(' not in t or f == '%08x.c' % (ctor + GBASE):
            continue
        fp = fingerprint(t)
        for i, (op, fl, g) in enumerate(cparse.senders(t, ctor_name)):
            if op < c_n:
                c2s[op]['senders'].append({'func': f[:-2], 'idx': i, 'fields': fl, 'fp': fp, 'guard': g})
    site_ops = Counter(op for _, op in sites[ctor])
    for op in range(c_n):
        c2s[op]['sites'] = site_ops.get(op, 0)
    out = {'so': os.path.basename(so), 'size': os.path.getsize(so),
           's2c_table': [s_off, s_n, s_d], 'c2s_table': [c_off, c_n, c_d],
           'constructor': ctor + GBASE, 'dispatcher': dispatcher, 'buffer': [b, p],
           'nonliteral_sites': sum(1 for _, op in sites[ctor] if op is None), 's2c': s2c, 'c2s': c2s}
    json.dump(out, open(os.path.join(rev_dir, 'extracted.json'), 'w'), indent=1, sort_keys=True)
    return out


def fmt(fields):
    return ' '.join('%d:%s' % (k, n or 'w%d?' % w) for k, w, n in fields)


def tsv(x):
    """The extracted tables, one row per opcode: side, opcode, size, the client's fields (offset:transform)."""
    rows = ['# side\topcode\tsize\tclient fields (offset:transform; w<n>? = width n, transform not read)']
    for op in sorted(x['s2c'], key=int):
        e = x['s2c'][op]
        rows.append('s2c\t%s\t%d\t%s' % (op, e['size'], 'no case' if e.get('nocase') else fmt(e['fields'])))
    for op in sorted(x['c2s'], key=int):
        e = x['c2s'][op]
        rows.append('c2s\t%s\t%d\t%s' % (op, e['size'], ' | '.join(fmt(s['fields']) for s in e['senders']) or
                                         ('no sender' if not e['sites'] else 'sender not decompiled')))
    return '\n'.join(rows) + '\n'
