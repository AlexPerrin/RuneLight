#!/usr/bin/env python3
"""Map ELF addresses of one osclient.so build to another (241.4 -> 242 and on).

    port_offsets.py OLD.so NEW.so ADDR...           prints "old new how" per address
    port_offsets.py OLD.so NEW.so --fields FUNC...  per old function, the struct offsets its matched instructions use
                                                    in the new build ("old>new*count"; offsets that differ first)
    port_offsets.py OLD.so NEW.so --struct OFF,...  one struct's offsets: the old functions using most of them
                                                    (rarer offsets weigh more) vote each offset's new value

Code addresses are matched by windows of instructions normalised for relocation (ADRP, BL/B, literal loads and the
page offsets of ADRP-based ADD/LDR/STR are masked); a window must hit one place in the new .text. Data addresses
(globals, vtables, GOT slots) are mapped through the code that references them: each ADRP+ADD/LDR/STR in the old
.text resolving to (or near) the address is matched, and the same pair in the new build is decoded. Each result is a
vote; a mapping with disagreeing votes prints them all.
Struct offsets are read per function: the old function and its match are aligned instruction by instruction with every
immediate masked, and each matched pair's offset (ADD/LDR/STR immediates, MOVZ/MOVK constants added to a base, summed
along ADD chains) is compared.
"""
import bisect
import struct
import sys
from collections import Counter


class Elf:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        b = self.b
        shoff, = struct.unpack_from('<Q', b, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x3a)
        secs = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * shentsize) for i in range(shnum)]
        stroff = secs[shstrndx][4]
        self.sec = {}
        for s in secs:
            name = b[stroff + s[0]:b.index(b'\0', stroff + s[0])].decode()
            self.sec[name] = (s[3], s[4], s[5])  # vaddr, file offset, size
        va, off, size = self.sec['.text']
        self.text_va = va
        self.words = struct.unpack_from('<%dI' % (size // 4), b, off)
        self.norm = normalise(self.words)
        va, off, size = self.sec['.eh_frame_hdr']  # its binary-search table: every function's start
        n, = struct.unpack_from('<I', b, off + 8)
        self.funcs = sorted(struct.unpack_from('<i', b, off + 12 + 8 * i)[0] + va for i in range(n))
        self.funcset = set(self.funcs)
        va, off, size = self.sec['.rela.dyn']  # R_AARCH64_RELATIVE: the pointers in vtables and tables
        self.rel = {}
        for k in range(off, off + size, 24):
            r_off, r_info, r_add = struct.unpack_from('<QQq', b, k)
            if r_info & 0xffffffff == 0x403: self.rel[r_off] = r_add

    def in_text(self, a):
        return self.text_va <= a < self.text_va + 4 * len(self.words)


def is_adrp(w): return (w & 0x9f000000) == 0x90000000
def is_add_imm(w): return (w & 0xff800000) == 0x91000000  # ADD Xd, Xn, #imm (no shift)
def is_ldst_uimm(w): return (w & 0x3b000000) == 0x39000000
def is_branch_imm(w): return (w & 0x7c000000) == 0x14000000  # B, BL
def is_ldr_lit(w): return (w & 0x3b000000) == 0x18000000
def is_adr(w): return (w & 0x9f000000) == 0x10000000


def adrp_page(w, pc):
    imm = ((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20): imm -= 1 << 21
    return (pc & ~0xfff) + (imm << 12)


def lo12(w):
    """The byte offset an ADD imm or a LDR/STR unsigned-offset instruction adds to its base."""
    if is_add_imm(w): return (w >> 10) & 0xfff
    size = w >> 30
    if (w & 0x04800000) == 0x04800000 and size == 0: size = 4  # 128-bit SIMD&FP
    return ((w >> 10) & 0xfff) << size


def normalise(words):
    out, page = [], {}  # page: register -> instructions since its ADRP
    for w in words:
        rd, rn = w & 31, (w >> 5) & 31
        if is_adrp(w):
            out.append(w & 0x9f00001f); page[rd] = 0
        elif is_branch_imm(w):
            out.append(w & 0xfc000000)
        elif is_ldr_lit(w) or is_adr(w):
            out.append(w & 0xff00001f)
        elif (is_add_imm(w) or is_ldst_uimm(w)) and rn in page:
            out.append(w & 0xffc003ff)
            if rd != rn and not is_ldst_uimm(w): page[rd] = 0
        else:
            out.append(w)
            page.pop(rd, None)
        for r in list(page):
            page[r] += 1
            if page[r] > 24: del page[r]
    return out


class Index:
    def __init__(self, new, n):
        self.n, self.map = n, {}
        nw = new.norm
        for i in range(len(nw) - n):
            k = hash(tuple(nw[i:i + n]))
            self.map[k] = -1 if k in self.map else i


def map_code(old, new, idx, a, span=48):
    """Votes for a's address in new: windows that hit one place, those holding a first, then those near it."""
    i0 = (a - old.text_va) // 4
    for near in (False, True):
        votes = Counter()
        for ix in idx:
            n = ix.n
            for s in range(i0 - span, i0 + span):
                if s < 0 or s + n > len(old.norm) or (s <= i0 < s + n) == near: continue
                j = ix.map.get(hash(tuple(old.norm[s:s + n])))
                if j is not None and j >= 0:
                    votes[new.text_va + 4 * (j + i0 - s)] += 1
            if votes: return votes
    return votes


def bl_target(w, pc):
    off = w & 0x3ffffff
    if off & (1 << 25): off -= 1 << 26
    return pc + 4 * off


def map_by_callers(old, new, idx, f, most=40):
    """Votes for function f's new address: the targets of the matched BL instructions that call it."""
    votes = Counter()
    sites = [i for i, w in enumerate(old.words) if (w & 0xfc000000) == 0x94000000 and bl_target(w, old.text_va + 4 * i) == f]
    for i in sites[:most]:
        v = map_code(old, new, idx, old.text_va + 4 * i, span=16)
        if len(v) != 1: continue
        j = (next(iter(v)) - new.text_va) // 4
        w = new.words[j]
        if (w & 0xfc000000) == 0x94000000: votes[bl_target(w, new.text_va + 4 * j)] += 1
    return votes


def map_table(old, new, idx, g, slots=6):
    """A vtable or pointer table: the new address whose pointers are the old ones' matches, slot by slot."""
    want = []
    for k in range(slots):
        t = old.rel.get(g + 8 * k)
        if t is None or not old.in_text(t): continue
        v = map_code(old, new, idx, t)
        if len(v) == 1 or v and v.most_common(1)[0][1] > 2 * sum(v.values()) // 3: want.append((k, v.most_common(1)[0][0]))
    if len(want) < 2: return Counter()
    k0, t0 = want[0]
    hits = Counter()
    for a, t in new.rel.items():
        if t == t0 and all(new.rel.get(a + 8 * (k - k0)) == tk for k, tk in want[1:]): hits[a - 8 * k0] += len(want)
    return hits


def data_refs(elf, g, near=0x40):
    """(index of ADRP, index of the use, target) for each ADRP-based reference into [g - near, g + near]."""
    out, ws = [], elf.words
    lo, hi = g - near, g + near
    for i, w in enumerate(ws):
        if not is_adrp(w): continue
        page = adrp_page(w, elf.text_va + 4 * i)
        if page > hi or page + 0x1000 + 0x8000 < lo: continue
        rd = w & 31
        for j in range(i + 1, min(i + 24, len(ws))):
            u = ws[j]
            if (is_add_imm(u) or is_ldst_uimm(u)) and (u >> 5) & 31 == rd:
                t = page + lo12(u)
                if lo <= t <= hi: out.append((i, j, t))
                if is_add_imm(u) or (u & 31) == rd: break
            elif (u & 31) == rd and not is_ldst_uimm(u):
                break
    return out


def map_data(old, new, idx, g, near=0x40):
    """Votes for data address g in new, from exact references when there are any, else from ones within near."""
    got_lo, got_size = old.sec['.got'][0], old.sec['.got'][2]
    for d in sorted({0, near}):
        votes = Counter()
        for i, j, t in data_refs(old, g, d):
            cand = map_code(old, new, idx, old.text_va + 4 * i, span=16)
            if len(cand) != 1: continue
            ni = (next(iter(cand)) - new.text_va) // 4
            nj = ni + (j - i)
            if nj >= len(new.words): continue
            w, u = new.words[ni], new.words[nj]
            if not is_adrp(w) or not (is_add_imm(u) or is_ldst_uimm(u)): continue
            votes[adrp_page(w, new.text_va + 4 * ni) + lo12(u) - (t - g)] += 1
        # through the GOT: a slot holding the pointer is loaded by ADRP+LDR; its match holds the new address
        for slot, t in old.rel.items():
            if abs(t - g) > d or not got_lo <= slot < got_lo + got_size: continue
            sv, _ = map_data(old, new, idx, slot, near=0)
            for ns, n in sv.items():
                if ns in new.rel: votes[new.rel[ns] - (t - g)] += n
        if votes: return votes, d == 0
    return votes, False


def func_range(elf, a):
    i = bisect.bisect_right(elf.funcs, a) - 1
    end = elf.funcs[i + 1] if i + 1 < len(elf.funcs) else elf.text_va + 4 * len(elf.words)
    n = len(elf.words)
    return min(n, (elf.funcs[i] - elf.text_va) // 4), min(n, (end - elf.text_va) // 4)


def is_movz(w): return (w & 0x7f800000) == 0x52800000
def is_movk(w): return (w & 0x7f800000) == 0x72800000


def loose(w):
    if is_add_imm(w) or (w & 0xff800000) in (0xd1000000, 0x91400000 & 0xff800000) or is_ldst_uimm(w): return w & 0xffc003ff
    if is_movz(w) or is_movk(w): return w & 0xffe0001f
    if (w & 0x3a000000) == 0x28000000: return w & 0xffc07fff  # ldp/stp imm7
    if is_branch_imm(w): return w & 0xfc000000
    if (w & 0xff000010) == 0x54000000 or (w & 0x7e000000) == 0x34000000: return w & 0xff00001f  # b.cond, cbz
    if (w & 0x7e000000) == 0x36000000: return w & 0xfff8001f  # tbz
    if is_adrp(w) or is_adr(w): return w & 0x9f00001f
    if is_ldr_lit(w): return w & 0xff00001f
    return w


def offsets(elf, lo, hi):
    """index -> the offset its immediate adds to a base, for the struct-access instructions of [lo, hi): the
    instruction's own immediate, plus a page step (ADD #n, LSL #12) or a MOVZ/MOVK constant that made its base."""
    out, reg, const = {}, {}, {}
    for i in range(lo, hi):
        w = elf.words[i]
        rd, rn, rm = w & 31, (w >> 5) & 31, (w >> 16) & 31
        if is_adrp(w):
            reg.pop(rd, None); const.pop(rd, None); continue
        if is_movz(w):
            const[rd] = ((w >> 5) & 0xffff) << (16 * ((w >> 21) & 3)); reg.pop(rd, None); continue
        if is_movk(w):
            hw = 16 * ((w >> 21) & 3)
            const[rd] = (const.get(rd, 0) & ~(0xffff << hw)) | (((w >> 5) & 0xffff) << hw); continue
        if is_add_imm(w) or (w & 0xffc00000) == 0x91400000:
            off = reg.get(rn, 0) + (((w >> 10) & 0xfff) << (12 if (w >> 22) & 1 else 0))
            out[i] = off; const.pop(rd, None)
            if (w >> 22) & 1: reg[rd] = off  # only a page-sized step is a base (x + 0x413000); small adds start new ones
            else: reg.pop(rd, None)
            continue
        if (w & 0xffe0fc00) == 0x8b000000 and rm in const:  # add xd, xn, xm (xm a constant)
            off = reg.get(rn, 0) + const[rm]
            out[i] = off; reg[rd] = off; const.pop(rd, None); continue
        if is_ldst_uimm(w):
            out[i] = reg.get(rn, 0) + lo12(w)
            if not (w >> 22) & 1 or w >> 30 == 3 and (w >> 23) & 1: continue  # a store writes no register
        elif (w & 0x3b200c00) == 0x38200800 and rm in const:  # ldr/str [xn, xm] (xm a constant)
            out[i] = reg.get(rn, 0) + const[rm]
            if not (w >> 22) & 1: continue
        elif (w & 0x3a000000) == 0x28000000:  # ldp/stp
            if (w >> 22) & 1: reg.pop(rd, None); reg.pop((w >> 10) & 31, None); const.pop(rd, None)
            continue
        elif (w & 0xff000010) == 0x54000000 or (w & 0x7c000000) == 0x34000000 or rd == 31:
            continue  # branches, compares
        elif (w & 0xffe0ffe0) == 0xaa0003e0:  # mov xd, xm
            if rm in reg: reg[rd] = reg[rm]
            else: reg.pop(rd, None)
            if rm in const: const[rd] = const[rm]
            else: const.pop(rd, None)
            continue
        reg.pop(rd, None); const.pop(rd, None)
        if (w & 0xfc000000) == 0x94000000 or (w & 0xfffffc1f) == 0xd63f0000:  # a call clobbers x0-x18
            for r in [r for r in reg if r < 19]: del reg[r]
            for r in [r for r in const if r < 19]: del const[r]
    return out


class LooseIndex:
    """Every 16-instruction window of the new .text with every immediate masked, by hash (-1: not unique)."""
    def __init__(self, new, n=16):
        self.n, self.map = n, {}
        self.words = [loose(w) for w in new.words]
        for i in range(len(self.words) - n):
            k = hash(tuple(self.words[i:i + n]))
            self.map[k] = -1 if k in self.map else i


def field_map(old, new, lidx, f):
    """Counter of (old offset, new offset) over old function f's instructions that lie in a 16-instruction window
    matching one place of the new .text with immediates masked."""
    lo, hi = func_range(old, f)
    ow = [loose(w) for w in old.words[max(0, lo - lidx.n):hi + lidx.n]]
    base = max(0, lo - lidx.n)
    al = {}
    for s in range(len(ow) - lidx.n + 1):
        j = lidx.map.get(hash(tuple(ow[s:s + lidx.n])))
        if j is None or j < 0: continue
        for d in range(lidx.n): al.setdefault(base + s + d, j + d)
    eo = offsets(old, lo, hi)
    pairs, en_cache, nf = Counter(), {}, None
    for i in range(lo, hi):
        j = al.get(i)
        if j is None or i not in eo or old.norm[i] != old.words[i]: continue
        nlo, nhi = func_range(new, new.text_va + 4 * j)
        if nlo not in en_cache: en_cache[nlo] = offsets(new, nlo, nhi)
        if i == lo or nf is None: nf = new.text_va + 4 * nlo
        b = en_cache[nlo].get(j)
        if b is not None: pairs[(eo[i], b)] += 1
    return nf, pairs


def struct_map(old, new, idx, offs, top=12):
    """{old offset: Counter(new offset)} from the old functions whose offsets overlap offs the most."""
    import math
    fsets, df = [], Counter()
    for k, f in enumerate(old.funcs):
        lo, hi = func_range(old, f)
        s = set(offsets(old, lo, hi).values())
        fsets.append(s); df.update(s)
    n = len(fsets)
    want = set(offs)
    score = sorted(((sum(math.log(n / df[o]) for o in want & s), k) for k, s in enumerate(fsets) if len(want & s) >= 2), reverse=True)
    votes = {o: Counter() for o in offs}
    used = []
    for sc, k in score[:top]:
        nf, pairs = field_map(old, new, idx, old.funcs[k])
        if nf is None: continue
        used.append('%#x(%d)' % (old.funcs[k], len(want & fsets[k])))
        for (a, b), c in pairs.items():
            if a in votes: votes[a][b] += c
    return votes, used


def main():
    old, new = Elf(sys.argv[1]), Elf(sys.argv[2])
    if sys.argv[3] in ('--struct', '--fields'): idx = LooseIndex(new)
    else: idx = [Index(new, n) for n in (16, 32, 64)]
    if sys.argv[3] == '--struct':
        for arg in sys.argv[4:]:
            offs = [int(x, 16) for x in arg.split(',')]
            votes, used = struct_map(old, new, idx, offs)
            print('functions: ' + ' '.join(used))
            for o in offs:
                v = votes[o]
                print('  %#x -> %s' % (o, ' '.join('%#x:%d' % kv for kv in v.most_common(3)) or '?'))
        return
    if sys.argv[3] == '--fields':
        for arg in sys.argv[4:]:
            nf, pairs = field_map(old, new, idx, int(arg, 16))
            if nf is None:
                print('%s ? no match' % arg); continue
            items = sorted(pairs.items(), key=lambda kv: (kv[0][0] == kv[0][1], kv[0][0]))
            print('%s -> %#x: %s' % (arg, nf, ' '.join('%x>%x*%d' % (a, b, c) if a != b else '%x' % a for (a, b), c in items)))
        return
    for arg in sys.argv[3:]:
        a = int(arg, 16)
        if old.in_text(a):
            v, how = map_code(old, new, idx, a), 'code'
            if a in old.funcset:  # an entry maps to an entry, and its callers' matches call it
                if any(c in new.funcset for c in v): v = Counter({c: n for c, n in v.items() if c in new.funcset})
                cv = map_by_callers(old, new, idx, a)
                if cv:  # the callers decide: a short function's windows run into its neighbours
                    agree = v and v.most_common(1)[0][0] == cv.most_common(1)[0][0]
                    how = 'calls' if agree else 'calls (windows said %s)' % (' '.join('%#x:%d' % kv for kv in v.most_common(2)) or 'nothing')
                    v = cv
        elif a in old.rel and map_table(old, new, idx, a):
            v, how = map_table(old, new, idx, a), 'table'
        else:
            v, exact = map_data(old, new, idx, a)
            how = 'data' if exact else 'data-near'
        if not v:
            print('%#x ? none' % a); continue
        best, n = v.most_common(1)[0]
        flag = '' if len(v) == 1 else ' disputed ' + ' '.join('%#x:%d' % kv for kv in v.most_common(4))
        print('%#x %#x %s %d%s' % (a, best, how, n, flag))


if __name__ == '__main__':
    main()
