# protmap check, rev 242

Round-trip: 84 pass, 0 fail, 166 not run (var-size, no Android codec, the client layout not fully read, or shapes differ).

## Failures (0)


## Shapes not compared (8)

The client reads a field in pieces (a packed coordinate or colour byte by byte), or the codec branches, so field-by-field comparison does not apply; the size check still does.

- s2c 6 CAM_TARGET_V4: client widths [1, 4], RSProt [1, 1, 1, 1, 4, 4, 4, 4]
- s2c 6 CAM_TARGET_V4 round-trip not compared: codec p1Alt1 p4Alt1 p1Alt1 p4Alt1 p1Alt1 p4Alt1 p1Alt1 p4Alt1, client p1Alt1 p4Alt1
- s2c 36 SET_ACTIVE_WORLD_V2: client widths [1, 2], RSProt [1, 1, 2, 2]
- s2c 36 SET_ACTIVE_WORLD_V2 round-trip not compared: codec p2 p1 p2 p1, client p2 p1
- s2c 93 LOC_ANIM_SPECIFIC: client widths [1, 1, 1, 1, 2], RSProt [1, 2, 3]
- s2c 93 LOC_ANIM_SPECIFIC round-trip not compared: codec p2Alt2 p1Alt3 p3Alt2, client p2Alt2 p1Alt3 p1 p1 p1
- s2c 127 OBJ_CUSTOMISE_SPECIFIC_V1: client widths [1, 1, 1, 1, 2, 2, 2, 2, 4, 4], RSProt [2, 2, 2, 2, 2, 2, 4, 4]
- s2c 127 OBJ_CUSTOMISE_SPECIFIC_V1 round-trip not compared: codec p2Alt2 p2Alt1 p2Alt2 p4Alt2 p2Alt1 p2Alt1 p4Alt1 p2Alt1, client p2Alt2 p1 p1 p2Alt2 p4Alt2 p2Alt1 p1 p1 p4Alt1 p2Alt1

## Bench against the hand-made mapping (1 differences)

- s2c 12: protmap UNKNOWN_STRING (size), reference OP_12
