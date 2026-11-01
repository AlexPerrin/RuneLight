# Android game protocol (build 241.4)

The official Android app logs in as `ENHANCED_ANDROID` (client type 7) and speaks its own game
protocol: opcodes are permuted relative to RSProt's desktop protocol **and** field orders/byte
transforms differ per packet. Login and JS5 are the same as desktop. The Android codecs live in
`server/rsprot` (module `osrs-241-android`, kept as `server/rsprot.patch`).

Addresses below are Ghidra addresses (ELF vaddr + 0x100000).

## Protocol status (2026-10-16)

| | S->C (151) | C->S (102) | Total (253) |
|---|---|---|---|
| Named (mapped) | 147 | 93 | 240 |
| Verified (live or from the client's own reader/sender) | 147 | 92 | 239 |
| Dead (`OP_n // dead:`) | 4: 0, 110, 116, 137 | 9: 1, 12, 27, 62, 73, 76, 83, 85, 87 | 13 |
| Open | none | 45 SET_HEADING | 1 |

How opcodes were verified:
- **S->C:** each one has a case in the dispatcher (`0x541dd4`), and its reads were matched field by
  field with RSProt's desktop encoder; many were also checked live on the device.
- **C->S:** each one has a sender found by a raw scan for calls to the packet constructor
  `0x79cc90`. The identity comes from its UI or CS2 caller, or from a live capture (`::trace`).
- **Dead:** S->C opcodes the dispatcher ignores or drops, and C->S opcodes with no sender. They stay
  unnamed, because nothing in the client ties them to a desktop prot.
- **Open:** 45 has a sender in the minimap click handler under a special mode, most likely the
  sailing boat heading. It can't be sent live until a server has sailing.
- Counts come from `tools/progress.py` (tags in `AndroidGameServerProt`/`AndroidGameClientProt`).

## Where things are in the client

| What | Where |
|---|---|
| Server→client size table (151 × int32, by opcode) | .rodata `0x104c4c` (file offset), via GOT `0xb826c8` |
| Client→server size table (102 × int32, by opcode) | .rodata `0x109538`, via GOT `0xb84320` |
| Packet reader (opcode + size, ISAAC) | `0x5da16c` |
| Packet dispatcher, one `case` per server opcode | `0x541dd4` |
| Outgoing packet constructor `(pkt, opcode, isaac)` | `0x79cc90` (opcode is a literal at each call site) |
| Login completion: reads gpi init then REBUILD fields | `0x51aa80` |
| PLAYER_INFO reader (bits + extended info) | `0x669a04` |
| NPC_INFO reader (flag: small/large) | `0x569778` |
| Config validator (param 15 = 0 requires jagex.com/runescape.com URL) | `0x50ee18`, `0x83d254` |
| Login RSA modulus (parsed by static init) | string at `0x1d8ebc`, `_INIT_141` |

Both size tables are permutations of RSProt's desktop sizes (plus one entry for desktop's
unassigned opcode), which is what made them easy to find.

## Byte transforms

| Client read | RSProt write |
|---|---|
| `b ^ 0x80` | `p1Alt1` |
| `-b` | `p1Alt2` |
| `-0x80 - b` (128 − b) | `p1Alt3` |
| `[lo^0x80][hi]` | `p2Alt3` |
| `[hi][lo^0x80]` | `p2Alt2` |
| native int, no swap | `p4Alt1` (little endian) |
| native int/short behind the `FUN_00b40c20` swap | `p4` / `p2` (big endian) |

## Server→client (identified)

| Opcode | Prot | Layout |
|---|---|---|
| 2 | UPDATE_STAT_V2 | current `p1`, boosted `p1Alt3`, stat `p1Alt1`, xp `p4Alt1` |
| 8 | UPDATE_INV_PARTIAL | as desktop |
| 6 | REBUILD_NORMAL_V2 (+ login variant: gpi block first) | worldArea `p2Alt3`, zoneX `p2`, zoneZ `p2` |
| 10 | RUNCLIENTSCRIPT | as desktop |
| 26 | UPDATE_RUNWEIGHT | `p2` |
| 28 | IF_SETANIM | anim `p2Alt1`, component `p4` |
| 30 | SET_NPC_UPDATE_ORIGIN | `p1`, `p1` |
| 32 | UPDATE_INV_FULL | header as desktop; per slot id+1 `p2Alt2`, count `p1Alt1`, big count `p4Alt3` |
| 42 | OBJ_ADD_SPECIFIC | timeUntilPublic `p2`, coord `p4Alt3`, ownership `p1Alt2`, neverPublic `p1Alt2`, id `p2`, timeUntilDespawn `p2Alt2`, opFlags `p1Alt2`, quantity `p4`; timeUntilDespawn 0 = expires immediately |
| 47 | IF_SETNPCHEAD_ACTIVE | component `p4Alt3`, slot `p2Alt2` |
| 34 | VARP_SMALL | value `p1Alt2`, id `p2` |
| 46 | LOGOUT | — |
| 55 | SERVER_TICK_END (probable) | — |
| 59 | IF_SETEVENTS_V2 | end `p2`, events1 `p4Alt3`, combined id `p4`, events2 `p4Alt1`, start `p2Alt3` (ops are events2 bits) |
| 62 | IF_SETNPCHEAD | component `p4`, npc `p2Alt1` |
| 64 | VARP_LARGE | id `p2Alt2`, value `p4Alt1` |
| 72 | IF_OPENSUB | dest `p4`, type `p1Alt3`, interface `p2Alt2` |
| 75 | IF_OPENTOP | `p2Alt3` |
| 77 | IF_SETHIDE | hidden `p1Alt3`, component `p4` (from client session; not implemented) |
| 79 | IF_SETPLAYERHEAD | component, middle-endian (from client session; not implemented) |
| 81 | MESSAGE_GAME | as desktop |
| 90 | PLAYER_INFO | bit layout as desktop; extended info below |
| 99 | SET_ACTIVE_WORLD_V2 | `p2`, `p1` (as desktop) |
| 101 | IF_CLOSESUB | component `p4` |
| 102 | UPDATE_RUNENERGY | `p2` |
| 109 | OBJ_DEL_SPECIFIC | coord `p4Alt1`, id `p2Alt2`, quantity `p4Alt1` |
| 122 | IF_SETTEXT | component `p4Alt3`, string |
| 145 / 150 | NPC_INFO_LARGE_V6 / NPC_INFO_SMALL_V6 | RSProt's NpcInfo bitstream; new NPCs use the Android order below |

| 7 | UPDATE_ZONE_PARTIAL_FOLLOWS | z `p1Alt3`, x `p1Alt1`, level `p1Alt1` |
| 68 | UPDATE_ZONE_FULL_FOLLOWS | level `p1Alt1`, z `p1Alt1`, x `p1Alt2` |
| 23 | UPDATE_ZONE_PARTIAL_ENCLOSED | level `p1Alt1`, z `p1Alt3`, x `p1`, then (Android zone index `p1`, payload)* |
| 4 / 14 / 16 / 17 / 48 / 105 | MAP_ANIM / LOC_ADD_CHANGE_V2 / LOC_DEL / LOC_ANIM / SOUND_AREA / LOC_MERGE | top-level zone prots, payload as in the zone table below |

### Server→client, second pass (2026-10-16)

Identified by reading each dispatcher case (helpers decompiled headlessly) and comparing reads with
RSProt's desktop encoder. "As desktop" = same order and transforms, so the Android encoder delegates.
Entries marked (unverified) have weaker identity or layout evidence (reason in the enum comment);
none of this table is verified live yet. Codecs: `AndroidMoreEncoders.kt`.

| Opcode | Prot | Layout |
|---|---|---|
| 0 | OP_0 (dead) | the dispatcher switch has no case 0 (like 116/137); by size and elimination it would be desktop SET_HEATMAP_ENABLED, but nothing in the client ties it, so it stays unnamed with no codec (2026-10-16) |
| 1 | CAM_RESET | as desktop (zeroes the camera state) |
| 3 | HINT_ARROW | as desktop (FUN_004fd3a4 switches on the desktop types) |
| 5 | NPC_HEADICON_SPECIFIC | index `p2Alt2`, headIconSlot `p1Alt2`, spriteGroup `p4Alt2`, spriteIndex `p2Alt1` |
| 9 | MESSAGE_CLANCHANNEL_SYSTEM | as desktop (chat type 43 / 46 guest) |
| 11 | CHAT_FILTER_SETTINGS_PRIVATECHAT | privateChatFilter `p1` (> 2 stored as -1) into client+0x33c4. The CS2 chat_setfilter op (0x5be1a0) pops public/private/trade into +0x33c0/+0x33c4/+0x33c8 (private clamped to 0-2) and sends them as C->S 47 in that order, so +0x33c4 is the private filter. The tab label does not redraw by itself (live 2026-10-15/16) |
| 12 | OBJ_CUSTOMISE_SPECIFIC_V1 | retex `p2`, retexIndex `p2`, coordGrid.packed `p4`, model `p2Alt2`, recolIndex `p2Alt1`, id `p2Alt2`, recol `p2Alt2`, quantity `p4` |
| 13 | UPDATE_IGNORELIST | as desktop (FUN_0066ee54 on the ignore list) |
| 15 | CAM_MODE | as desktop (oculus orb state) |
| 18 | UPDATE_INV_STOPTRANSMIT | as desktop (inventory p2Alt2, clears it) |
| 19 | CLANCHANNEL_FULL | as desktop: signed clanType p1 (payload of 1 byte = leave), body FUN_0056e768 -> 0x6f8a3c (flags, version, p8 hash, p8 updateNum, name, kick/talk rank, members) |
| 20 | HIDELOCOPS | hidden `p1` (b & 1); verified live (loc ops vanish) |
| 21 | UPDATE_UID192 | as desktop (24 bytes + crc32) |
| 22 | VARP_RESET | as desktop (zeroes all varps) |
| 24 | URL_OPEN | one plain gjstr (FUN_0058d824 flag 0) to a Java bridge call (vtable +0xf0); encoder bound for `::url`. Live 2026-10-16 (twice): `::url https://example.com` drops the app to the login screen ("Unexpected server response" from the reconnect, no server error), and no activity starts (logcat), so no browser opens: the client closes the session itself. Identity doubtful; next lead is what the bridge slot +0xf0 does (the app has `ApplicationContext.openBrowser`/`openOverlaidBrowser`). Client dev (bd077c0, static): the bridge accepts only http(s):// or market:// URLs and Java `openBrowser` shows an "opening a link to an external site" AlertDialog. Third live try 2026-10-16 18:56 (devtools build): no dialog; the app process died with SIGSEGV (SEGV_MAPERR, fault addr 0x78401024096) in thread osrsrl-game, backtrace only in libhoudini, so the earlier "session drops" were probably this crash. Cause found statically (fifth server dev): the packet reader `0x5da16c` decrypts every payload byte of opcode 0x18 with ISAAC (`b - isaac.next()`), as desktop does (RSProt `UrlOpenEncoder.encryptedPayload`), but `AndroidDelegatingEncoder` did not pass the flag on, so the payload went out in clear and the client's cipher fell out of step (stock client: session drop; with our hooks: crash). Fixed by forwarding `encryptedPayload`. **Verified live 2026-10-16 19:42**: `::url https://example.com` showed "You are opening a link to an external site..." (CANCEL/OK); CANCEL kept the session |
| 25 | UPDATE_FRIENDLIST | as desktop (FUN_0066e448) |
| 27 | LOGOUT_WITHREASON | as desktop (reason p1) |
| 29 | IF_SETCOLOUR | combinedId `pCombinedIdAlt1`, colour15BitPacked `p2Alt2` |
| 31 | MINIMAP_TOGGLE | minimapState `p1`; verified live (2 blanks the minimap) |
| 33 | UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER | as desktop (FUN_00569184) |
| 35 | LOGOUT_TRANSFER | as desktop (host, id, properties) |
| 36 | MESSAGE_PRIVATE_ECHO | as desktop (chat type 6) |
| 37 | NPC_SPOTANIM_SPECIFIC | index `p2Alt3`, slot `p1Alt3`, id `p2`, height `p2Alt1`, delay `p2Alt1` |
| 38 | ACCOUNT_FLAGS | as desktop (p8) |
| 39 | UPDATE_TRADINGPOST_V2 | as desktop (FUN_006ff0cc(version 2)) |
| 40 | UPDATE_FRIENDCHAT_CHANNEL_FULL_V2 | as desktop (FUN_005689fc) |
| 41 | REBUILD_REGION_V2 | see notes; verified live (`::region 0 0 1` rebuilt the area with every zone rotated) |
| 43 | MESSAGE_FRIENDCHANNEL | as desktop (chat type 9) |
| 44 | OCULUS_SYNC | as desktop (p4) |
| 45 | IF_SETPOSITION | combinedId `pCombinedIdAlt3`, y `p2Alt2`, x `p2Alt2` |
| 49 | RESET_ANIMS | as desktop (resets every actor sequence/spotanim) |
| 50 | ANIM_SPECIFIC | as desktop (local player anim) |
| 51 | CAM_SHAKE | as desktop (camera shake arrays) |
| 52 | IF_CLEARINV | combinedId `pCombinedIdAlt1` |
| 53 | VARP_LONG | (value ushr 32).toInt() `p4Alt1`, value.toInt() `p4Alt1`, id `p2` |
| 54 | UPDATE_STOCKMARKET_SLOT_V1 | as desktop (FUN_00700428) |
| 56 | CAM_ROTATEBY | as desktop (relative yaw/pitch) |
| 57 | MIDI_SONG_V2 | fadeInDelay `p2Alt2`, id `p2`, fadeInSpeed `p2Alt3`, fadeOutSpeed `p2Alt1`, fadeOutDelay `p2` |
| 58 | UPDATE_TRADINGPOST_V1 | as desktop (FUN_006ff0cc(version 1)) |
| 60 | PACKET_GROUP_START | length `p2` (BE); handled in the reader 0x5da16c, which groups the following packets; no codec |
| 61 | UPDATE_REBOOT_TIMER_V2 | [gameCycles p2][message string]; "\u0018" cancels |
| 63 | IF_SETPLAYERMODEL_BASECOLOUR | index `p1Alt1`, colour `p1Alt2`, combinedId `pCombinedIdAlt2` |
| 65 | SITE_SETTINGS | settings `pjstr` (as desktop); stored in the client and passed to the URL builder FUN_0070332c by the CS2 URL ops |
| 66 | UPDATE_STOCKMARKET_SLOT_V2 | as desktop: [slot][status; 7 = version prefix, then version 2 and the status][obj p2][price p8][count p4][done p4][gold p8]; a second byte of 0 resets the slot (FUN_00700408: state 0, type 1). Device-checked 2026-10-17 (SELLING/SOLD/BUYING/CANCELLED_BUY in the GE plugin; slot object layout confirmed with the host's `ge raw` probe). RSProt's Reset wrote slot 0 instead of the slot, fixed in rsprot.patch |
| 67 | VARCLAN_ENABLE | as desktop (creates the varclan store) |
| 69 | FRIENDLIST_LOADED | as desktop (friend manager state) |
| 70 | REBUILD_WORLDENTITY_V4 | recognised, no codec |
| 71 | SYNTH_SOUND | as desktop (FUN_005ae508(id, loops, delay)) |
| 73 | CAM_ROTATETO | as desktop (absolute yaw/pitch) |
| 74 | MIDI_SONG_WITHSECONDARY | fadeInSpeed `p2Alt3`, primaryId `p2Alt1`, secondaryId `p2`, fadeInDelay `p2Alt3`, fadeOutDelay `p2`, fadeOutSpeed `p2` |
| 76 | PROJANIM_SPECIFIC_V4 | sourceIndex `p3Alt2`, progress `p2`, startHeight `p2`, targetIndex `p3Alt3`, startTime `p2Alt3`, end `p4Alt1`, start `p4Alt3`, angle `p1`, id `p2Alt1` (0xffff = none), endTime `p2Alt2`, endHeight `p2Alt3` (byte orders read and re-checked against RSProt's Alt helpers 2026-10-16. Struct slots (case 76 and zone MAP_PROJANIM_V2 both fill a 0x3c-byte struct passed to the projectile constructor `0x577b14`, which copies it to object+0x25c and looks up the spotanim from S+0x38): S+0/4/8 start level/x/z (confirmed: the zone case puts the zone base + packed offset there), S+0xC startHeight?, S+0x10 sourceIndex, S+0x14/18/1C end coord, S+0x20 endHeight?, S+0x24 targetIndex, S+0x28 and S+0x2C cycle + value (start/end time, order unconfirmed), S+0x30 angle (u8), S+0x34 progress?, S+0x38 id. Verified live 2026-10-16: `::proj 100 4 0` (start height 40) flew a fire-strike orb from the player to 4 tiles east; mode 2 (heights swapped) started at the feet. The 2026-10-15 failure was the start height 160 (x4 = 640 units, off camera). Times confirmed too: the orb was in flight 1.5 s after the command, so S+0x28 (30) is the start cycle and S+0x2C (300) the end) |
| 78 | CHAT_FILTER_SETTINGS | publicChatFilter `p1Alt1`, tradeChatFilter `p1Alt3` |
| 80 | MIDI_JINGLE | lengthInMillis `p3Alt1`, id `p2Alt3` |
| 82 | HISCORE_REPLY | recognised, no codec |
| 83 | IF_SETPLAYERMODEL_OBJ | combinedId `pCombinedId`, obj `p4Alt3` |
| 84 | CLANSETTINGS_FULL | as desktop (version 6 layout), body FUN_0056e7e8 -> 0x6fb06c |
| 85 | SET_INTERACTION_MODE | as desktop (world id + two modes) |
| 86 | MIDI_SONG_STOP | fadeOutSpeed `p2`, fadeOutDelay `p2` |
| 87 | CLANSETTINGS_DELTA | as desktop: p8 owner, p4 updateNum, p1 sub-ops 1-15 until 0 (FUN_0056ea04 -> 0x6fe6f0) |
| 88 | CLANCHANNEL_DELTA | as desktop: p8 hash, p8 updateNum, p1 sub-ops 1-5 until 0 (FUN_0056e910 -> 0x6fab28) |
| 89 | CAM_TARGET_V3 | as desktop (type, world entity, target) |
| 91 | NPC_ANIM_SPECIFIC | index `p2`, id `p2`, delay `p1Alt2` |
| 92 | SEND_PING | as desktop (client answers with opcode 19) |
| 93 | MESSAGE_PRIVATE | as desktop (chat type 3/7) |
| 94 | CAM_SMOOTHRESET | as desktop (four speeds) |
| 95 | OBJ_ENABLED_OPS_SPECIFIC | id `p2Alt2`, coordGrid.packed `p4`, opFlags.toInt() `p1Alt1` |
| 96 | VARP_SYNC | as desktop (copies server varps) |
| 97 | OBJ_UNCUSTOMISE_SPECIFIC | id `p2Alt3`, coordGrid.packed `p4Alt3`, quantity `p4Alt3` |
| 98 | PLAYER_SPOTANIM_SPECIFIC | id `p2Alt2`, (height shl 16) or delay `p4Alt1`, index `p2Alt3`, slot `p1Alt2` |
| 100 | IF_SETPLAYERMODEL_SELF | if (copyObjs) 1 else 0 `p1`, combinedId `pCombinedIdAlt1` |
| 103 | IF_SETPLAYERMODEL_BODYTYPE | bodyType `p1Alt2`, combinedId `pCombinedIdAlt2` |
| 104 | LOC_ANIM_SPECIFIC | locPropertiesPacked `p1Alt3`, id `p2Alt1`, coordInBuildAreaPacked `p3Alt3` |
| 106 | IF_SETANGLE | zoom `p2Alt2`, angleX `p2Alt2`, angleY `p2Alt3`, combinedId `pCombinedId` |
| 107 | TRIGGER_ONDIALOGABORT | as desktop (root interface event 0x17) |
| 108 | CAM_TARGET_V4 | [index p4][type -b] |
| 110 | OP_110 (dead) | one string, read into a local and dropped (case 0x6e); every desktop S->C prot except REFLECTION_CHECKER already has an Android opcode, so it stays unnamed; no codec |
| 111 | MAP_ANIM_SPECIFIC | delay `p2Alt3`, height `p1Alt3`, coordInBuildAreaPacked `p3Alt3`, id `p2Alt3` |
| 112 | SET_PLAYER_OP | if (priority) 1 else 0 `p1Alt1`, id `p1Alt1`, op ?: "null" `pjstr` |
| 113 | HIDENPCOPS | hidden `p1` (b & 1); verified live (NPC ops vanish) |
| 114 | RESET_INTERACTION_MODE | as desktop (world id p2) |
| 115 | HIDEOBJOPS | hidden `p1` (b & 1); verified live 2026-10-16 (`::hideops obj 1`: a ground Pot showed only Walk here; 0 restored Take/Examine) |
| 117 | MESSAGE_CLANCHANNEL | as desktop (chat types 41/44) |
| 118 | OBJ_COUNT_SPECIFIC | newQuantity `p4Alt1`, id `p2Alt2`, oldQuantity `p4Alt2`, coordGrid.packed `p4Alt1` |
| 119 | VARCLAN | as desktop (id + typed value) |
| 120 | IF_SETROTATESPEED | combinedId `pCombinedIdAlt3`, xSpeed `p2`, ySpeed `p2`: component +0x134 = x << 16 \| y, and the interface tick (FUN_005600a4) adds cycles * x to +0x11c and cycles * y to +0x120, the angleX/angleY of IF_SETANGLE (106) |
| 121 | IF_RESYNC_V2 | as desktop (toplevel `p2`, count `p2`, (combinedId `p4`, id `p2`, type `p1`)*, then (combinedId `p4`, start `p2`, end `p2`, events1 `p4`, events2 `p4`)* to the end) |
| 123 | MIDI_SWAP | fadeInDelay `p2`, fadeInSpeed `p2Alt3`, fadeOutSpeed `p2Alt1`, fadeOutDelay `p2Alt3` -> FUN_007907c8(outDelay, outSpeed, inDelay, inSpeed). The four globals are the ones MIDI_SONG_STOP (86) and the song player FUN_0078e970 use; globals 1 and 3 feed wait tasks (delays), global 2 a fade task (speed) |
| 124 | VARCLAN_DISABLE | as desktop (clears the varclan store) |
| 125 | IF_SETSCROLLPOS | combinedId `pCombinedIdAlt3`, scrollPos `p2` |
| 126 | IF_MOVESUB | destinationCombinedId `pCombinedId`, sourceCombinedId `pCombinedId` |
| 127 | IF_SETOBJECT | count `p4Alt2`, obj `p2Alt3`, combinedId `pCombinedIdAlt1` |
| 128 | CAM_UNLOCK | as desktop (p1Alt3) |
| 129 | CAM_MOVETO_ARC_V3 | destinationX `p2Alt3`, centerX `p2`, centerZ `p2Alt1`, destinationZ `p2Alt3`, if (ignoreTerrain) 1 else 0 `p1Alt2`, easing.id `p1Alt2`, cycles `p2Alt2`, height `p2`, if (heightRelative) 1 else 0 `p1Alt1` |
| 130 | IF_SETMODEL_V2 | model `p4Alt1`, combinedId `pCombinedIdAlt1` |
| 131 | CAM_LOOKAT_V3 | x `p2Alt1`, rate2 `p1`, height `p2`, z `p2Alt1`, rate `p1Alt3`, if (heightRelative) 1 else 0 `p1` |
| 132 | OBJ_CUSTOMISE_SPECIFIC_V2 | see notes |
| 133 | CAM_MOVETO_CYCLES_V3 | easing.id `p1Alt3`, x `p2`, cycles `p2`, z `p2`, if (ignoreTerrain) 1 else 0 `p1`, if (heightRelative) 1 else 0 `p1`, height `p2` |
| 134 | GROUP_VAR_INT | as desktop (shared handler FUN_006871e0; value width from the packed var type, 0 = p4) |
| 135 | OBJUNLOCK_RESET | as desktop (frees the obj-unlock array) |
| 136 | CAM_MOVETO_V3 | if (heightRelative) 1 else 0 `p1Alt1`, height `p2Alt1`, x `p2Alt1`, rate `p1Alt2`, z `p2Alt2`, rate2 `p1` |
| 138 | GROUP_VAR | as desktop (shared handler FUN_006871e0, loops to the end of the payload) |
| 139 | OBJUNLOCK_UPDATE | as desktop (obj-unlock long array) |
| 140 | GROUP_FULL | as desktop (FUN_006863e8 -> 0x68647c: index, smart1or2null id, p8 uid, vars by config type) |
| 141 | GROUP_VAR_LONG | as desktop (shared handler FUN_006871e0; packed type 1 = p8) |
| 142 | CAM_ROTATETO_COORDINATE_V3 | height `p2Alt1`, if (trackTarget) 1 else 0 `p1`, easing.id `p1Alt3`, cycles `p2Alt1`, if (heightRelative) 1 else 0 `p1Alt1`, z `p2Alt3`, x `p2Alt1` |
| 143 | SET_MAP_FLAG_V2 | coord `p4` (as desktop), -1 clears the flag; verified live (`::flag x z` shows the minimap flag) |
| 144 | CAM_LOOKAT_CYCLES | z `p2Alt1`, x `p2Alt1`, easing.id `p1Alt3`, height `p2Alt2`, if (heightRelative) 1 else 0 `p1`, cycles `p2Alt2` |
| 146 | WORLDENTITY_INFO_V7 | recognised, no codec |
| 147 | AMBIENTSOUND_START | id `p2Alt2`, if (fade) 1 else 0 `p1` |
| 148 | CAM_SKYBOX | model `p4` (see notes) |
| 149 | AMBIENTSOUND_STOP | if (fade) 1 else 0 `p1Alt1` |

Notes:
- The jump table is at vaddr 0x1040f6 (u16 entries, target = 0x441e8c + 4 × entry). Opcodes 0, 60,
  116 and 137 go straight to the exit path (the client ignores them; 60 is consumed in the reader).
- 0 / 20 / 113 / 115 (unverified): the server size table has 10 size-1 opcodes (0, 11, 15, 20, 27,
  31, 113, 115, 128, 149) for desktop's 10 size-1 prots; the other six are mapped, leaving
  SET_HEATMAP_ENABLED / HIDENPCOPS / HIDELOCOPS / HIDEOBJOPS. Cases 20, 113 and 115 store `b & 1` at
  bytes +4, +3 and +5 of one settings struct (`*(client+0x4132b0)+0x3b8`, accessor FUN_0068fa48;
  a CS2 op at 0x5c2b10 writes +2), and opcode 0 is ignored by the dispatcher, so 0 is taken as the
  heatmap flag. Live test 2026-10-15 (`::hideops npc|loc|obj [1|0]`): 113 hides NPC ops, 20 hides
  loc ops, so 115 is HIDEOBJOPS by elimination (struct bytes: +3 npc, +4 loc, +5 obj).
- 116 and 137 (var-short) are ignored by the dispatcher: REFLECTION_CHECKER (no Java reflection on
  Android) and the opcode desktop leaves unassigned, in some order. Left unnamed, tagged `// dead:` as
  `OP_116` / `OP_137` (2026-10-16).
- 148 CAM_SKYBOX: the last size-4 prot once 143 = SET_MAP_FLAG_V2 (stores an int; -1 clears a
  world-view resource).
  Verified statically 2026-10-16 (fifth server dev): the frame code at 0x552260 reads the stored
  int; when it is not -1 and differs from renderer+0x54dbc, it loads a resource for it (0x81d824 on
  client+0x23f8, then a vtable +0x80 call) and passes id + resource to 0x7d971c, which keeps them at
  renderer+0x54dbc/+0x54dc0. The -1 path's 0x7d97c8 clears exactly those fields. That is desktop
  CAM_SKYBOX's "set the skybox model, -1 clears". Not tried live (no known skybox model id).
- 41 REBUILD_REGION_V2 (FUN_00655510 -> parser 0x6580e4): reload `p1Alt3` (raw byte 0x7f = reload),
  zoneX `p2Alt1`, zoneZ `p2Alt1` (order from the base-coord call FUN_006d0a94(level 0, x*8-48,
  z*8-48)), then as desktop: distinct mapsquare count `p2`, and for 4 levels x 13 x 13 zones a 1-bit
  present flag + 26-bit reference zone. Verified on device 2026-10-15.
- 132 OBJ_CUSTOMISE_SPECIFIC_V2: id `p2`, recol count `p1`, (index `p1Alt1`, value `p2Alt3`)*,
  colour flag `p1Alt3` [+ colour `p2Alt2`], model `p2Alt3`, retex count `p1`, (index `p1Alt1`,
  value `p2`)*, coord `p4`, quantity `p4`. The colour flag is present when the raw byte is 0x7f
  (p1Alt3 of 1). Recolour vs retexture: the lists go to the same vectors as case 12's recol/retex.
  Encoder `AndroidObjCustomiseSpecificV2Encoder` (not tested on device).
- NPC vs player lookups: FUN_00685094 resolves NPCs (5, 37, 91), FUN_00684e20 players (98).

### Clan and group bodies (static, 2026-10-16)

The client's parsers were decompiled (Ghidra, `re/decomp/`) and compared field by field with
RSProt's desktop encoders. All eight match, with every value plain big-endian (p2/p4/p8 through the
FUN_00b40c20 swap) and strings plain gjstr, so the Android module reuses the desktop encoders.
- **Which is which:** 19 and 88 share the clan-channel slots (`+0x4660/+0x4670`), and 88 compares a
  long updateNum. 84 and 87 share the clan-settings slots (`+0x4610/+0x4620`), and 87 compares an
  int updateNum.
- **Delta sub-ops:** settings 1-15 and channel 1-5 have the same codes and widths as desktop. The
  hash read peeks the first wire byte (0xFF means no hash), while RSProt tests the low byte of the
  hash. This is harmless for real name hashes, and desktop does the same.
- **Groups:** 134/138/141 share FUN_006871e0, a loop of p1 index, p4 packed var, then a value
  whose width the packed type sets (0 = p4, 1 = p8, 2 = string). So size 9 is INT, size 13 is LONG,
  and the var-short one is the multi-update GROUP_VAR.
- **GROUP_FULL caveat:** the client's group config has a third var list, scope 2, read after the
  member vars. RSProt models only two lists, so a cache group with scope-2 vars would need them
  appended.

## Zone packets

Zone x/z in the headers are build-area tiles of the zone's south-west corner (build area starts
6 zones south-west of the rebuild zone); level is the plane. Zone prots carry coord-in-zone
`x << 4 | z`. All zone prots are handled by one function (Ghidra `0x56edb0`) that switches on
an Android index, which is also the sub-opcode inside PARTIAL_ENCLOSED:

| Index | Prot | Size | Payload (implemented ones) |
|---|---|---|---|
| 0 | MAP_ANIM | 6 | coord `p1Alt2`, id `p2Alt3`, delay `p2Alt2`, height `p1Alt2` |
| 1 | LOC_ADD_CHANGE_V2 | var | op count `p1Alt2`, (op index 0-4 `p1`, string)*, shape<<2\|rot `p1`, coord `p1`, opFlags `p1Alt3`, id `p2Alt2` |
| 2 | LOC_ANIM | 4 | shape<<2\|rot `p1Alt1`, anim id `p2Alt1`, coord `p1` |
| 3 | OBJ_ENABLED_OPS | 4 | coord `p1`, id `p2`, opFlags `p1` |
| 4 | OBJ_UNCUSTOMISE | 7 | quantity `p4`, coord `p1Alt3`, id `p2Alt1` |
| 5 | OBJ_DEL | 7 | coord `p1`, quantity `p4Alt2`, id `p2Alt3` |
| 6 | MAP_PROJANIM_V2 | 24 | endTime `p2Alt3`, startHeight `p2Alt1`, endHeight `p2Alt3`, progress `p2Alt3`, end coord `p4`, id `p2Alt2`, targetIndex `p3`, coord `p1`, startTime `p2Alt3`, sourceIndex `p3Alt1`, angle `p1Alt2` (start/end time and endHeight/progress by struct order, unverified) |
| 7 | OBJ_CUSTOMISE | 17 | quantity `p4`, coord `p1`, retexIndex `p2Alt1`, model `p2Alt2`, recolIndex `p2Alt1`, recol `p2Alt3`, retex `p2Alt1`, id `p2Alt2` |
| 8 | OBJ_COUNT | 11 | coord `p1`, newQuantity `p4`, oldQuantity `p4Alt2`, id `p2Alt3` |
| 9 | LOC_MERGE | 14 | skipped by the client (14 bytes, ignored); server writes the desktop layout |
| 10 | LOC_DEL | 2 | shape<<2\|rot `p1Alt3`, coord `p1` |
| 11 | OBJ_ADD | 14 | neverPublic `p1Alt1`, timeUntilDespawn `p2Alt1`, quantity `p4Alt1`, ownership `p1Alt1`, coord `p1`, id `p2`, opFlags `p1Alt2`, timeUntilPublic `p2Alt2` |
| 12 | SCRIPTEDPROJ_ADD | 37 | sourceHeight `p2Alt1`, id `p2Alt3`, sourceIndex `p3`, targetIndex `p3Alt2`, targetOffsetX `p2Alt3`, curveScriptT `p2`, sourceOffsetX `p2Alt3`, slot `p2Alt1`, endTime `p2Alt1`, targetCoord `p4`, curveScriptH `p2Alt1`, targetOffsetZ `p2Alt3`, targetHeight `p2Alt3`, sourceOffsetZ `p2Alt3`, startTime `p2Alt1`, curveScriptA `p2`, coord `p1Alt3` (offset/height/time/curve roles by struct order, unverified) |
| 13 | SOUND_AREA | 7 | id `p2Alt3`, loops `p1Alt2`, range `p1`, dropOffRange `p1Alt2`, delay `p1`, coord `p1` |
| 14 | SCRIPTEDPROJ_CHANGE | 18 | targetOffsetX `p2Alt3`, deleteOnFreezeEnd `p1`, slot `p2Alt1`, targetHeight `p2Alt1`, targetIndex `p3Alt2`, targetCoord `p4Alt3`, targetOffsetZ `p2`, freezeDuration `p2` (offset/height roles by struct order) |

Indices were matched by payload size and by what each case does with the tile's loc/obj lists
(OBJ_ADD's argument order against OBJ_ADD_SPECIFIC, which calls the same function `0x55775c`).
The server sends them with `AndroidUpdateZonePartialEnclosed` + `AndroidZoneProts.encode`; all 15
have encoders (`AndroidZoneProts.write`). `::zfx mapanim|locanim|sound|proj id ...` exercises some.

## Player extended info

Flag bytes: 1–3; `0x80` in the first byte announces a second, `0x4000` a third (desktop uses
`0x2000`). Blocks are read in this order:

`0x2` reset, `0x200` tinting, `0x4` sequence, `0x20000` headbars, `0x40` face, `0x100` move speed,
`0x8000` ? (three strings), `0x2000` temp move speed, `0x10000` ?, `0x400` chat, `0x10`
appearance, `0x1` say, `0x80000` ?, `0x100000` ?, `0x20` ?, `0x1000` ?, `0x40000` hitmarks

| Block | Layout |
|---|---|
| sequence `0x4` | id `p2Alt2`, delay `p1` |
| face `0x40` | flag (walkMode \| kind << 3 \| instant << 6) `p1Alt3`, then the desktop face payload (parser `0x4fb10c`: kind 0 entity = type smart, index smart2or4null, fallback angle smart; 1 loc = x, z, sizes smarts; 2 angle = smart; 3 reset) |
| move speed `0x100` | `p1Alt2` |
| temp move speed `0x2000` | `p1Alt1` (127 = teleport) |
| chat `0x400` | colour<<8\|effects `p2Alt3`, modicon `p1Alt1`, autotyper `p1`, huffman length `p1Alt2`, huffman `pdataAlt3`, pattern bytes `p1Alt1` |
| appearance `0x10` | length `p1Alt1`, payload `pdataAlt3` (reversed, +128); payload as desktop |
| headbars `0x20000` | count `p1`; per bar: type smart, endTime smart (0x7FFF = remove, nothing follows), startTime smart, startFill `p1Alt2`, endFill `p1Alt3` only if endTime != 0 (client: `0x572780` → `0x575130(cycle+startTime, startFill, endFill, endTime)`) |
| hitmarks `0x40000` | count `p1Alt1`; per hit: type, value, delay, limit, all smart1or2 (client: `0x572240(type, value, delay, cycle, limit)`) |

## Client→server (identified)

| Opcode | Prot | Layout |
|---|---|---|
| 31 | MOVE_GAMECLICK | x `g2Alt3`, z `g2Alt2`, keyCombination `g1Alt1` |
| 64 | MESSAGE_PUBLIC | as desktop (type, colour, effect, huffman) |
| 81 | CLIENT_CHEAT | as desktop (string, autocomplete byte) |
| 93 | NO_TIMEOUT (probable) | — |
| 48 | RESUME_PAUSEBUTTON | component `g4Alt1` (int LE), sub `g2Alt1` (LE); chatmenu options arrive as 219:1 sub = option (1-based) |
| 99 | IF_BUTTONX | as desktop (combined id, sub, obj, op); inventory item ops arrive as 149:0 with sub = slot, op 3 = wield/wear |
| 7, 55, 58, 3, 66 | OPLOC1..5 | see below (sender Ghidra `0x61d10c`; verified OPLOC1 on device) |
| 38 | OPLOC6 (examine) | id `g2` (observed `0464` = bush 1124) |
| 63 | OPLOCT | ctrl `g1Alt1`, selected combined id `g4Alt1`, id `g2Alt1`, selected sub `g2Alt3`, selected obj `g2Alt2`, x `g2`, z `g2` (sender `0x61cfa8`) |
| 74 | OPOBJT | selected sub `g2`, ctrl `g1Alt2`, id `g2`, selected combined id `g4`, z `g2Alt2`, x `g2Alt2`, selected obj `g2Alt1` (sender `0x6205c4`) |
| 2 | OPNPCT | selected sub `g2Alt2`, index `g2Alt2`, selected combined id `g4Alt3`, ctrl `g1Alt3`, selected obj `g2Alt3` (sender `0x61f9fc`) |
| 38 / 6 / 49 | OPLOC6 / OPOBJ6 / OPNPC6 | examine; 6 (6 bytes) is OPOBJ6's sender `0x6211ac`, 49 (2 bytes) sender `0x620400` |

The T packets (63, 74, 2) were decoded from the senders and verified live (bush, cabbage, Hans; inventory "Use" needs events1 target bits 11-16). Other single-packet
senders near them: 5 (11 bytes, `0x622bd4`, probably OPWORLDENTITYT), 69 (`0x623550`), 45
(`0x623960`), 35 (`0x623a28`), 31 = MOVE_GAMECLICK (`0x61cbb8`).
| 79, 24, 82, 34, 40 | OPOBJ1..5 | see below (sender `0x62073c`) |
| 92, 44, 71, 33, 9 | OPNPC1..5 | see below (sender `0x61fc20`) |

Op packets (fields in wire order; `p2a2` = `[hi][lo+128]`, `p2a3` = `[lo+128][hi]`, `p2a1` =
little endian, `p1a1` = b+128, `p1a2` = -b, `p1a3` = 128-b; x/z are absolute coordinates, ctrl
is the run modifier, subop is the minimenu sub-op):

| Packet | Layout |
|---|---|
| OPLOC1 (7) | id p2a2, subop p1a3, x p2a2, z p2, ctrl p1a2 |
| OPLOC2 (55) | id p2a2, z p2, subop p1a1, x p2a2, ctrl p1a1 |
| OPLOC3 (58) | ctrl p1, x p2, z p2a3, id p2a1, subop p1a3 |
| OPLOC4 (3) | x p2, id p2, ctrl p1a2, z p2a1, subop p1a1 |
| OPLOC5 (66) | subop p1a3, ctrl p1a1, x p2a3, z p2a1, id p2a1 |
| OPOBJ1 (79) | id p2, subop p1a1, z p2a2, x p2, ctrl p1a1 |
| OPOBJ2 (24) | id p2a2, ctrl p1a2, x p2a3, subop p1, z p2a1 |
| OPOBJ3 (82) | x p2a2, id p2a1, ctrl p1a1, subop p1a1, z p2a2 |
| OPOBJ4 (34) | subop p1a1, ctrl p1a2, id p2a3, x p2, z p2a2 |
| OPOBJ5 (40) | id p2a2, ctrl p1a3, x p2a3, z p2a2, subop p1a2 |
| OPNPC1 (92) | index p2a3, ctrl p1, subop p1a1 |
| OPNPC2 (44) | index p2, subop p1, ctrl p1a1 |
| OPNPC3 (71) | ctrl p1, subop p1, index p2 |
| OPNPC4 (33) | subop p1a1, ctrl p1a3, index p2a3 |
| OPNPC5 (9) | index p2a2, ctrl p1a2, subop p1a1 |

The op senders were found by listing every call to the packet constructor `0x79cc90` (opcode
is the second argument): functions with five calls are the op1-5 families (OPPLAYER1-8 is
`0x623e7c`, OPWORLDENTITY1-5 `0x622f10`). OPLOC vs OPOBJ was told apart by OPLOC6/OPLOCT sharing
the loc helper `0x623b5c`. A loc/obj x/z swap would show as a wrong tile; OPLOC1 was checked live.

### Client→server, second pass (2026-10-16)

From a scan of every call to the packet constructor 0x79cc90 (w0 = opcode; about 9 senders are
in code Ghidra had not analysed, including the CS2 interpreter at ELF 0x4e82d0). Codecs:
`AndroidMoreDecoders.kt`; enum entries are tagged "sender read" or "unverified: reason".
Decoded live on 2026-10-16: 10, 13, 21, 37, 67, 98, 100 (EVENT_KEYBOARD's delta value looks
too large, so the g3Alt3 transform is unconfirmed).

| Opcode | Prot | Layout (as RSProt reads, wire order) |
|---|---|---|
| 0 / 41 / 60 / 94 | IGNORELIST_DEL / FRIENDLIST_DEL / FRIENDLIST_ADD / IGNORELIST_ADD | name |
| 4 | RESUME_P_COUNTDIALOG | count `g4` |
| 5 | OPPLAYERT | selectedObj `g2Alt1`, selectedCombinedId `g4Alt1`, index `g2Alt1`, ctrl `g1Alt3`, selectedSub `g2Alt3` (corrects the earlier OPWORLDENTITYT guess) |
| 10 | WINDOW_STATUS | as desktop |
| 13 | CONNECTION_TELEMETRY | 7 × `g2` as desktop (decoded without desktop's zero checks) |
| 14 | SEND_SNAPSHOT | as desktop |
| 15 | IF_BUTTONT | targetCombinedId `g4`, selectedSub `g2`, selectedCombinedId `g4`, targetSub `g2Alt2`, targetObj `g2Alt2`, selectedObj `g2Alt1` |
| 16 | EVENT_CAMERA_POSITION | angleX `g2Alt3`, angleY `g2Alt3` |
| 18 / 32 | EVENT_MOUSE_MOVE / EVENT_NATIVE_MOUSE_MOVE | as desktop |
| 19 | SEND_PING_REPLY | fps `g1Alt2`, value1 `g4Alt1`, value2 `g4`, gcPercentTime `g1` |
| 21 / 77 | EVENT_MOUSE_CLICK_V1 / V2 | V1 as desktop; V2 packed `g2`, y `g2Alt2`, x `g2`, code `g1` |
| 22 / 96 / 36 / 86 | RESUME_P_STRINGDIALOG / NAMEDIALOG / OBJDIALOG / LONGDIALOG | as desktop |
| 23 / 30 / 67 / 97 | EXIT_FREECAM / CLOSE_MODAL / MAP_BUILD_COMPLETE / IDLE | — |
| 29 | CLICKWORLDMAP | packed coord `g4Alt3` |
| 35 | TELEPORT | level `g1Alt2`, x `g2Alt3`, z `g2Alt2`, oculusSyncValue `g4Alt3` |
| 37 | EVENT_KEYBOARD | per key: key `g1Alt2`, delta `g3Alt3` |
| 39 | MOVE_MINIMAPCLICK | x `g2Alt3`, z `g2Alt2`, keyCombination `g1Alt3`, then the desktop tail (decoded as MoveGameClick) |
| 42 | BUG_REPORT | instructions, type `g1`, description |
| 43 | IF_BUTTOND | selectedCombinedId `g4Alt2`, targetSub `g2Alt2`, targetObj `g2Alt2`, targetCombinedId `g4Alt3`, selectedObj `g2Alt3`, selectedSub `g2Alt2` |
| 45 | SET_HEADING (unverified) | heading `g1Alt2` |
| 47 | SET_CHATFILTERSETTINGS | as desktop |
| 49 / 69 | OPNPC6 (verified live: Examine chicken -> id 1173) / OPWORLDENTITY6 (verified statically: the menu builder `0x633a1c` adds its entry, bound to sender `0x623550` with the world entity's type id, right after the entity's five OPWORLDENTITY1-5 entries, the same way it adds OPOBJ6 after OPOBJ1-5) | id `g2` / id `g2Alt3` |
| 51 / 61 / 88 | FRIENDCHAT_KICK / FRIENDCHAT_JOIN_LEAVE / FRIENDCHAT_SETRANK | name / name / rank `g1Alt1`, name |
| 56 / 72 | CLANCHANNEL_FULL_REQUEST / CLANSETTINGS_FULL_REQUEST | clanId `g1s` |
| 70 | IF_SUBOP | as desktop |
| 75 / 98 / 100 | DETECT_MODIFIED_CLIENT / MEMBERSHIP_PROMOTION_ELIGIBILITY / RSEVEN_STATUS | as desktop |
| 89 | EVENT_MOUSE_SCROLL | rotation `g2s` |
| 95 | IF_BUTTON | combinedId `g4` |
| 91, 17, 65, 53, 25, 26, 68, 101 | OPPLAYER1-8 | 1: index `g2Alt1` ctrl `g1Alt1`; 2: index `g2Alt3` ctrl `g1`; 3: index `g2Alt1` ctrl `g1Alt3`; 4: ctrl `g1` index `g2Alt3`; 5: index `g2` ctrl `g1Alt1`; 6: index `g2Alt3` ctrl `g1Alt3`; 7: index `g2` ctrl `g1Alt2`; 8: ctrl `g1Alt3` index `g2Alt2` |
| 78, 54, 11, 52, 59 | OPWORLDENTITY1-5 | 1: index `g2Alt3` ctrl `g1Alt2`; 2: index `g2` ctrl `g1Alt2`; 3: ctrl `g1Alt2` index `g2Alt1`; 4: ctrl `g1Alt3` index `g2`; 5: ctrl `g1Alt3` index `g2Alt1` |
| 46 | SOUND_JINGLEEND | jingle id `g4` (FUN_005694e4, only while client+0x2f58 > 0); called when the music module stops a jingle player (FUN_00790634 loops the active jingles and reports each id; FUN_0079a8e0) |
| 50 | MESSAGE_PRIVATE | name gjstr, Huffman message, as desktop (CS2 op 5009 = 0x1391 in FUN_005bdf14: CP1252 C string, then FUN_008668f0); decoder bound |
| 20 | AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL | clanId `g1`, memberIndex `g2`, muted `g1`, name (sender FUN_0053b714), as desktop; decoder bound |
| 28 / 57 | AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL / CLANCHANNEL_KICKUSER | both clanId `g1`, memberIndex `g2`, name, as desktop (guests only). 28's CS2 op (call 0x5ecfe8) pops clanId and memberIndex, the explicit-clan convention of 20; 57's (call 0x5ed1bc) pops only memberIndex and takes clanId from the active clan channel, i.e. activeclanchannel_kickuser. Decoders bound |
| 8 | IF_SCRIPT_TRIGGER | combinedId `g4Alt3`, obj `g2` (component +0xb38, which IF_SETOBJECT sets), sub `g2Alt1`, crc `g4Alt3` (the CS2 op's first int), then the args by the signature string, as desktop (ints as zigzag LEB128 varints, `gVarInt2s`). Sender 0x5e6af4, CS2 op 0x789/0xb71 |
| 84 | IF_CRMVIEW_OP | behaviour2 `g4Alt1`, behaviour1 `g4Alt1`, sub `g2Alt1`, serverTarget `g4Alt2`, behaviour3 `g4Alt1`, combinedId `g4Alt2` (sender 0x51fc20: behaviours = the 3-int array at +0x148 from FUN_006cf41c, target from FUN_006cf688, sub/combinedId from the component +8/+4) |

Third pass (2026-10-16): a raw scan of every `bl` to the packet constructor `0x79cc90` over the
whole text segment (capstone, not limited to Ghidra-analysed functions; the opcode is the `w0`
immediate, `wzr` = 0) found a sender for every opcode except 1, 12, 27, 62, 73, 76, 83, 85 and
87 (96 is built through a register). Those are tagged `// dead:` in the enum and stay `OP_n`: the
desktop rev-241 U prots are deprecated ("all usages go through ...T now"), and the unmapped
desktop prots are OPNPCU/OPPLAYERU/OPWORLDENTITYU/OPWORLDENTITYT (11 bytes: 1/62/76/85),
OPLOCU/OPOBJU (15 bytes: 73/83), REFLECTION_CHECK_REPLY, EVENT_APPLET_FOCUS (size 1, probably 87),
but nothing ties a specific opcode to a specific name. Named in this pass:

| Opcode | Prot | Layout |
|---|---|---|
| 6 | OPOBJ6 | id `g2Alt1`, z `g2Alt3`, x `g2Alt1` (sender `0x6211ac`; same struct offsets as the OPOBJ1-5 sender: +4 x, +8 z, +0xc id) |
| 80 | HISCORE_REQUEST | requestId `g1`, type `g1`, name `jstr`, as desktop (sender `0x64d850` writes the var-byte length itself, a per-request counter, the type, the name, and sets a pending flag) |
| 90 | UPDATE_PLAYER_MODEL_V2 | 26 bytes (sender `0x562a9c` = CS2 player-designer ops 300-326; payload writer `0x6e2248` starts with gender, then idkits as `p2` (value - 256, 0xffff = none)); no codec, RSProt 241 has none |

The earlier note that opcode 5 (`0x622bd4`) is "probably OPWORLDENTITYT" is wrong: its sender looks
up the target in the player container (client+0xc878) like OPPLAYER1-8 (`0x623e7c`), so 5 = OPPLAYERT.

## Display name

The chatbox refuses input ("You must set a name before you can chat") unless varbit 8119
`has_displayname_transmitter` (varp 1737 bit 31) is set; clientscripts 88, 107, 111, 125, 129,
223, 550, 681 and 7822 check it. The server sets it at login. It is unrelated to whether the Jagex
account has a character name: the name shown in game is the one the server sends in the
appearance block.

## Mobile gameframe notes

The logout panel is opened from the door icon in the top-left corner of `toplevel_osm` (the
debug overlay of libosrsrl covers it; `setprop debug.osrsrl.overlay 0` to hide the overlay).
Its "Tap here to logout" button arrives as IF_BUTTONX on interface 182, as on desktop.

## Reconnect

Reconnect uses the login protocol's `ReconnectOk` (same as desktop): the client presents the
previous session's XTEA seed, the server resets player/npc/world-entity info and replies with
the gpi init block. The test server keeps a dropped player for 60 s; `::drop` closes the
connection without logging out to exercise it.

## NPC info

New (low-resolution → high-resolution) NPCs are written in this order on Android: index (16),
dx (6 bits small / 8 bits large), dz, spawn-cycle flag (1) [+ size index (2) + value
(18/19/20/32 bits)], type size index (2) + type (12/14/17/24 bits), extended-info flag (1),
direction (3), jump (1). Desktop sends dz first and dx last, and jump/direction/spawn before the
type.

NPC extended info: flag bytes chain with `0x20` (second byte), `0x2000` (third) and `0x20000`
(fourth). Read order starts 0x2 transformation, 0x800000 (3 bytes), 0x200000 (1 byte), 0x80
(hits, count `-b`), 0x200 (1 byte `-b`), 0x1 sequence, 0x4 say, 0x1000, ...

| Block | Layout |
|---|---|
| transformation `0x2` | id `p2Alt2` (as desktop) |
| sequence `0x1` | id `p2Alt3`, delay `p1` |
| say `0x4` | `pjstr` (as desktop) |
| face `0x40` | flag byte `p1` (raw; desktop writes `p1Alt2`), then the face payload as for players |
| headbars `0x1000000` | count `p1Alt2`; per bar: type smart, endTime smart (0x7FFF = remove), startTime smart, startFill `p1Alt1`, endFill `p1` if endTime != 0 |
| hitmarks `0x40000` | count `p1`; per hit: type, value, delay, limit (smart1or2) |

Full NPC read order (flag tests in 0x569778): 0x2, 0x800000, 0x200000, 0x80 (spotanims), 0x200,
0x1, 0x4, 0x1000, 0x800, 0x1000000 (headbars), 0x10, 0x8, 0x100, 0x2000000, 0x400, 0x40,
0x40000 (hitmarks), 0x4000, 0x10000, 0x80000, 0x100000, 0x8000, 0x400000.

Other NPC blocks are not mapped; the Android writer drops them.

NPC adds near the view edge and runtime spawns (2026-10-16, device):
- The high-resolution count can drop by one right after an add at distance 15. That is an NPC at
  the edge stepping out of range; the client then drops the tail slot. Desktop gets the same
  bytes, and `NpcInfoViewEdgeTest` (RSProt desktop tests) checks that an add at exactly 15 tiles
  stays in high resolution.
- `::npc 300 goblin` spawns on the player's own tile. The goblin is added, placed and interactive
  at once (two "Attack Goblin" entries on a long-press right after a second spawn); it is just
  hidden inside the player model until the player steps away. The spawn clock is irrelevant: an
  A/B build without it behaved the same, so Android adds keep sending it.

## Local sign-in (no Jagex account)

With `LOCAL_AUTH=true` (server) and a client built with `LOCAL_AUTH=1 OPENRUNE_ENDPOINT=... tools/repack.sh`,
the test server replaces Jagex's sign-in:

- jav_config params 11, 20, 22 and 28 (auth.jagex.com, social.auth.jagex.com,
  auth.runescape.com, account.jagex.com) point at the server's HTTP base.
- The Java SDK (AppAuth) runs OpenID Connect against it: discovery, `oauth2/auth` (redirects
  straight back with a code), `oauth2/token` (unsigned ID token echoing the nonce).
- Native code then calls the game-session API at the param-11 base: `POST sessions`,
  `GET accounts` (one character, `LOCAL_DISPLAY_NAME`) and `POST tokens`. That token goes into
  the game login as auth type 7.
- Client patches (tools/patch_local_auth.py): cleartext allowed in network_security_config, and
  AppAuth's "only https connections" and "Issuer must be an https URL" checks removed.

In-app: "Sign in with Email / Username", close the privacy screen, then Play Now. The client
ID, scope and redirect URI are hard-coded in native code (Ghidra 0x59d638).

## Dialogue

`::talk` opens chat_left (231) in **chatbox:chatmodal = 162:568** (rev 241; it was 162:566 in
rev 239, whose symbols this server's ids come from. Component ids can shift between revisions).
Sequence: RUNCLIENTSCRIPT 2379 (reset background), IF_OPENSUB, IF_SETNPCHEAD, IF_SETANIM,
IF_SETTEXT name/text/continue, IF_SETEVENTS (pause button). Tapping continue sends client opcode
48 RESUME_PAUSEBUTTON (component int LE, sub LE); the server closes with IF_CLOSESUB.
After IF_CLOSESUB the server runs scripts 2158 and 216 (chat_build); without 216 the chat history stays hidden.

The journal side tab (629) is filled by the server: IF_OPENSUB of 712 (summary), 399 (quest list)
or 259 (diaries) into 629:43, plus varbit 8168; its subtab buttons arrive as IF_BUTTON 629:2-25.
Chatmenu (219, root 479x122) does not fit chatbox:chatmodal 162:568 (479x96): set varbit 10670
(chatmodal_unclamp) = 1 and run clientscript 113 so 113 sizes 162:568 to its parent; without it
the lower rows are clipped and taps resolve to the wrong option.

NPC "Attack" is hidden unless varp 1306 (attack option setting) is non-zero.

### Live checks 2026-10-16 (23:33-23:39, `::trace` on)

- 15 IF_BUTTONT verified: Use bronze axe (inventory slot 0) on tinderbox (slot 1) decoded as
  selectedSub 0 / selectedObj 1351, targetSub 1 / targetObj 590.
- 16 EVENT_CAMERA_POSITION verified after fixing the order from the sender (yaw first, then
  pitch): arrow keys (`input keyevent --longpress 21/19`; one-finger swipes only send mouse
  events) gave pitch 128..383 with yaw fixed and yaw 1608 -> 405 -> 1852 (wraps at 2048) with
  pitch fixed.
- 45 SET_HEADING: the sender sits in the minimap click handler (`0x68d438`) under a special
  minimap mode; a plain long-press on the minimap sends nothing. Still unverified.
  Hypothesis (2026-10-16, fifth server dev): the special mode is sailing, where the minimap sets
  the boat's heading. Neither server has sailing, so it can't be sent live yet.
- 46 SOUND_JINGLEEND: `::jingle 152` got no reply; the sender only fires with a positive volume
  (client+0x2f58), and the emulator's audio is probably off. Verified statically 2026-10-16: the
  music module calls the sender with each jingle player's id when it stops them (FUN_00790634).
- 11 CHAT_FILTER_SETTINGS_PRIVATECHAT: stores at client+0x33c4, between 78's public (+0x33c0,
  which gates overhead public chat) and trade (+0x33c8). Neither `::pmfilter 2` nor the control
  `::chatfilter 2 2` (78) changes the chat tab labels, so the labels need a script redraw.
  Verified statically 2026-10-16 through the CS2 chat_setfilter op (see the table).

