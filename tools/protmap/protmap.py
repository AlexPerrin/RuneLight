#!/usr/bin/env python3
"""Android game-protocol mapping per revision.

  protmap.py extract <rev>   size tables, constructor, decompiled dispatcher + senders, read/write sequences
  protmap.py match <rev>     opcode -> prot: size, field widths, carry-over from the previous revision, hand
  protmap.py check <rev>     static checks and the round-trip test; nonzero exit on a failure
  protmap.py generate <rev>  enum rows and the docs table from mapping.tsv
  protmap.py all <rev>       the four in order

Inputs per revision: protmap/<rev>/anchors.json ({so, project, program, dispatcher, rsprot, prev}) and
protmap/<rev>/hand.tsv (by-hand rows). Bulky work (decompiled C, objdump) goes under re/protmap/<rev>/.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))

import extract  # noqa: E402
import match  # noqa: E402
import rsprot  # noqa: E402
import validate  # noqa: E402


def anchors(rev):
    a = json.load(open(os.path.join(REPO, 'protmap', rev, 'anchors.json')))
    for k in ('so', 'project', 'rsprot'):
        a[k] = os.path.expanduser(a[k])
    return a


def main(cmd, rev):
    a = anchors(rev)
    rec = os.path.join(REPO, 'protmap', rev)
    work = os.path.join(os.path.expanduser(a.get('work', '~/osrs-plugins-reverse/re/protmap')), rev)
    rs = rsprot.Rev(a['rsprot'], rev)
    if cmd in ('extract', 'all'):
        x = extract.extract(work, a['so'], a['project'], a.get('program', 'osclient.so'), int(a['dispatcher'], 0),
                            list(rs.s2c.values()), list(rs.c2s.values()))
        json.dump({k: v for k, v in x.items() if k not in ('s2c', 'c2s')}, open(os.path.join(rec, 'tables.json'), 'w'),
                  indent=1, sort_keys=True)
        with open(os.path.join(rec, 'extracted.tsv'), 'w') as f:
            f.write(extract.tsv(x))
    if cmd in ('match', 'all'):
        match.run(rec, work, rs, a.get('prev'))
    if cmd in ('check', 'all'):
        if validate.run(rec, work, rs):
            sys.exit(1)
    if cmd in ('generate', 'all'):
        match.generate(rec, rev)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
