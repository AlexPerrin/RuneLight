# protmap check, rev 241

Round-trip: 72 pass, 1 fail, 167 not run (var-size, no Android codec, the client layout not fully read, or shapes differ).

## Failures (1)

- c2s 68 OPPLAYER7 round-trip: field 1: codec p1Alt2, client p1

## Shapes not compared (16)

The client reads a field in pieces (a packed coordinate or colour byte by byte), or the codec branches, so field-by-field comparison does not apply; the size check still does.

- c2s 9 OPNPC5_V2: client widths [1, 1, 1, 1], RSProt [1, 1, 2]
- c2s 9 OPNPC5_V2 round-trip not compared: codec p2Alt2 p1Alt2 p1Alt1, client p1 p1Alt1 p1Alt2 p1Alt1
- c2s 33 OPNPC4_V2: client widths [1, 1, 1, 1], RSProt [1, 1, 2]
- c2s 33 OPNPC4_V2 round-trip not compared: codec p1Alt1 p1Alt3 p2Alt3, client p1Alt1 p1Alt3 p1Alt1 p1
- c2s 66 OPLOC5_V2: client widths [1, 1, 1, 1, 2, 2], RSProt [1, 1, 2, 2, 2]
- c2s 66 OPLOC5_V2 round-trip not compared: codec p1Alt3 p1Alt1 p2Alt3 p2Alt1 p2Alt1, client p1Alt3 p1Alt1 p2Alt3 p2Alt1 p1 p1Alt1
- s2c 12 OBJ_CUSTOMISE_SPECIFIC_V1: client widths [1, 1, 2, 2, 2, 2, 2, 4, 4], RSProt [2, 2, 2, 2, 2, 2, 4, 4]
- s2c 12 OBJ_CUSTOMISE_SPECIFIC_V1 round-trip not compared: codec p2 p2 p4 p2Alt2 p2Alt1 p2Alt2 p2Alt2 p4, client p2 p2 p4 p2Alt2 p1 p1 p2Alt2 p2Alt2 p4
- s2c 99 SET_ACTIVE_WORLD_V2: client widths [1, 2], RSProt [1, 1, 2, 2]
- s2c 99 SET_ACTIVE_WORLD_V2 round-trip not compared: codec p2 p1 p2 p1, client p2 p1
- s2c 103 IF_SETPLAYERMODEL_BODYTYPE: client widths [1, 2, 2], RSProt [1, 4]
- s2c 103 IF_SETPLAYERMODEL_BODYTYPE round-trip not compared: codec p1Alt2 p4Alt2, client p1Alt2 p2 p2
- s2c 104 LOC_ANIM_SPECIFIC: client widths [1, 1, 1, 1, 2], RSProt [1, 2, 3]
- s2c 104 LOC_ANIM_SPECIFIC round-trip not compared: codec p1Alt3 p2Alt1 p3Alt3, client p1Alt3 p2Alt1 p1 p1 p1
- s2c 108 CAM_TARGET_V4: client widths [1, 4], RSProt [1, 1, 1, 1, 4, 4, 4, 4]
- s2c 108 CAM_TARGET_V4 round-trip not compared: codec p4 p1Alt2 p4 p1Alt2 p4 p1Alt2 p4 p1Alt2, client p4 p1Alt3

## Bench against the hand-made mapping (0 differences)

