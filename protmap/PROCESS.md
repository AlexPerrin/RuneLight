# Android protocol mapping: one row per revision

`tools/protmap/protmap.py all <rev>` (extract, match, check, generate) writes `protmap/<rev>/`:
`anchors.json` (inputs: the .so, the Ghidra project, the dispatcher, RSProt, the previous revision, the
hand-made reference), `tables.json` (size tables and constructor the tool located), `extracted.tsv` (the
client's own read / write sequence per opcode), `hand.tsv` (a person's rows), `mapping.tsv` (opcode, prot,
size, how it was identified, client layout), `check.md` (static checks, round-trip, bench),
`enums.kt.txt` and `mapping.md` (generated). The aim: fewer by-hand rows each revision.

| Rev | Opcodes S->C / C->S | size | widths | carry-over | by hand | dead | open | check failures | bench differences | Tooling changed for the next revision |
|---|---|---|---|---|---|---|---|---|---|---|
| 241 | 151 / 102 | 15 | 4 | 0 (no earlier revision) | 221 | 13 | 0 | 1 (an extractor limit, see below) | 0 | first revision through the tool: size tables found by multiset distance and the client's opcode bound, the constructor by its literal opcodes, layouts read from the decompiled C |
| 242 | 153 / 110 | 23 | 8 | 216 (182 by code, 33 by menu op, 1 by no dispatcher case) | 3 | 13 | 0 | 0 | 1 (explained below) | carry-over by menu op (a sender function's guard constants, opcodes set in a register and merged by the compiler); prots renamed by RSProt matched by base name and size (OPLOC1 -> OPLOC1_V2 / _V3); deprecated RSProt prots out of the pool; fingerprints keep numbers below 0x100, drop comments, declarations, register shuffles and the case's shared tail, and add each case's callees; a byte transform written back to its own variable (`c = -0x80 - c`) read |

241 check failure, the extractor's, not the codec's: C->S 68 OPPLAYER7 (the sender negates the ctrl byte after
the branches of all OPPLAYER senders join, past the straight-line code the tool reads). The hand map's p1Alt2
stands.

242 by hand: S->C 74 / 80 (IF_SETNPCHEAD / _ACTIVE: twin handlers whose code similarity ties, told apart by the
model type each sets, 2 or 6, as in 241) and 145 (SET_WEBSITE_TOKEN, new in 242). The bench difference: S->C 12 is
UNKNOWN_STRING (its dispatcher case reads one string and drops it), where the hand-made module has it dead ("no
handler"); the tool's row is the client's.

Next revision: twins that differ only in a small constant still tie on code similarity (74 / 80); weigh the
handler's own constants above its read code, which changes every revision.
