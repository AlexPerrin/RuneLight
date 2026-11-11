"""match: each Android opcode to a prot, cheapest evidence first, and generate the per-revision record.

  size       the only Android opcode of its size against the only desktop prot of that size
  widths     the client's field widths (in order) equal exactly one remaining desktop prot's
  carry-over the previous revision's opcode with the most similar code (mutual best, same size)
  by hand    a row of hand.tsv (a person's decision, with its reason)
  dead       no dispatcher case / no sender, and no hand row
  open       none of the above: listed with its candidates for a person
"""
import difflib
import json
import os
import re
from collections import defaultdict

SIDES = ('s2c', 'c2s')


def complete(fields, size):
    """The client's layout covers every byte once, in order, with known transforms."""
    if size < 0 or any(n is None for _, _, n in fields):
        return False
    fields = sorted(fields)
    return sum(w for _, w, _ in fields) == size and all(a[0] + a[1] == b[0] for a, b in zip(fields, fields[1:])) and \
        (not fields or fields[0][0] == 0)


def client_fields(e):
    if 'senders' in e:
        ok = [s['fields'] for s in e['senders'] if complete(s['fields'], e['size'])]
        return ok[0] if ok else (e['senders'][0]['fields'] if e['senders'] else [])
    return e['fields']


def fps(e):
    if 'senders' in e:
        return ['%s %s' % ('i%d' % s['idx'] if s.get('guard') is None else 'g%d' % s['guard'], s['fp'])
                for s in e['senders']]
    return [e['fp']] if e.get('fp') else []


def similarity(a, b):
    best = 0
    for x in a:
        for y in b:
            ta, tb = x.split(), y.split()
            if ta[0] != tb[0] and ta[0][0] == tb[0][0] == 'g':
                continue  # senders selected by different menu ops (cparse.guard) are different packets
            sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
            if sm.real_quick_ratio() > best and sm.quick_ratio() > best:
                best = max(best, sm.ratio())
    return best


def successor(prot, size, pool):
    """prot's name in pool ({name: size}) at this size: itself, else the one prot of its base name
    (RSProt versions a prot whose layout changes: OPLOC1 -> OPLOC1_V2, and adds OPLOC1_V3 beside it)."""
    if pool.get(prot) == size:
        return prot
    base = re.sub(r'_V\d+$', '', prot)
    c = [p for p, sz in pool.items() if sz == size and re.sub(r'_V\d+$', '', p) == base]
    return c[0] if len(c) == 1 else None


def read_tsv(path):
    if not os.path.exists(path):
        return []
    return [line.rstrip('\n').split('\t') for line in open(path) if line.strip() and not line.startswith('#')]


def run(rec, work, rs, prev):
    x = json.load(open(os.path.join(work, 'extracted.json')))
    hand = {(r[0], int(r[1])): (r[2], r[3] if len(r) > 3 else '') for r in read_tsv(os.path.join(rec, 'hand.tsv'))}
    pmap, px = {}, None
    if prev:
        pmap = {(r[0], int(r[1])): r for r in read_tsv(os.path.join(rec, '..', prev, 'mapping.tsv'))}
        px = json.load(open(os.path.join(work, '..', prev, 'extracted.json')))
    rows = {}
    for side in SIDES:
        desk = rs.s2c if side == 's2c' else rs.c2s
        lay = rs.enc if side == 's2c' else rs.dec
        ops = {int(k): v for k, v in x[side].items()}
        free = dict(desk)
        got = {}

        def take(op, prot, how, note=''):
            got[op] = (prot, how, note)
            free.pop(prot, None)

        for op in ops:  # a hand row wins: applied first, its prot leaves the pool
            if (side, op) in hand:
                prot, note = hand[(side, op)]
                prot = successor(prot, ops[op]['size'], desk) or prot
                take(op, prot if prot != 'dead' else 'OP_%d' % op, 'by hand' if prot != 'dead' else 'dead', note)
        changed = True
        while changed:
            changed = False
            by_size, dsz = defaultdict(list), defaultdict(list)
            for op, e in ops.items():
                if op not in got and not e.get('nocase') and not ('senders' in e and not e['senders'] and not e['sites']):
                    by_size[e['size']].append(op)  # an opcode the client never handles / sends stays dead
            for prot, sz in free.items():
                dsz[sz].append(prot)
            for sz, a in by_size.items():
                if len(a) == 1 and len(dsz.get(sz, [])) == 1:
                    take(a[0], dsz[sz][0], 'size')
                    changed = True
            if changed:
                continue
            for op in sorted(ops):
                e = ops[op]
                if op in got or op not in by_size[e['size']] or not complete(client_fields(e), e['size']):
                    continue
                w = sorted(f[1] for f in client_fields(e))
                c = [p for p in dsz.get(e['size'], []) if lay.get(p) and None not in lay[p][0] and
                     sorted(int(t[1]) for t in lay[p][0]) == w]
                rivals = [o for o in by_size[e['size']] if o != op and complete(client_fields(ops[o]), ops[o]['size'])
                          and sorted(f[1] for f in client_fields(ops[o])) == w]
                if len(c) == 1 and not rivals:
                    take(op, c[0], 'widths')
                    changed = True
            if px and not changed and side == 'c2s':
                # carry-over by menu op: a function that picks its sender by menu op (cparse.guard) is the
                # previous revision's function whose prots, at the same menu ops, all have a free successor
                pfun, cur = defaultdict(dict), defaultdict(list)
                for pop, pe in px[side].items():
                    r = pmap.get((side, int(pop)))
                    for snd in pe['senders']:
                        if r and snd.get('guard') is not None:
                            pfun[snd['func']][snd['guard']] = (r[2], int(pop))
                for op, e in ops.items():
                    for snd in e['senders'] if op not in got else []:
                        if snd.get('guard') is not None:
                            cur[snd['func']].append((snd['guard'], op))
                for f, sel in sorted(cur.items()):
                    if len({g for g, _ in sel}) < 2:
                        continue
                    fits = []
                    for pf, gp in pfun.items():
                        names = [successor(gp[g][0], ops[op]['size'], free) if g in gp else None for g, op in sel]
                        if all(names) and len(set(names)) == len(names):
                            fits.append((pf, names))
                    if len(fits) == 1:
                        pf, names = fits[0]
                        for (g, op), prot in zip(sel, names):
                            take(op, prot, 'carry-over', 'from %s %s %d (menu op %d)' % (prev, side, pfun[pf][g][1], g))
                        changed = True
                        break
            if px and not changed:  # a prot both revisions handle outside the dispatcher (PACKET_GROUP_START)
                for pop, pe in sorted(px[side].items()):
                    r = pmap.get((side, int(pop)))
                    if not pe.get('nocase') or not r or r[4] == 'dead':
                        continue
                    c = [op for op, e in ops.items() if op not in got and e.get('nocase') and
                         successor(r[2], e['size'], free)]
                    if len(c) == 1:
                        take(c[0], successor(r[2], ops[c[0]]['size'], free), 'carry-over',
                             'from %s %s %s (no dispatcher case in either)' % (prev, side, pop))
                        changed = True
            if px and not changed:
                pops = {int(k): v for k, v in px[side].items()}
                cand = []
                for op in ops:
                    if op in got or not fps(ops[op]):
                        continue
                    for pop, pe in pops.items():
                        r = pmap.get((side, pop))
                        prot = r and successor(r[2], ops[op]['size'], free)
                        if not prot or not fps(pe):
                            continue
                        cand.append((similarity(fps(ops[op]), fps(pe)), op, pop, prot))
                cand.sort(reverse=True)
                used_op, used_prot = set(got), set()
                for sc, op, pop, prot in cand:
                    if sc < 0.5 or op in used_op or prot in used_prot:
                        continue
                    used_op.add(op)
                    used_prot.add(prot)
                    take(op, prot, 'carry-over', 'from %s %s %d (%.2f)' % (prev, side, pop, sc))
                    changed = True
        for op, e in ops.items():
            if op in got:
                continue
            if e.get('nocase') or ('senders' in e and not e['senders'] and not e['sites']):
                got[op] = ('OP_%d' % op, 'dead', 'no dispatcher case' if side == 's2c' else 'no sender')
            else:
                c = sorted(p for p, sz in free.items() if sz == e['size'])
                got[op] = ('OP_%d' % op, 'open', 'candidates: ' + (' '.join(c) or 'none'))
        for op, (prot, how, note) in got.items():
            f = sorted(client_fields(ops[op]))
            rows[(side, op)] = [side, str(op), prot, str(ops[op]['size']), how,
                                ' '.join('%d:%s' % (k, n or 'w%d?' % w) for k, w, n in f), note]
    with open(os.path.join(rec, 'mapping.tsv'), 'w') as out:
        out.write('# side\topcode\tprot\tsize\tidentified\tclient layout (offset:transform)\tnote\n')
        for k in sorted(rows):
            out.write('\t'.join(rows[k]) + '\n')
    return rows


def generate(rec, rev):
    """Kotlin enum rows of both prot enums and the docs table, from mapping.tsv."""
    rows = read_tsv(os.path.join(rec, 'mapping.tsv'))
    var = {'-1': 'Prot.VAR_BYTE', '-2': 'Prot.VAR_SHORT'}
    with open(os.path.join(rec, 'enums.kt.txt'), 'w') as f:
        for side, enum in (('s2c', 'AndroidGameServerProt'), ('c2s', 'AndroidGameClientProt')):
            f.write('// %s rows (rev %s), generated by tools/protmap from mapping.tsv\n' % (enum, rev))
            for r in sorted((r for r in rows if r[0] == side), key=lambda r: int(r[1])):
                tail = '' if r[4] not in ('dead', 'open') else ' // %s: %s' % (r[4], r[6])
                f.write('    %s(%s, %s),%s\n' % (r[2], r[1], var.get(r[3], r[3]), tail))
    with open(os.path.join(rec, 'mapping.md'), 'w') as f:
        f.write('# Android protocol %s (generated by tools/protmap from mapping.tsv)\n\n' % rev)
        for side, title in (('s2c', 'Server->client'), ('c2s', 'Client->server')):
            f.write('## %s\n\n| Opcode | Prot | Size | Identified | Client layout |\n|---|---|---|---|---|\n' % title)
            for r in sorted((r for r in rows if r[0] == side), key=lambda r: int(r[1])):
                f.write('| %s | %s | %s | %s | %s |\n' % (r[1], r[2], r[3], r[4], r[5] or '-'))
            f.write('\n')
