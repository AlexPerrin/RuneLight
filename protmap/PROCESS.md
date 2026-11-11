# Android protocol mapping: one row per revision

`tools/protmap/protmap.py all <rev>` (extract, match, check, generate) writes `protmap/<rev>/`:
`anchors.json` (inputs: the .so, the Ghidra project, the dispatcher, RSProt, the previous revision, the
hand-made reference), `tables.json` (size tables and constructor the tool located), `extracted.tsv` (the
client's own read / write sequence per opcode), `hand.tsv` (a person's rows), `mapping.tsv` (opcode, prot,
size, how it was identified, client layout), `check.md` (static checks, round-trip, bench),
`enums.kt.txt` and `mapping.md` (generated). The aim: fewer by-hand rows each revision.

| Rev | Opcodes S->C / C->S | size | widths | carry-over | by hand | dead | open | check failures | bench differences | Tooling changed for the next revision |
|---|---|---|---|---|---|---|---|---|---|---|
| 241 | 151 / 102 | 15 | 4 | 0 (no earlier revision) | 221 | 13 | 0 | 2 (extractor limits, see below) | 0 | first revision through the tool: size tables found by multiset distance and the client's opcode bound, the constructor by its literal opcodes, layouts read from the decompiled C |
| 242 | 153 / 110 | 25 | 9 | 170 | 0 | 17 | 42 | 46 (42 of them open rows) | 63 | in progress: carry-over picks wrong twins among near-identical handlers (HIDE*OPS, IF_SETNPCHEAD / _ACTIVE, OPPLAYER / OPWORLDENTITY); some 242 senders pass the opcode in a register |

241 check failures, both the extractor's, not the codec's: C->S 68 OPPLAYER7 (the sender negates the ctrl
byte after the branches of all OPPLAYER senders join, past the straight-line code the tool reads) and S->C
142 CAM_ROTATETO_COORDINATE_V3 byte 4 (the load is not consumed in the case's straight-line code, so its
transform is not seen). The hand map's p1Alt2 / p1Alt3 stand.
