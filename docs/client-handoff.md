# Client work: handoff notes

> **Start here: docs/client-state.md** (current state, gotchas, next tasks). This file is history now; the last entry covers gens 41-46 and the first harness tasks (moved from client-state.md).

Status and workflow for whoever continues the client side (the injected library plus the
RuneLite plugin host). The test server in `server/` is developed in parallel by another
session; its notes are in `docs/android-protocol.md`.

## Where things stand (2026-10-14)

- The official OSRS Android APK (build 241.4) is repacked to load `libosrsrl.so`, and can be
  pointed at the test server (`OPENRUNE_ENDPOINT=…`). Target device: the Agent_Phone emulator (x86_64 API 37,
  arm64 translated).
- `libosrsrl.so` provides:
  - an ImGui overlay drawn from the `eglSwapBuffers` GOT hook;
  - a game-thread hook on `Client::doCycle` (vtable slot 5);
  - read-only game accessors keyed by GNU build id (`native/src/game.cpp`);
  - an opt-in, token-authenticated, read-only memory agent;
  - the embedded Java plugin host.
- The plugin host runs unmodified RuneLite plugins on ART. Working API: game state, ticks,
  varps and varbits, local player position, NPCs with spawn/despawn events. See
  `docs/plugin-host.md` for the table and gaps, and `docs/offsets.md` for every address.
- Verified live: `host/plugins/example` (Tick Info) logs correct ticks, positions, varbit
  8119 and the four courtyard NPCs.
- **Quest Helper (real Plugin Hub jar, 2026-10-15) starts on Android**: all quest helpers load,
  startUp completes (its Swing panel is built headless), subscribers run every tick. Not visible
  yet: no sidebar UI. Next gaps it hits at runtime: `Client.runScript` (quest state scripts,
  needs the CS2 VM entry point), `Client.macroExpand`, menu entries, widget writes.
  `re/hub/` holds the hub jar (`curl https://repo.runelite.net/plugins/jar/quest-helper_<hash>.jar`,
  hash from `.../plugins/manifest/1.13.1_full.js`); convert with `tools/dex-plugin.sh`.

- **2026-10-15 (second client session)**: `Client.runScript` + int/object stacks (the game's
  `runScriptEvent`; `Quest.getState` is right for the server's `::quest` states),
  `getItemDefinition` / `ItemManager.canonicalize`, `getNpcDefinition` / `NPC.getComposition`,
  scene tile heights + settings (`WorldView.getTileHeights/getTileSettings`, so
  `Perspective.getTileHeight` works), `macroExpand` (identity). A plugin **sidebar**: an "RL"
  button at the left edge opens an ImGui window listing the ClientToolbar buttons and the selected
  panel's headless Swing tree (labels, buttons, checkboxes, clickable panels); taps go to the
  Swing listeners on the game thread. Touches reach ImGui through a `Window.Callback` proxy
  installed on the game Activity (`Sidebar.java`); touches outside the sidebar go to the game.
  **Quest Helper is usable**: quest list and full quest details in the sidebar, starting a quest
  (button next to its name, then an assist level) shows QH's step overlay in game ("Demon Slayer:
  Talk to Aris..."), no runtime exceptions left in that flow. Also `getEnum`, `WorldView.getScene`
  (tiles without objects), RuneScapeProfileChanged on login (QH waits for it before loading its
  quest list), inert models/world map/hint arrows.
- **2026-10-15 (third client session, cut short by a wrap-up request)**: menu support written
  and compiled, **not yet run on the device**. Native: `game::read_menu()/menu_open()/
  menu_entry_for_path()` (game.cpp), MenuOpened (per-cycle diff of MenuManager+0x7c0) and
  MenuOptionClicked (menu model vtable slot 1 `0xa6d6c8` patched to `hook_menu_select` in
  client_thread.cpp; consume skips the game's select). JNI `Native.menuEntries/clickedMenuEntry/
  menuOpen/consumeMenuClick`; Java `AndroidMenu`/`AndroidMenuEntry`, `Client.getMenu/
  getMenuEntries/isMenuOpen/createMenuEntry` (plugin-created entries stay host-side, not yet
  inserted into the game's menu). Layouts are from static RE (see docs/offsets.md "Menus").
  The Quest Helper Cook's Assistant end-to-end test was **not done**: the test server container
  was re-downloading its cache (fresh volume) when the session ended, so login failed.
- **2026-10-15 (fourth client session)**: menus **verified on device**: MenuOpened, select hook on
  the client thread, game node order top-first (getMenuEntries reverses it), plain taps also go
  through select (node 0, the default action) so MenuOptionClicked fires for left-clicks too.
  Entry params decoded (docs/offsets.md "Menu entry"): NPC_*_OPTION/EXAMINE_NPC (identifier = NPC
  index), GAME_OBJECT_*/EXAMINE_OBJECT (loc id, scene x/y), WALK, CANCEL, CC_OP/WIDGET_TARGET
  (widget id, slot). Not seen yet: ground items, players (decode as UNKNOWN). Quest Helper's Cook's
  Assistant overlay follows the inventory (egg/flour/milk steps, requirement colours). Gaps it hits:
  `NPC.getInteracting`, `NPC.getConvexHull` (NpcStep highlight/arrow), no dialogue-option
  highlight. The quest start through dialogue is blocked server-side (chatmenu option hit offset,
  `::quest cook` ambiguous), reported. The server's RSA key is now baked into its image; build with
  `MODULUS=8555c56c...` (or whatever server/data/rsa.properties holds) if the repack can't log in.
- **Cook's Assistant end to end with Quest Helper (2026-10-15)**: started through the Cook's
  dialogue (after the server's chatmenu fix), varp 29 0 → 1 → 2; QH's steps followed the items
  (egg → wheat → milk → "Give the Cook..."), the Cook got QH's cyan highlight, and the overlay
  closed on "Quest complete". Same session added scene objects + ground items (all planes, spawn/
  despawn events per tick), NPC hulls (footprint boxes) and canvas helpers, `ModelOutlineRenderer`
  replacement, `WorldView.getTileHeight`, extended tile settings. Unverified: object highlights
  (QH ObjectStep), item highlights on the ground, QH's arrow over the NPC after the extended-
  settings fix. Next: Sheep Shearer (object highlights, item counts), then sidebar text input,
  minimap geometry, dialogue-option highlights (widget dynamic children).
- **2026-10-15 (client dev gen 3; hashes are post-rewrite)**: verified on device with 5123432:
  Fred the Farmer gets QH's highlight and (blinking) arrow; no NpcStep NPE after `::tele`;
  Sheep Shearer starts through Fred's dialogue; shearing works; QH's ground-item highlight
  (shears) and `Take` advance the steps. 9874572: `Widget.setTextColor` writes Widget +0x98
  (QH dialogue-option highlight, verified). 9453adc: sidebar text input (Android dialog;
  QH search verified) and a GenHeadless fix (PlainDocument no longer shadows
  AbstractDocument's listener list). be4b788: top-level world view id is 0 (API TOPLEVEL),
  minimap locations via 601:34, player hulls/defaults. 82144c4 + next: actor animation/
  orientation/interacting/overhead text from static RE, AnimationChanged/InteractingChanged/
  OverheadTextChanged, ground-item menu entries, scene overlays clipped out of 601:50/36/111.
  A 16-tile hull offset seen once after `::tele` did not reproduce (camera and WorldView frames
  checked equal with `setprop debug.osrsrl.camlog 1`).
- **Where gen-3 stopped (2026-10-15 23:41, last client commit before this note: dda217a)**:
  verified live: overhead text + OverheadTextChanged (`::npcsay`), orientations read plausible
  values (0/1024/1536), QH step `mapPoint` is set (so the minimap arrow should render; not
  checked visually). Not yet verified: animation (`::npcanim`), interacting, the overlay clip
  under 601:50/36/111, minimap arrow position (601:34, zoom 4), ground-item MenuAction decode.
  Sheep Shearer: the account is at Fred's with the quest started, inventory emptied and shears
  given (server `::empty`, `::give id n` exist now). Remaining: `::give 1737 20`, castle stairs
  (ObjectStep highlight), Spin at the wheel 14889 (3209,3212,1), climb down, hand in to Fred.
  Then: model-based hulls, sidebar combo boxes, player composition, NPC/Player API coverage.
- **2026-10-16 (client dev gen 4)**: **Sheep Shearer completed end to end with Quest Helper**
  (second full quest). QH ObjectStep highlights (box + arrow + minimap arrow) on the castle stairs
  56230/16672 and the wheel 14889, steps followed the items (wool → balls), overlay closed at the
  hand-in. Added: `getObjectDefinition` (LocType cache `0x62e5ec`, multiloc `getImpostor`),
  `getWidget(WidgetInfo)` (RuneLite's own `Perspective.localToMinimap` calls it; this was hiding
  QH's minimap arrow), `getMouseCanvasPosition` (-1,-1), object minimap locations, the open menu
  rectangle (`MenuManager+0x2a8` press point + measured layout) with overlays clipped around it
  (verified), actor pose animations and player composition/combat level (verified via
  `debug.osrsrl.selflog`), NPC multi-transforms and widget scroll/model/opacity getters
  (unverified). Debug hooks: `debug.osrsrl.where <locId>` (screen position of a loc, for adb
  taps), `widgetlog`, `selflog`. Romeo & Juliet started with QH (Romeo highlight, dialogue
  highlight, varp 144 → 10, Juliet's house stairs); stopped upstairs at "Talk to Juliet".
  Tips: tapping a wandering NPC often hits "Walk here"; long-press and pick the entry
  (MenuOpened logs list entries in RuneLite order, top entry last).
  Later in gen 4: sidebar combo boxes (Android picker + ItemListener events; GenHeadless JComboBox
  keeps items; unverified), `addChatMessage` through the game's `0x43a9c0` (verified),
  `getVarps`, varcs (`Client+0x417b68`; unverified), **model-based actor hulls** (entity vtable
  slot 0 `getModel` → posed Model vertices, rotated + projected in `Hulls.model`; verified on
  Juliet/Romeo; `setprop debug.osrsrl.modelhull 0` = boxes), despawn events kept for departed
  NPCs/players (fixes a stale NPC blocking QH's NpcStep after a teleport; unverified).
  **022f070 must be on the device before teleporting**: 4d60e14 crashed on `::tele` (getModel on a
  freed entity); 022f070 resolves the live entity at call time (unverified).
  R&J state: varp 144 = 20 (Juliet gave the message, "1 x Message"); next: give it to Romeo
  (Varrock Square), Father Lawrence (3254,3483), cadava berries (bush Pick-from, or `::give 753`),
  Apothecary (3195,3405, dialogue "Talk about Romeo & Juliet."), potion to Juliet (upstairs,
  long-press her; a door blocks the direct path), back to Romeo. QH quirk (not ours): in Juliet's
  room at stage 20 QH shows the stage-60 text (copy-paste in QH's helper).
  Next after R&J: object model hulls/click boxes (GameObject `+0x230` Renderable*, slot 0; AABB
  `0x71ae98`), the real viewport zoom (`renderer+0x630c60`) instead of the fitted scale,
  `getWeight`, `menuAction`, setters (setVarbit/setVarcIntValue), widget sprite ids.
- **2026-10-16 (client dev gen 5)**: **Romeo & Juliet completed end to end with Quest Helper**
  (third full quest): Message → Romeo, Father Lawrence, cadava bush Pick-from (23625), Apothecary,
  potion → Juliet, Romeo; chat "Quest complete" and the QH overlay closed. 022f070's `::tele` crash
  fix verified (many teleports). a4e3ae9: NPC/player spawn events are held back until the actor
  has a position (a new Npc sits at fine 0,0 for a cycle; QH's NpcStep checks the roam range at
  NpcSpawned time and dropped Romeo after a teleport), and spawn lookups re-read the snapshot when
  the Java cache was refreshed earlier in the cycle (the event used to arrive with a null NPC).
  Verified: Romeo highlighted again after teleporting away and back. Debug: `setprop
  debug.osrsrl.actorlog 1` also logs each NpcSpawned with local/world/base.
  `host/tools/menu_pick.py talk` long-presses QH's cyan highlight and taps the matching menu row
  (wandering NPCs, no more "Walk here" misses). getEnergy reads 10000; getWeight can't be checked
  against this server (it never sends UPDATE_RUNWEIGHT).
  Later in gen 5 (976174a..fdaa3b0): client-side setters through the script VM's own functions
  (setVarbit, varp write-back via queueChangedVarp, setVarcInt/StrValue, getVarbit composition;
  verified), scene-object click boxes/hulls from the real models (GameObject and wall verified,
  decorations unverified; offsets from static RE of the tile draw, docs/offsets.md "Scene object
  models"), playSoundEffect (unverified), live viewport reader (zoom 512 at 1920x1080; did NOT fit
  better than the fitted scale, cause open), ~25 Widget getters from the CC_SET handlers (verified),
  Widget setHidden/setText (unverified), and no GameTick while LOADING (fixes QH's empty quest list
  on OpenRune, verified there; getWeight verified on OpenRune = 9 after ::give 1127).
  Debug hooks added: `debug.osrsrl.set 'varbit|varp|varc|varcstr id v'`, `debug.osrsrl.wdump
  <group>`, `debug.osrsrl.where` now logs model position, `camlog` logs the viewport.
  Next: verify decoration hulls, setHidden/setText, playSoundEffect; Widget.getType (+0x28);
  why the real zoom (512) doesn't reproduce the fitted projection (camera distance/height?);
  menuAction (needs the menu builder's per-action std::function, or reuse of an existing entry);
  more Widget/Client coverage (tools/progress.py lists per-interface counts).
- **2026-10-16 (client dev gen 6)**: **second plugin: Ground Items** (core RuneLite, on by default on
  desktop). It uses API that Quest Helper doesn't: MenuEntryAdded and menu text edits, ground item
  timers, prices, notifications. Core plugins need no dex: `tools/dex-plugin.sh --core grounditems
  re/host/plugins/grounditems.jar` (just plugin-classes.txt), then push it like any plugin. Needed a
  same-name `Notifier` (javax.sound is missing). Verified on device: labels "Egg (3) (GE: 12 gp) (HA: 6 gp)"
  (prices = store value), MenuEntryAdded posted before MenuOpened, plugin edits to option/target
  written back into the game's open menu (orange names), picking a rewritten entry and plain taps
  still work, Widget.getType, setText/setHidden, wall/ground decoration models read. The renderer's
  camera equals ours, so the zoom 512 mismatch is a pure factor (see docs/offsets.md "Scene object
  models"). Static, unverified: definition params (`getIntValue/getStringValue` on item/NPC/object
  compositions), getMapRegions, isPrayerActive, getLocalDestinationLocation, idle ticks, playSoundEffect
  (ran, not heard). Debug hooks: `debug.osrsrl.set 'whide|wtext <group:child> <v>'`, `'sound <id> <vol>'`,
  `'iparam <item> <param>'`; `debug.osrsrl.where deco|ground` lists those within 5 tiles; `camlog`
  logs the scene camera and its trig. Coverage ranking: a small script over the client jar's core
  plugins (javap, Client/Player/... calls minus host overrides and API defaults) gave the order used
  this session; the remaining top items are getWorld, createScriptEventBuilder, getFriendsChatManager,
  getDBTableField, getClanChannel, getMessages, sidebar combo boxes and NPC multi-transforms (still unverified).
- **Gen 6, later (2026-10-16 02:30-03:15)**: verified on device: definition params (bronze axe
  param 14 = 5), getMapRegions, idle ticks, getMessages, widget setters + revalidate (setOriginalX
  relaid the "All" chat tab), ItemLayer.getHeight (Ground Items labels above the items). Added
  createScriptEventBuilder, widget field setters (position/size/modes/sprite/opacity/font/flags),
  NPC getId/getName = current multi-NPC transform (-1/null when hidden) and no hull for a hidden one.
  **OpenRune fixes:** OpenRune's cache has 65660 varps; the varp diff's 1 << 16 bound skipped every
  VarbitChanged there, so QH never advanced (d67379a, verified: live `::varp 144` moves QH's R&J
  steps). Quest status script 4029 is fine there (Sheep Shearer FINISHED at varp 179 = 21).
  **Open (priority 1 for the next client dev): QH highlights on the wrong tile on OpenRune.**
  Romeo (id 5037, NPC index 19884; OpenRune's NPC indices are large, the old server's tiny) reads fine
  x/y 6976,6080 = 3214,3423 from Npc +0x1fc/+0x200 while the game draws him ~5 tiles north at the
  fountain; QH's arrow/outline follow our coordinates. Model, base (WorldView +0x24/+0x28 matches
  ::mypos) and projection are fine. Leads: a stale entity (index list wv+0xd0 vs hash map
  wv+0x98), +0x1fc/+0x200 not being the drawn position for these NPCs (compare +0x268/+0x26c and the
  path arrays live with tools/agent.py), or a second, visible Romeo with another id. Static RE of
  NPC_INFO (`0x469778`, decomp `re/decomp/00569778.c` ~l.560-590): on add/teleport the game sets the
  path start tile (+0x3f0/+0x418) and fine +0x268/+0x26c from the local player's tile (Client
  +0xcb7c/+0xcb80) plus the packet deltas; otherwise it queues a walk step, and +0x1fc/+0x200 is the
  per-frame drawn position along the path. So +0x1fc is the right field for a live entity, and the
  entity we read is probably not the rendered one. Ruled out: the render world view (camlog's last
  field `render_wv_same` was 1 on OpenRune, render base 3160,3376 = ours). **Check first:** whether a
  second, visible Romeo with another NPC id stands at the fountain (long-press him: MenuOpened logs the
  NPC index/id), then index list (wv+0xd0) vs hash map (wv+0x98) for index 19884. Probe:
  `setprop debug.osrsrl.set 'npcmodel <id> 0'` (lists base-id matches with index, tile, model
  bounds, hull, feet). Other probes this session: `quest <QUEST> <varp>`, `iparam <item> <-1|param>`,
  `wmove <g:c> <dx>`, `whide`/`wtext`, `sound`, `debug.osrsrl.varlog 1` (logs VarbitChanged),
  `debug.osrsrl.eyeback D` (experimental projection eye offset, off by default).
  Pitfall: `debug.osrsrl.set` values persist across app restarts and re-run once per process (a
  leftover `wmove` hid the "All" tab after restarts); clear the props before freeing the device.
  Still unverified: sidebar combo boxes, NPC multi-transforms in game, createScriptEventBuilder,
  ground item timers/ownership, playSoundEffect audio. getWorld: the world id is jav_config
  param 12 (1 old server, 255 OpenRune); where the client stores it is not found yet.
- **2026-10-16 (client dev gen 7)**: **the OpenRune wrong-tile highlight is a camera zoom problem,
  not a wrong entity.** `setprop debug.osrsrl.drawlog 1` (41ee61f/6441dc8) hooks the Npc vtable slot 0
  (getModel) and logs, every 50 cycles, each WorldView hash-map node near the player with fine x/y,
  +0x268, path start, list membership and how often the renderer asked for its model (caller
  `0x72dbbc` = drawRenderable). Romeo is a single entity (index 19868 that session; indices change per
  login), in the map and the index list, drawn every frame; no duplicates or stale nodes. What differs:
  this OpenRune account's view is zoomed in a lot (player ~3x bigger on screen than on the old server's
  account), while every camera value we read is identical on both servers (Client camera 1136 behind /
  470 above the focal point, pitch 1024, viewport zoom 512, scene trig). So the GPU applies a zoom that
  none of our fields carry, and the fitted scale only matches the old account's zoom; highlights get
  pulled toward the screen centre (the player). Probe: gltrace (`setprop debug.osrsrl.gltrace 1`, read at
  startup) now scans glBufferSubData/BufferData/mapped-range uploads for the ViewTransforms UBO (std140:
  camera position @0, view @16, inverse view @80, projection @144, view-projection @208) and logs it
  every 5 s. Next: capture it on both servers, then project with the GPU's matrices (or derive camera +
  scale from them) instead of the fitted 285/720.
  getWorld = Client +0x3174, getWorldType = WorldType.fromMask(Client +0x2f64) (the CS2 map_world 3318 /
  worldflags 3324 handlers in `0x5eaf30`); verified 255 on OpenRune, 1 on the old server, flags 0x1.
  getDBTableField (44e3f49, unverified): `game::db_field` calls db_getfield (7502) in `0x5cd7f4(vm, op)`
  with row, (table << 12 | column << 4), tuple on the VM int stack (table = DBRowType `0x74f824` +0x30)
  and reads back ints/longs/strings in column-type order (DBTableType `0x6eb2c4`, column types
  `0x6ec320`; type descriptors at globals `0x1d8aff8` int, `0x1d8b000` long, `0x1d8b008` string). Test:
  `setprop debug.osrsrl.set 'db <row> <col> <tuple> [rows]'`. Also: null friends chat/clan getters,
  sprite override maps, empty instance template chunks.
  **Fixed later in gen 7:** the projection scale now comes from the game's viewport zoom
  (`Client+0x46d4` * viewport height / 334, viewport rect `+0x46e0..+0x46ec`; docs/offsets.md), which
  follows the zoom settings: QH's Romeo outline sits on his model on OpenRune (zoom 512) and on the old
  server (zoom 127). Also getVarps had the same 1 << 16 bound as the varp diff (empty on OpenRune; now
  1 << 20). The UBO notes below are kept for reference only.
  **(Superseded) next session:** with a device slot (old server, then OpenRune with the zoomed-in account),
  `setprop debug.osrsrl.gltrace 1`, restart, log in, and read the `gltrace:` lines (hook flags, which
  upload path carries the UBO, then 7 rows of the block). Map uCameraPosition onto our camera
  (6848/-825/5328-style local units), check the projection's P[5] * 1080/2 against the fitted 427 on the
  old server, then drive `getCameraFp*`, pitch/yaw and `getScale` from `Native.viewTransforms()`
  (44e3f49) and make the UBO hooks always on (scan only the found offset after the first hit). If no
  upload carries it, look for the CPU copy (search memory for the projection floats with tools/agent.py).
- **2026-10-16 (client dev gen 8)**, commits 35aa2a1..4c5846b, all on OpenRune (the default server):
  - Regression pass OK: Cook's Assistant end to end with QH (twice), Ground Items labels, isPrayerActive,
    sidebar combo boxes (QH Difficulty picker). The quest-start Yes/No tap works with and without QH's
    highlight after the server's chatmenu unclamp fix (153c8ce).
  - **Bug class to watch:** gen 7's getDBTableField called Ghidra addresses (ELF + 0x100000) and crashed the
    game; fixed (ELF `0x64f824/0x5eb2c4/0x5ec320/0x4cd7f4`), verified. Re/decomp file names are Ghidra
    addresses; always subtract 0x100000 before `g_base +`.
  - Verified: walk destination = map flag `*(Client+0x40e970)` (not `+0xecdc`), HitsplatApplied +
    getHealthRatio/Scale (actor `+0x330` hitmarks, `+0x388` head bars), getComponentTable,
    setGameState(LOADING) (game's reload `0x429900`), Actor.setOverheadText/Cycle, overlay clipping (an int
    overflow had kept the exclusion list empty forever; now panels + modal interfaces are clipped).
  - Plugins: **Opponent Information runs** (third plugin; needed okhttp 3.14.9/okio in host.jar and an
    OkHttpClient + `runelite.static.base` binding; NPCManager fetches max HP from static.runelite.net).
    **Boosts Information runs.** XP Drop starts but needs ScriptPreFired; Status Bars needed
    ItemStatChangesService (bound now, unverified). Extra jars live in `re/host/plugins-extra/` so
    `tools/plugin-test.sh` keeps the default set (QH, Ground Items, Opponent Info).
  - Friend/ignore containers (FriendSystem `*(Client+0x46c0)`, docs/offsets.md): list state reads 2,
    members unverified (no friends on the account; adding one needs the friends tab + keyboard).
  - `host/tools/rank_api.py [N]` ranks unimplemented API by core-plugin usage (skips API defaults).
    Top now: ClanChannel/FriendsChat members (managers are null), GE search setters, setDraggedOnWidget,
    getChatLineMap/ChatLineBuffer (Chat History, default-on), world hopper (createWorld/changeWorld),
    queueChangedSkill, setWidgetSelected, getCanvas, getSelectedSceneTile. `Client.getAccountHash` is called
    by the client jar but missing from runelite-api 1.13.1 (version skew; ignore).
  - QH note: its quest list refreshes on login; after `::quest cook reset` mid-session a finished quest may
    stay hidden until relog (QH behaviour, not ours).
  - Next: ScriptPreFired/ScriptPostFired (hook the script VM entry; unlocks XP Drop and many others), verify
    Status Bars, friends with a real friend, chat line buffers, InfoBox overlay (InfoBoxManager has no
    renderer; Boosts used its own overlay).
- **2026-10-16 (client dev gen 9)**, commits f63a715..:
  - **Inline arm64 hooks work under libnb** (`native/src/inline_hook.cpp`): patch the first 4
    instructions (must not be PC-relative; expected bytes are checked) with `ldr x16,#8; br x16`, from
    JNI_OnLoad before the target ever runs. Used for **ScriptPreFired/ScriptPostFired**
    (`script_hooks.cpp`: runScriptEvent = root; script lookup `0x3f5658` called from GOSUB = proc entry
    with args on the stacks; locals swap `0x4dfd5c` called from RETURN = proc exit). Listeners see the
    VM's live stacks (int stack edits written back). On by default; `setprop debug.osrsrl.scripts 0`
    off, `2` logs every script. 30 fps with everything delivered.
  - **Core plugin batch**: `## Core plugin status` table in docs/plugin-host.md (the orchestrator's
    progress.py parses it; keep its format: `| Plugin | Status | Notes |`, status runs/starts/throws/untested).
    `EXTRA="a b" tools/plugin-test.sh` pushes `re/host/plugins-extra/<name>.jar` (make them with
    `tools/dex-plugin.sh --core <pkg> re/host/plugins-extra/<pkg>.jar`). Always restore the default set
    (grounditems, opponentinfo, quest-helper) afterwards.
  - Host fixes from the batch: @PluginDependency installs the dependency's `getPublicModule()` (as
    RuneLite; XpTrackerService, ItemStatChangesService), config interface defaults
    (`setDefaultConfiguration`), Notification config as on/off, `runelite.title`/`runelite.ws`, protobuf-javalite +
    RuneLite http-api in host.jar, WidgetItemOverlay gets its OverlayManager + inventory/bank WidgetItems,
    Status Bars' viewport 161:73 → side panel 601:111, `getChatLineMap` (host-side), `getStructComposition`.
  - **Android 9 lacks Java 9+ APIs**: `host/tools/api_gaps.py` (dexdump.exe of host.jar vs the SDK's
    api-versions.xml) lists them; RuneLite's `Text` and `WildcardMatcher` are same-name replacements for that.
  - Later in gen 9 (up to c4b8ab3): NPC Indicators runs; getStructComposition verified (Attack Styles computes
    the style); VarbitChanged for unchanged server varps (changed-varp ring) + per-varbit events (verified, fps 30,
    index 23 ms); Player Indicators / Special Attack Counter start (protobuf + http-api). Table: 9 runs, 32 starts,
    0 throws.
  - Next (gen 10): verify Attack Styles' overlay below the mobile chatbox (ABOVE_CHATBOX_RIGHT remap,
    unverified; config group `attackIndicator`); orbs ARE group 160 as on desktop (`wdump all` shows
    160:10 '99' etc.; the old single-group dump stopped at the first missing child), so check why Regen
    Meter / Run Energy draw nothing; promote more `starts` rows by checking each plugin's visible feature
    (Ground/Object Markers need shift-menu entries: `isKeyPressed(SHIFT)` is always false; Timers & Buffs,
    Item Charges, Inventory Tags need items/config); the login varp burst can overflow the 32-entry ring
    (Attack Styles misses varp 43 = 0 at login); NPC hulls also draw over the left button column (add it to
    OverlayRenderer's opaque list); XP Drop's hideSkillIcons; ChatIconManager.loadRankIcons NPE (getModIcons
    null); Chat History removal from the game's chat; InfoBox placement (bottom right, by the tab stones).
  - Device-run recipe: config tweaks go in /sdcard/Android/data/com.jagex.oldscape.android/files/settings.properties
    (group.key=value; append while the app is stopped), `EXTRA=... tools/plugin-test.sh`, `setprop debug.osrsrl.overlay 0`
    for clean screenshots, probe plugin fields with `setprop debug.osrsrl.probe Plugin.field` (tag osrsrl-probe).
- **2026-10-16 (client dev gen 10)**, commits 1c137ef..:
  - **Plugin menu entries** (`createMenuEntry`) show as a strip under the game's long-press menu
    (ImGui, `plugin_menu_frame`); a tap runs `onClick` + `MenuOptionClicked` and closes the game menu
    (`game::menu_close`, the close branch of `0x599f68`). **Virtual Shift** toggle under the RL button
    (`isKeyPressed(KC_SHIFT)`), `getSelectedSceneTile`, touch-as-mouse (`getMouseCanvasPosition` = touch
    point while down + 3 s) with RuneLite's `TooltipOverlay`, login varp resync on ring overflow.
    Verified: Ground Markers, Object Markers, Attack Styles, Regeneration Meter (draws only below full
    HP/spec by default), Run Energy (orb tooltip). Table: 14 runs.
  - **Sign-in rule (orchestrator, hard):** on login screens tap only the signed-in "Play Now" button or,
    when signed out, "Sign in with Email / Username" (LOCAL_AUTH). Never tap "Sign in with Google" or touch
    any Google/Jagex account screen; screenshot and match the screen before every tap there and abort if it
    isn't the expected one. `tools/plugin-test.sh` now classifies a screenshot (Play Now vs chooser) and
    exits 2 instead of tapping when it isn't the Play Now screen. Background: blind retry taps signed the
    account out and the old fixed Play Now tap then opened Google sign-in (backed out with BACK).
  - "Either your account is still logged in": wait ~30 s after a force-stop before logging in again
    (a background `sleep 30`, screenshot to confirm the dialog, then "Try again" at 957,670).
  - **Performance rule:** anything Perspective calls (camera, viewport, canvas size, player position,
    tile heights) must be cached per cycle (`Host.cycleStamp`). Per-vertex JNI took the game to 0 fps
    with NPC Indicators on ~40 goblins (taps stopped working). `setprop debug.osrsrl.stacks 1` logs the
    game and main threads' Java stacks every 2 s (tag osrsrl-host) to find such hot spots.
  - Later in gen 10 (f0c4531..8b5745d): ChatMessageManager.queue → game chat (Examine runs),
    MessageNode.setValue writes Message +0xa0 (unverified: does the chatbox redraw?), plugin submenus,
    getAccountHash (OAuthApi; XP Tracker runs), StatChanged held until the local player exists (Agility
    NPE), user.home = files dir (Emojis cache), sprite-widget overlay clip, Combat Level and Prayer
    run, getWidgetSpriteCache/createSpritePixels (Poison; unverified). Table: 18 runs, 23 starts.
  - Next (gen 11): verify XP Drop hideSkillIcons (Poison now no longer throws), Interact Highlight (tap-attack missed moving goblins; long-press + Attack is more reliable), setValue redraw (Chat Notifications/Chat Filter;
    if the chatbox doesn't redraw, find the chat rebuild trigger, e.g. RuneLite's refreshChat equivalent);
    Woodcutting/Fishing/Mining (::item 1351 axe; the oak by the Lumbridge goblins), Interact Highlight,
    Idle Notifier; "hover" menu entries for Item Stats (getMenuEntries while no menu is open = entry under
    the touch point); Swing colour picker for "Pick" entries (Android colour dialog); OpenRune may send
    some examine text as GAMEMESSAGE (Examine logs a type mismatch; reported). Stale config value
    `boosts.notifyOnBoost=...Notification@...` from an old build reads as OFF.
- **2026-10-16 (client dev gen 11)**, commits bf46c0a..:
  - **Hover entry**: with no menu open, `getMenuEntries()` returns one entry for the inventory slot under
    the touch point (`AndroidMenu.hoverEntry`, per cycle) -> Item Stats tooltips. Hold a finger on the item
    (`input swipe x y x y 2500`) and screenshot within ~0.5 s, before the long-press menu opens.
  - **Colour picker**: same-name `RuneliteColorPicker`/`ColorPickerManager` (`host/src/net/runelite/client/
    ui/components/colorpicker/`) open `ColorPrompt` (Android dialog: hex, preview, 24 swatches). Works for
    menu "Pick" entries (Ground Markers, Inventory Tags); ConfigPanel colour buttons get mouse events now
    (sidebar buttons without ActionListeners), unverified there.
  - XP Drop `hideSkillIcons`: nulled child slots are re-hidden every cycle for 150 cycles (the drop script
    unhides them). Chat: the node's RuneLite-format message is applied after ChatMessage (Chat
    Notifications' `<colHIGHLIGHT>`), and `chatFilterCheck` is raised once per new message with host-made
    stacks (`AndroidClient.fakeStacks`); the chatbox shows rewritten text when it next rebuilds.
  - Widget Examine = identifier 10 / CC_OP_LOW_PRIORITY (Inventory Tags). TOP_LEFT overlays stack below the
    chatbox (unverified; Woodcutting's panel was under the chatbox before).
  - Verified: Item Stats, Interact Highlight, Chat Filter, Chat Notifications, Woodcutting, Fishing, Item
    Charges, Timers & Buffs, Agility, XP Drop icons, colour dialog. Table: 27 runs.
  - Config keys: `chatnotification.*` (no s). Append test keys with one `echo` per line (printf `\n` through
    adb shell is unreliable); the host rewrites settings.properties on any setConfiguration.
  - **Item icons**: `Client.createItemSprite` calls the game's own createItemSprite (`0x63ff9c`, docs/offsets.md;
    game thread, its LRU cache), so `ItemManager.getImage`/`getItemOutline` work (Inventory Tags outline,
    stamina InfoBox icon verified 2026-10-16, no crash, 30 fps). `scale` != 512 is ignored.
  - Table at the end of gen 11: 30 runs. Cannon: OpenRune doesn't set up cannons.
  - Next (gen 12): Fishing/Cooking counters after the OpenRune redeploy (river spot 1527 = Lure fly rod +
    feathers lvl 20 / Bait rod + bait lvl 25; sea spots at Draynor), Examine single price line (server line
    removed); ConfigPanel colour buttons in the sidebar (unverified); hover entries for equipment (387) and
    bank (12) slots; Chat Timestamps (the mobile chat already prints times; its `addTimestamp` callback isn't
    raised), Chat History, Logout Timer, Daily Task Indicator, Random Events, Implings (`::npc` takes a duration
    and name on OpenRune), Player Indicators / Special Attack Counter (need other players / spec weapon),
    Poison; then rank_api.py. Chat Filter's duplicate collapsing needs per-redraw callbacks (not emulated).
    OpenRune mining text was "pickaxe" (server fix requested); `::npc` takes `duration npcDebugName`.
- **2026-10-16 (client dev gen 12)**, commits e79bdb2..:
  - **Chat**: `chatMessageBuilding` is raised once per new message (Chat Timestamps puts its local time
    before the line, after the game's own UTC time). `Client.addChatMessage` returns a pending node that
    binds to the game's message when it arrives (postEvent=false honoured). `ChatLineBuffer.removeMessageNode`
    removes the line from the game's chat channel ring (`game::chat_remove_message`) and `refreshChat` bumps
    the chat cycle (`0x43aacc`); Chat History's "Clear history" removed the lines once but they came back on
    a later rebuild (the All tab probably reads another list: next lead is `0x3ef964`'s second map at
    `PTR_DAT_00b82728`).
  - **Idle logout**: `setIdleTimeout` writes the game's AFKTimeout setting (minutes, `Client+0x2278 -> +0x154c`,
    re-applied at login; the game saves it, so reset it to 5 after tests). Logout Timer verified.
  - **Sprites**: `SpriteManager.getSprite` returns the game's own sprites (`0x5d4074` sprite cache, frame 0);
    `Client.getSpriteOverrides` writes same-size overrides into the cached sprite and restores on remove
    (Poison's green heart verified). Sprite calls are guarded until game state >= 10 (a startup call crashed
    the game thread).
  - **Members**: items show "(Members)" with no ops until the login screen's world switcher selects world 255
    (`setWorld 0x432748` is the only path that sets the members byte `0x1221e90`); server fix pending.
    Cannon verified that way.
  - Hover entries now cover bank items (12:12), the inventory beside the bank (15:3) and worn equipment (387,
    child 1 item; unverified). BufferedImage(ColorModel, WritableRaster, ...) / copyData / getRaster in the shim.
  - Same-name `SessionManager` (no runelite.net login), player option arrays, headless `JSpinner.getEditor`.
  - Members flag: `game::sync_members_world` (per cycle) calls the game's setMembersWorld when Client+0x2f64 & 1
    and the members byte Client+0x2070 disagree (the jav_config start path skips setWorld). The Notifier posts with
    postEvent=false (no ChatMessage), which ended Cannon's re-notify loop.
  - Verified in gen 12: Chat Timestamps, Daily Task Indicator, Fishing/Cooking counters, Examine single price line,
    Logout Timer, Poison heart, Cannon, Loot Tracker, Hiscore, Mouse Tooltips (inventory + equipment hover).
    Table at the end of gen 12: 39 runs / 11 starts / 0 throws of 50.
  - Device tips: hold-to-hover screenshots need `adb exec-out screencap` ~0.35 s into a 2.5 s `input swipe`
    (the long-press menu opens at ~1 s); the logout panel is the door icon at (88,40) then (1490,962); the world
    switcher is (288,994), tap world 255 twice. The memory agent crashed the game once at startup under houdini
    (retry). OpenRune `::npc <duration> <gameval>` (e.g. `ii_impling_type_1`).
  - Next (gen 13): Special Attack Counter (dragon dagger spec after OpenRune e09bc3d) and Bank (`::openbank` with
    nothing open); Implings once OpenRune's spawned impling reaches the client; Notes (JTextArea shows as a
    checkbox: make editable JTextArea kind E and fire focusLost after the prompt, but check that the headless
    PlainDocument returns the text first or the note is wiped); Skill Calculator's icon-only skill picker
    (sidebar H nodes need a label: use the icon's sprite/tooltip); Chat History clear (lines return on the next
    rebuild: find the All tab's list); Poison infobox (copyData shim fix, unverified); Player Indicators via
    multi-client; Menu Entry Swapper needs the game's default tap action reordered (only text edits are written
    back today). host/tools/rank_api.py shows no API gap used by more than 3 core plugins.
- **2026-10-16 (client dev gen 13)**, commits 67113dc..c563943:
  - **Sidebar**: headless documents hold their text (GenHeadless: `AbstractDocument.__content`;
    `JTextComponent.get/setText` go through the document, so `Document.getText(0, getLength())` works) and text
    components start editable as in Swing. Editable non-JTextField text components are kind `A` (multi-line Android
    prompt, then FocusListeners' focusLost, where Notes saves). Icon-only clickables (MaterialTab, icon buttons
    without text/tooltip) are kind `I` "<icon id>:<selected>": the pixels go to the overlay once per image
    (`Native.sidebarIcon`) and draw as ImGui image buttons (GL texture per EGL context); small ones after a label sit
    on the label's row (QH's quest arrows). The headless `ImageIcon(Image)` now keeps its image.
  - **Menu Entry Swapper**: a plain tap selects node 0 without opening the menu. `hook_menu_select` raises
    `MenuTap` first (synchronous); the host reads the tapped menu once (`AndroidMenu.beginSession`, setMenuEntries
    reorders persist within the tap), posts MenuEntryAdded per entry + PostMenuSort, and if a plugin put another
    game entry on top calls `Native.redirectMenuTap(nativeIndex)`; the hook then runs the game's own select with that
    node. Verified: banker tap -> "Bank"; taps with nothing swapped unchanged (MES off and on). Not done: the
    long-press menu's order (needs the game's node vector reordered) and plugin-created entries on top.
  - **Chat History clear**: `game::chat_remove_message` also unlinks the message's node from the all-messages
    std::list at `0xaab610` ({prev, next, size}, push_front in the Message ctor `0x3f0a04`; Message +0 = node, node
    +0x10 = id), which the All tab walks via chat_getprevuid/nextuid (`0x3f061c`/`0x3f0694`, by-id map `0xaab628`).
    Message +0 is pointed at the list head so the channel add `0x3ef964` skips its own unlink/free when it reuses
    the ring slot (the node and the by-id entry leak, a few bytes per cleared line). Verified.
  - Verified in gen 13: Notes, Skill Calculator, Menu Entry Swapper, Chat History. Table: 43 runs / 8 starts /
    0 throws of 51.
  - Findings: OpenRune dragon dagger (100 -> 75) and DWH (100 -> 50) specs work. Special Attack Counter logs
    "Special attack used ... DRAGON_WARHAMMER" then "Timeout waiting for hitsplat": its onHitsplatApplied needs
    `Hitsplat.isMine()` (types DAMAGE_ME 16 / BLOCK_ME 12 ...), `actor == lastSpecTarget` (same NPC instance) and
    `hitsplatTick == client.getTickCount()`; check which fails with `setprop debug.osrsrl.actorlog 1` (logs
    HitsplatApplied with type). It also logged "Removing counters due to interact change" right after the spec.
  - Bank: `::openbank` works; script 505 does run on mobile (as a gosub, `setprop debug.osrsrl.scripts 2` before
    start logs every script) but `BankPlugin.prices` stays null, and its "Computing bank price" debug line never
    appears, so getWidgetContainerPrices(12:12, 95) returns early: check `getWidget(786444)`, `getItemContainer(95)`
    (client_thread inventories snapshot) and varbit 4150 at PreFired(505), then the title widget 12:3.
  - Runtime NPC spawns (`::npc 300 goblin`) still didn't show right after the reply after OpenRune 3cf4e2b; one
    showed ~1 min later after walking. NPC_INFO add-block layout checked statically (reader at Ghidra 0x569778,
    getBits `0x71430c` vaddr): index 16, dx/dz 6|8, spawn clock flag + 2-bit sel {18,19,20,32}, type 2-bit sel
    {12,14,17,24}, ext 1, direction 3, jump 1; matches the server encoder. The spawn clock is only an identity key.
  - Next (gen 14): Special Attack Counter hitsplat match; Bank title value; Poison infobox (`poison.showInfoboxes`);
    Implings once spawns show; MES on the long-press menu order; then widely used Plugin Hub plugins (add rows).
- **2026-10-16 (client dev gen 14)**, commits f843e68..bd077c0:
  - **Event order as on desktop** (f843e68): a cycle's events (varps, NPC/hitsplat changes, spawns) now come
    before that tick's GameTick, then ClientTick (native `after_cycle`; Host drains actor changes before
    GameTick). Special Attack Counter matches the hitsplat to `getTickCount()` and now counts (DWH on an imp:
    30% drain and count InfoBoxes). Rechecked: QH step change, XP Drop/Tracker, 30 fps.
  - **Live inventories on the game thread**: `Native.inventory` reads the game's map directly when called on
    the game thread (scripts like the bank's 505 run in the cycle inventory 95 arrives). Bank title shows the
    value ("The Bank of Gielinor (35.9K)").
  - **MES long-press order** (d25b10d): the game sorts a menu by ordering its node vector {EntryList*, entry
    index, children*} over an unsorted entry list; `game::menu_reorder` permutes those nodes after the host
    runs desktop's pre-open PostMenuSort (`AndroidMenu.presort`, isMenuOpen() false meanwhile). Long-press
    menus are drawn from the model itself (MenuManager +0x7c0 == +0x140); other shows rebuild the display
    with `0x50621c`. Verified: banker menu "Bank" first, each row runs what it shows; MES off unchanged.
  - **NPC_INFO probe** (f7fbed1, `setprop debug.osrsrl.npcprobe 1`, decode with `host/tools/npc_info_decode.py`).
    Findings: the runtime `::npc` goblin is added correctly (it spawns on the player's tile, hidden inside the
    player model; not a client bug). Server bug routed: its high-res count is sometimes one lower than the
    client's list right after an add at distance 15, so the client silently drops the last slot and the server
    re-adds it next tick. +0x1fc/+0x200 is only written once an NPC moves; NPCs that haven't moved since the
    add now report the add position +0x268/+0x26c (game.cpp read_npcs), so stationary/nameless NPCs (type
    6709 etc.) appear in the host's list as on desktop.
  - **OpenRune-Developer-Tools (MCP) in the host** (72967bd, 4830915, 11fe587, bd077c0): see docs/plugin-host.md
    "Developer tools (MCP)" (build, enable, security, 6-step procedure). Host pieces: loopback-only,
    token-authenticated `com.sun.net.httpserver` (off unless `debug.osrsrl.devtools=1`); ASM call-site rewrites
    in dex-plugin.sh (`host/gen/CompatRewrite.java` -> `net.runelite.host.compat.JdkCompat`: readAllBytes,
    transferTo, Package.getImplementationVersion's ART "0.0"); converted jars keep MANIFEST.MF; the plugin's
    self-updater is stubbed out of our build (`host/gen/StubMethods.java`) and the host deletes any jar it
    could have left; `DevInput` (AWT events -> Instrumentation touches/keys, LOGGED_IN only; virtual mouse),
    `DevMenu` (menuAction: `pick_at` 0x72b758 asks the next frame to ray-pick a point, then the long-press build
    0x58fa84 runs unshown, the entry goes through the game's select via `select_menu_node`, the model is
    discarded; hover menus the same way), `HostDrawManager` (one-shot glReadPixels in the swap hook,
    frame_capture.cpp). `.mcp.json` entry "osrs-devtools" -> tools/devtools-mcp.sh (stdio <-> HTTP over
    `adb shell -T nc`; Windows adb turns LF into CRLF, undone; token cached in ~/.cache/osrs-devtools).
    Verified: tools/list, get_client_state, type_chat, list_npcs, interact_npc + get_dialogue +
    continue_dialogue, click, walk_to, screenshot.
  - **URL_OPEN** (static): case 24 -> platform vtable +0xf0 -> bridge 0x98a214 (http/https/market only) ->
    Java `ApplicationContext.openBrowser`, which shows Jagex's "You are opening a link to an external site"
    dialog; OK starts ACTION_VIEW. Why the session drops right after is unverified (focus loss?).
  - Table: 45 runs / 6 starts / 0 throws of 51 (Special Attack Counter, Bank now run).
  - Next (gen 15): Plugin Hub rows (popular plugins without GPU/desktop-only APIs: Inventory Setups, clue
    helpers, ...; build/convert like devtools, `api_gaps.py` + the new CompatRewrite table for Java 9+ calls);
    devtools rough edges: interact_npc found no point for an NPC near the left canvas edge once (hull points
    clipped?), interact_object/item_action/widget_action/select_option/enter_input not tried yet, the
    `menu: select ... x 0 y 0` of unshown menus (MenuManager +0x2a8 isn't set by our builds; harmless so
    far); Poison infobox (verify); Implings (spawns are fine; the impling spawns on the player's tile);
    recheck the server's NPC count fix with the npcprobe.
- **2026-10-16 (client dev gen 15)**, commits 4e566fd..:
  - **Plugin Hub from source**: `host/plugins/hub/build.sh <name>` clones the commit runelite/plugin-hub pins
    (`--pin <name>` copies it into `host/plugins/hub/plugins.txt`), compiles with Lombok against our 1.13.1 jars,
    keeps the LICENSE and resources, stops on extra Maven deps (Loot Lookup: jsoup), and converts to
    `re/host/plugins-extra/hub-<name>.jar`. 18 pinned by install count (api.runelite.net
    `/runelite-1.13.1/pluginhub`), all BSD-2. `host/tools/rank_api.py N --jars re/hub-build/*.jar` lists the
    Client API they use that the host lacks; api_gaps.py on their dexdump the Java ones (CompatRewrite now
    maps the Java 9 Duration methods). Table: `## Plugin Hub status` in docs/plugin-host.md.
  - Host gaps closed for them: getDBTableRows (row scan of the DBRowType cache), getDBRowsByValue,
    **getDBTableField with desktop semantics** (k-th field of every tuple; db_getfieldcount 7503), getItemCount,
    getClientThread, getSprites/getIndexSprites, ConfigManager.getProfile/getConfigDescriptor, enum-like
    config classes (FontType), jopt-simple in host.jar, headless Swing builders (GroupLayout), JPopupMenu
    children, overlays only while LOGGED_IN, menu tile kept after the menu closes (plugin-strip entries),
    InteractingChanged for actors that arrive interacting (Random Events' Genie).
  - **host/build.sh now fails on any javac error**: a filtered GenHeadless error had shipped a partial host.jar
    (NoClassDefFoundError WorldMapDataBag at start).
  - URL_OPEN crash: server-side (the payload is ISAAC-encrypted per byte; fixed by the server dev). A/B with
    `debug.osrsrl.off=1` (installs nothing of ours) showed stock dropping the session where we crashed; per-cycle
    actor/inventory/menu reads now only run in LOADING/LOGGED_IN. `debug.osrsrl.crashlog=1` logs arm64
    pc/lr/regs of faults in the game lib or ours (chains to the previous handler; not yet exercised).
  - devtools: interact_npc prefers hull points on the canvas and off the UI columns (left-edge fix,
    unverified); select_option, continue_dialogue, item_action (needs the inventory tab open) used for Turael.
  - Verified in gen 15: Implings (Puro-Puro), Random Events, NPC Aggression Timer, Slayer (core); Better NPC
    Highlight, Monster HP %, Visual Metronome, Attack Timer Metronome, Shortest Path, Max Hit (hub). Tables at the
    end of gen 15: core 49 runs / 2 starts (Emojis, Player Indicators) / 0 throws of 51; hub 21 rows: 6 runs,
    13 starts, 1 throws (Charges Improved, compat fix for Collection.toArray(IntFunction) built, unverified),
    1 untested (Loot Lookup, jsoup).
  - Test keys: `randomevents.notifyAll` (not notifyAllEvents), `npcUnaggroArea.npcUnaggroAlwaysActive`,
    `BetterNpcHighlight.tileHighlight/tileNames`, `MonsterHP.npcShowAll`, `implings.showgourmet`. Slayer: tester
    has Turael's 17 Icefiends; `::varp 394 <n>` changes the count (InfoBox appears on a change, as on desktop).
    Shortest Path clears targets within 5 tiles at once; pick a far tile. Probe nested fields with
    `debug.osrsrl.probe Plugin.field.field` (e.g. `ShortestPathPlugin.debugState.search`).
  - api_gaps.py misses Java 9+ *interface default* methods (Collection.toArray(IntFunction) slipped through);
    CompatRewrite now rewrites INVOKEINTERFACE too; JdkCompat needs one overload per receiver interface.
  - Next (gen 16): verify Charges Improved, Customizable XP Drops' drops, Tree Despawn Timer, Improved Tile
    Indicators, Large Logout, Radius Markers; Banked Experience's JComboBox label (renderer: show the item's
    toString/renderer text, not the class name); Bank Heatmap's created button (long-press showed no menu: plugin
    widgets created with createChild are host-side only); more hub plugins by install count; Player Indicators via
    the desktop login; the impling name label height; devtools left-edge fix unverified.
- **2026-10-16 (client dev gen 16)**, commits 7855e48..:
  - **api_gaps.py** resolves calls through the api-versions.xml superclass/interface graph (inherited Java 9+
    default methods such as `ArrayList.toArray(IntFunction)` -> Collection, API 33), `--own <host dexdump>` for
    plugin jars, a HIDDEN table for members from non-SDK superclasses (StringBuilder.getChars). All 21 hub jars
    clean. **CompatRewrite now also runs over the client classes in host/build.sh** (Moonlight potion's toArray,
    Fairy Ring's static `Collectors.toUnmodifiableMap`). Lesson: the first version rewrote `toArray` on any
    `java/util/` owner, which caught `Stream.toArray` and broke Slayer's startUp (IncompatibleClassChangeError);
    it is limited to java.util / java.util.concurrent types now. Check the "compat rewrites: N" count (2 now).
  - Host: Sidebar combo boxes show the renderer's JLabel text (Banked Experience); `BeforeRender` posted each
    frame before overlays; GameTick and NpcSpawned wait for the local player (Tree Despawn Timer, Attack Timer
    NPEs at login); `ScriptEvent.getSource()` from ScriptEvent +0x40 (`Native.scriptEventSource`);
    `PluginPanel.onActivate/onDeactivate` for the selected sidebar panel (Time Tracking's panel was stale);
    TOP_RIGHT overlays stack below the minimap block (root child 36), which clipped UNDER_WIDGETS overlays
    (Customizable XP Drops' drops).
  - **devtools**: with no menu open the game's entry list holds a lone Cancel, which hid the hover menu
    (interact_object found only [Cancel]; fixed, verified with Chop down). Widget ops: a virtual mouse over an
    item slot gets the slot's ops as its hover menu (`AndroidMenu.slotMenu`), and CC_OP/WIDGET_* menuActions
    long-press the widget (`DevInput.longPress`) and pick the row from the opened menu (item_action Examine
    verified). Scene picks (`menuBuildAt`) only see the scene under the UI.
  - **createChild in the game's tree** (CC_CREATE path, docs/offsets.md) behind `setprop debug.osrsrl.createchild 1`,
    **off by default**. The first build crashed at login (make_shared's out pointer is x0, not x8: check the
    disassembly, not only Ghidra's signature, before calling a shared_ptr-returning function). After the fix:
    no crash/errors with Item Stats + Prayer + Skills Tab Progress Bars, tabs toggled 3x, bank 3x, teleport,
    30 fps. Not yet confirmed: that a created widget is visible (the tester's skills are all 99, so the bars
    don't show; use an account/skill below 99, e.g. `::varp`/xp commands, or a plugin that draws a created
    text/graphic), no duplicates after reopen, relog. deleteAllChildren runs CC_DELETEALL. Turn it on by
    default only after that pass.
  - Findings: Bank Heatmap's toggles hang off desktop's bank "Show menu" op, which the mobile bank lacks (no
    long-press menu on its buttons); Large Logout targets desktop's logout tab (mobile logout differs);
    Charges Improved's daily-reset NFE until the bank was opened once is the plugin's (empty storage_bank).
  - Verified in gen 16: Charges Improved starts (throws -> starts), Banked Experience combo, Tree Despawn
    Timer, Customizable XP Drops, Improved Tile Indicators (`improvedtileindicators.customDestinationTile=true`),
    Time Tracking (core, new row; Catherby with the server's farming varbits, `::growpatches`). All 21 hub
    plugins + defaults + BeforeRender at 30 fps. Tables: core 50 runs / 2 starts of 52; hub 9 runs / 11 starts /
    1 untested of 21 (Loot Lookup row completed so progress.py counts it).
  - Next (gen 17): the createChild visibility/duplicate/relog pass, then default on; Player Indicators with the
    desktop login (tools/multiclient/desktop_login.py, coordinate with the orchestrator); Emojis (needs the
    font renderer's `<img=>` sprite array; only the `<img=` builder at 0x43caa8 was found); impling name label
    height (desktop uses the model height for getLogicalHeight; host returns 200); Large Logout on mobile;
    devtools `walk_to` args (worldX/worldY threw a null getAsInt: check its schema); more hub plugins by
    install count.
- **2026-10-16 (client dev gen 17)**, commits c80d06f..:
  - **createChild in the game's tree is on by default** (`setprop debug.osrsrl.createchild 0` turns it off).
    Pass: `::reset` (OpenRune: all stats to 1; `::master` restores 99s, always run it after), a few hits on a
    rat; Skills Tab Progress Bars' red bars appear on Attack/Strength/Defence/Hitpoints; `debug.osrsrl.wdump 320`
    now prints `dyn N` per widget: 17 per tile (game 12 + plugin 5) before and after reopening the tab 3x;
    fresh login clean. Not done: an in-process relog (the mobile logout is the door in the left drawer; my taps
    only toggled the drawer).
  - **Actor.getLogicalHeight = posed model height** (`Native.actorHeight`, Model `+0x18` = max -y, checked
    256/256 live), cached 10 cycles per actor (`ActorHeights`). Fallback boxes use it too. Impling label not
    re-checked (Puro-Puro implings showed as id -1 multi-NPCs that run).
  - **Emojis**: the font's `<img=N>` table is a global shared_ptr (docs/offsets.md "Mod icons"). Host:
    `getModIcons` = placeholders for the game's icons (`Native.modIconSizes`), `syncModIcons` each cycle pushes
    plugin entries (`Native.setModIcons`), and `game::mod_icons_cycle` points the global obj at our own header
    (shallow copies of the game's sprites plus ours), re-installed when the game swaps its table. The game's
    ctrl and array are never touched.
  - devtools: `walk_to` takes `x`/`y`/`plane` (documented in docs/plugin-host.md).
  - Clue Scroll (core) starts: `re/host/plugins-extra/cluescrolls.jar`, `EXTRA=cluescrolls`.
  - Hub: built bank-tag-layouts, barrows-door-highlighter, spawnpredictor, better-teleport-menu (no API gaps;
    collection-log skipped: built against a newer API, `getStringStack`).
  - **Second half (2026-10-17)**, commits 90bf48f..2f5627c:
    - **Events in packet order**: the Message constructor `0x3f0a04` is inline-hooked
      (`game::install_message_hook`) to copy the inventories when each chat line is made. `poll_messages`
      posts the inventory changes made before each message, then the ChatMessage; `n_inventory` reads the
      state as of the event being posted (`client_thread::inventory_view`). An inventory that didn't exist
      yet reads as absent. Verified: Loot Tracker records a beginner casket (`::item 23245`, Open).
    - `MenuEntry.getItemId` for widget ops on items (Clue Scroll's Read); `Actor.getCanvas*Location` no
      longer adds getLogicalHeight (labels were twice too high); LOGGED_IN waits for the local player;
      `getPreferences`, `getUsername`; the headless shim covers javax.accessibility with a non-null
      `getAccessibleContext()`.
    - Emojis run in overhead text. Your own public chat line isn't in the chatbox: static RE (subagent)
      says the decoder always adds the line with the overhead text and the wire layout matches RSProt.
      Our admin account's chat is type 1 MODCHAT with `<img=1>`, so check whether the line is stored
      but not displayed (log the Message constructor's type).
    - Desktop second player: `tools/multiclient/desktop_login.py --seconds 360 --cheat 'tele 3223 3218'`
      (NO_TIMEOUT keepalive every 8 s; CLIENT_CHEAT op 84). Player Indicators verified with it.
    - Verified: Player Indicators, Item Identification (`itemidentification.showHerbs=true`), Charges
      Improved, Clue Scroll, Loot Tracker casket. Converted 12 more core plugins and 10 more hub plugins
      (rows in docs/plugin-host.md, mostly `starts`).
  - Next (gen 18): re-run Metronome, KPH Tracker, Hunter Rumours (fixed, not re-run); own public chat
    line; Ammo infobox (no infobox after wielding arrows); player menu items
    (`MenuManager.addPlayerMenuItem` / RUNELITE_PLAYER, Equipment Inspector); `Client.getItemContainers`
    (devtools list_inventories); in-process relog with createChild on; promote the hub `starts` rows by
    checking each feature; Large Logout on mobile.
- **2026-10-17 (client dev gen 18)**, commits b0780b9..:
  - **Own public chat line**: not a host bug. The admin account's chat is type 1 MODCHAT (`<img=1>tester`,
    text unchanged by filters, `setprop debug.osrsrl.chatlog 1` logs every message with its type) and the
    chatbox shows it. Right after one login the chatbox didn't redraw new lines until a chat tab was tapped;
    not seen again.
  - **Player menus**: player entries are target type 6 (`[3]` player index, op 1-based: Follow 3, Trade 4,
    Report 8) → `PLAYER_*_OPTION`, `getPlayer()`/`getActor()`. Plugin player options
    (`MenuManager.addPlayerMenuItem`, `getPlayerOptions()[4..7]` + RUNELITE_PLAYER) are added to the plugin
    strip once per player when a menu opens (`AndroidMenu.addPlayerOptions`, after the pre-open
    MenuEntryAdded pass because MenuManager sets them from MenuEntryAdded). `ClientToolbar.openPanel` opens
    the sidebar on that panel (`Native.sidebarSetOpen`); sidebar tabs wrap. Headless GroupLayout adds the
    components its groups name to the host container (GenHeadless). Verified with Equipment Inspector.
  - `Client.getItemContainers()` (`Native.inventoryIds`; not exercised yet). **getItemContainer returns the
    same instance while an inventory is unchanged**: Ammo/Cannon/Wintertodt/Drift Net compare
    `event.getItemContainer() != client.getItemContainer(id)` by identity (that was the Ammo bug).
  - **ItemManager.search** over the game's item defs (tradeable, unnoted, 5615 items; built after login in
    4096-id steps per cycle, ~160 ms total), getWikiPrice, getItemStats(int, boolean). ItemManager is now
    `@Singleton` with a static index (it had been built once per plugin, 4x). Chat Commands' !price works.
  - **Chatbox inputs**: same-name ChatboxTextInput / ChatboxTextMenuInput open Android dialogs
    (`AndroidPrompts`); ChatboxPanelManager.openTextInput/openTextMenuInput. Tooltips are cleared on the
    cycles overlays skip (Item Prices drew its tooltip twice).
  - Verified (rows promoted): Ammo, Chat Commands, Item Prices (with `itemprices.hideInventory=false`),
    Equipment Inspector, Hunter Rumours, Radius Markers, Wilderness Player Alarm (desktop player at
    Wilderness 2), Barrows Door Highlighter. In-process relog with createChild on (left door icon → "Tap
    here to logout" → Play Now): clean.
  - Findings: core Metronome only plays sounds (no audio on the host); Fairy Rings augments the travel-log
    panel the server doesn't open; Ess Pouch's Check regex wants "There are three essences in this pouch."
    (server wording gap, relayed); Quest List's button is shown (createChild under 399:0) but filtering
    needs the "questFilter" runelite_callback only RuneLite's patched CS2 has. Idea (orchestrator): post a
    synthetic `ScriptCallbackEvent("questFilter")` from our CS2 VM hooks around the questlist build script,
    with the stack the patched script would push; the same pattern could cover other runelite_callback-only
    plugins.
  - Gotchas: `plugin-test.sh` relaunches faster than the server drops the old session, so the app shows
    "account still logged in": wait ~30 s, tap Try again (958,668), screenshot, then Play Now. `pkill -f
    desktop_login.py` also kills your own shell (its command line matches). Mobile side tabs: inventory
    (1757,517), skills (1840,517), quests (1757,935), spellbook (1757,768), emotes (1843,600).
  - Verified at the end (f562faf): Ground Markers "Label Tile" opens the Android dialog and the label is
    drawn; Item Prices' tooltip is drawn once; the singleton index is built once (204 ms over 16 cycles)
    and !price works. **Quest List's magnifier (its createChild button under 399:0) does nothing on tap**:
    the tap resolves to Cancel because the game's menu has no op for a plugin-created widget whose
    action/onOp listener is a Java lambda (`setAction` + `setOnOpListener(Object[]{lambda})`). Next step for
    such buttons: write the action text into the created widget's op slot and, when its menu entry runs,
    call the plugin's Java listener instead of a CS2 script.
  - Next (gen 19): the created-widget op/listener path above (Quest List search, other plugin buttons);
    touch → MouseManager
    dispatch for Screen Markers (listeners consume → don't forward to the game; plain taps unchanged
    otherwise; no MouseManager dispatch exists yet); the questFilter callback idea; remaining `starts` rows (Rune Pouch and Plank Sack need
    server support; Virtual Levels needs XP above 99; Friend Notes needs a friends list).
- **2026-10-17 (client dev gen 19)**, commits 1237474..:
  - **Ops/listeners on plugin widgets** (`WidgetListeners`): `setAction/setName/setTargetVerb/clearActions`
    write the game widget (CC_SETOP via setOp `0x5dfae8`, opbase `+0x2c0`, verb `+0x330`). The game's menu
    builder `0x525c78` lists op i only if the event-flag mask (`0x460e74`: IF_SETEVENTS range, else
    `(w+0x2b0 >> 1) & 0x3ff`) has bit i or the widget has a CS2 onOp listener. So while a Java
    `setOnOpListener` is set, the non-empty ops' bits go into `+0x2b0`, and the widget's own bits come back
    when it is cleared. On MenuOptionClicked (if not consumed) the Java listener runs and the click is
    consumed (no script, no IF_BUTTON). Keyed by (component, index) + the game's Widget* (stale slots ignored).
  - **Touch → MouseManager** (`TouchMouse`, from the touch router): DOWN/MOVE/UP → pressed/dragged/released
    (+clicked when it didn't move). A gesture whose press is consumed is taken from the game; with no
    listeners nothing is built. Headless `SwingUtilities.is*MouseButton` follow the JDK (GenHeadless
    staticBody). OverlayRenderer honours `getPreferredLocation()` for any position and `getPreferredSize()`.
  - **runelite_callback emulation by proc boundary**: "questFilter" is raised when proc 3238 returns (dbrow
    from its args at GOSUB entry; a plugin's answer replaces the proc's result on the int stack).
    `Host.questFilter`. The same pattern works for any callback RuneLite put at a proc's start/end.
    Callbacks inside root scripts (fairyringFilterFavorite/Dbrow in 8080) need more (not done).
  - **Widget CS2 listeners** read from the game (`Native.widgetListener`, slots in docs/offsets.md "Widget
    listeners"): getOnVarTransmit/InvTransmit/Load/Key, getOnOp fallback. **getDBTableField is cached**: the
    db_getfield handler can't run while a script is on the VM, so a mid-script miss reads empty and is
    fetched next cycle (`wantDbField` prefetches).
  - Regression fixed: gen 18's ChatboxTextInput lacked RuneLite's `(manager, clientThread)` constructor, so
    ChatboxItemSearch → Bank Tags → Clue Scroll failed to start. Clue Scroll needs `EXTRA="cluescrolls
    loottracker banktags"` (banktags.jar only names the core class).
  - Verified: Quest List (search filters), Screen Markers (drag draws, game gets no touch), Fairy Rings
    (destination text under the dial; travel log 381 opens on Configure), Ess Pouch (Check wording on
    OpenRune 9a73dbe; Fill with pure essence counted). Plain taps/long-press/walk unchanged; a plain tap on
    the Cook opens his dialogue (it takes a couple of seconds: the player walks there first).
  - Gotchas: the Ess Pouch plugin only counts pure/daeyalt/guardian essence (test Fill with 7936, not
    1436). `server/tools/device.sh cmd` text goes into an open chatbox input (fairy ring search) instead of
    chat. `sleep` > ~20 s in the foreground is blocked; wait for "account still logged in" with a
    background until-loop or just retry Try again.
  - Next (gen 20): fairyring filter callbacks (hook inside script 8080 or reimplement in Java); remaining `starts` rows (Rune
    Pouch, Plank Sack: server support; Virtual Levels; Friend Notes).
- **2026-10-17 (client dev gen 20)**, commits 2f19007..22335d7:
  - **Opcode-level runelite_callback emulation** (`ScriptCallbacks`, `script_hooks` opcode wrapper): the
    CS2 handler for opcodes >= 100 (`0x4e06dc(vm, opcode, script, isLong)`, called from the interpreter
    loop for every opcode >= 100) is inline-hooked. Java watches (script, opcode) pairs
    (`Native.setOpWatches`); Host.onScript gets phase 3 before / 4 after the instruction with
    id = opcode << 16 | script. Script -1 = any script whose object-stack top starts with a prefix
    (filtered natively). Callbacks see the live stack tops (`raiseOnLive`); changed ints go back with
    setScriptIntStack, changed strings with `Native.setScriptString` (game string assign `0x3f1498`).
    Opcode numbers are RuneLite's `cache/.../script/Opcodes.java` (CC_SETTEXT 1112, IF_SETTEXT 2112,
    STAT_BASE 3306, ENUM 3408, STRING_INDEXOF_STRING 4121, DB_GETFIELD 7502). Emulated: skillTabBaseLevel
    (395 after stat_base), skillTabTotalLevel (any settext of "Total level: "; mobile never runs 396),
    friendsChatSetText/SetPosition (125, not exercised), fairyringFilterFavorite/Dbrow (8080: rewrites
    the string_indexof_string result; keyed by enum 1298's result / db_getfield 364608's row). RuneLite's
    patched scripts: sparse clone of runelite/runelite `runelite-client/src/main/scripts`.
  - Also: rectilinear `java.awt.geom.Area` (host/awt), WorldViewLoaded on scene base change (teleport
    rebuilds never show LOADING to the state poll; base 0,0 skipped), GeneralPath float iterator fix,
    `Widget.getVarTransmitTrigger` (+0x5c0 vector), headless JSpinner/SpinnerNumberModel keep values,
    chatbox inputs `@Inject` constructors, sidebar JPopupMenu picker, plugin executor logs task
    exceptions, `JOptionPane` → `ModalDialogs` (blocking Android dialogs), hub build `ALLOW_DEPS=1`
    fetches Maven deps (Loot Lookup / jsoup).
  - Verified (rows promoted): Rune Pouch, Plank Sack (OpenRune 3935505), Thrall Helper (`::varbit
    arceuus_resurrection_cooldown 1`), Banked Experience, Wilderness Multi Lines, Wasted Bank Space
    (non-BiS item 26229; BiS items are filtered by default), Virtual Levels (99/103 and total 2380 with
    `::setxp mining 20000000`), Fairy Rings log filter (custom tag), Puzzle Solver (server dev's check).
  - **Dialog timeout result**: JOptionPane blocks the game thread; holding Inventory Setups' name dialog
    ~35 s disconnected the client ("Unexpected server response"). Do not leave ModalDialogs blocking.
  - Next (gen 21):
    1. **EDT move** (agreed with the orchestrator): run SwingUtilities' queue, sidebar listener dispatch
       and the sidebar model build on a dedicated "AWT-EventQueue" thread instead of the game thread, so
       JOptionPane can block there. Needed: AndroidClient getters called off the game thread must
       marshal to it (invoke on the game thread and wait, ≤ 1 cycle) since native reads are game-thread
       only; plugins' Swing code (Banked XP's combo listener reads skills) relies on that. Until then,
       cap ModalDialogs (e.g. 15 s, timeout = Cancel) or refuse to block on the game thread.
    2. **Debug overlay tap pass-through**: the ImGui debug panel (left ~600 px, on by default) swallows
       taps over the game (it covered Puzzle Solver's left column). Pass touches through outside its
       real content, or let it collapse (native/src/overlay.cpp, touch router).
    3. Loot Lookup: headless `Component.getFontMetrics` returns null (`Util.truncateToFit`); return the
       shim FontMetrics for the component's font.
    4. Friend Notes: the mobile friends list stays "Loading friends list" (429:13) on OpenRune 245ed50;
       server dev is checking FRIENDLIST_LOADED / UPDATE_FRIENDLIST order. Then add a friend, "Add Note",
       and check the icon (callbacks in script 125 are ready).
    5. Inventory Setups: add a setup once dialogs don't block (EDT move).
    6. API stubs plugins hit (`host/tools/rank_api.py`): ClanChannel.findMember, World setters/createWorld,
       Client.queueChangedSkill (Virtual Levels calls it on config change; stub throws), GE search setters.
  - Gotchas: the friends side tab is (1843,768) and shows interface 707 (sub-tabs + "00:00 UTC");
    `setprop debug.osrsrl.scripts 2` + restart logs every root/gosub (find which scripts a UI runs);
    `setprop debug.osrsrl.wdump <group>` dumps a group's widgets to tag osrsrl-probe; fairy ring
    tags are `fairyrings.fairyringtags.<CODE>=...` in files/settings.properties (read at startup).
- **2026-10-17 (client dev gen 21)**, commits ec53b85..:
  - **EDT** (`host/src/net/runelite/host/Edt.java`, plugin-host.md "Sidebar"): "AWT-EventQueue-0" runs
    invokeLater work, sidebar taps (kind 1/2) and the model build; long-press plugin entries (kind 3) stay
    on the game thread. Turn-taking: the game thread hands its turn over at the end of `Sidebar.onCycle`
    when the EDT waits for one, and parks until it comes back; the EDT is the native client thread during
    its turn (`client_thread::set_delegate`, so `on_client_thread()` and `Native.isClientThread()` are true
    there). `ModalDialogs` on the EDT releases the turn while waiting; on the game thread it is capped at
    15 s (Cancel). The generated SwingUtilities queue is a LinkedBlockingQueue; `invokeAndWait` goes through
    `SwingUtilities.__invokeAndWait` (inline on the game thread / before cycles start, else wait ≤ 60 s).
  - Verified: Inventory Setups dialog held 70 s connected (setups listed), Banked XP combo on the EDT,
    Loot Lookup (Cow table), Friend Notes (note icon). One unexplained logout ~39 s into the first dialog
    (SSO resultCode 1500), not reproduced in two 60–70 s holds; asked the orchestrator about a server
    restart then.
  - Headless Swing: `Component.getFontMetrics` (shim metrics), `Toolkit.getDefaultToolkit()` instance
    (Toolkit's abstract methods get bodies), CompoundBorder/MatteBorder keep their parts,
    BorderFactory.createCompoundBorder/createMatteBorder build them.
  - API: `queueChangedSkill` (changed-skills ring, offsets.md; unverified), `createWorld` (AndroidWorld
    record), `changeWorld` (logs only). Not done: `ClanChannel.findMember` (getClanChannel is null since
    the host reads no clan data; a real channel needs the game's clan structures), GE search setters,
    `setDraggedOnWidget`.
  - Debug overlay: compact by default (`debug.osrsrl.overlay` 0 off / 1 compact / 2 full). It never
    captured touches (NoInputs, not in `overlay_touch` rects); the "left ~600 px" report matches the RL
    sidebar's width when open (630 px). unverified: no tap-through test.
  - Friends list (orchestrator's bug): `native/src/friends_probe.cpp`, `setprop debug.osrsrl.friendprobe 1`
    + restart. With the server's resend on, login showed LOADED 0->1 (cycle 422), UPDATE_FRIENDLIST len 29
    1->2 (cycle 424), resend 2->1->2 (cycle 547): no drop on our side in that run. Next: run it with
    `::socialresend off` (OpenRune a114f17), relog, then `::social update`.
  - Untested in the last build (committed): client-thread tasks wait for LOGIN_SCREEN (Inventory Setups'
    startUp item-name NPE); chatbox prompt titles stripped of `<br>/<col>` tags.
  - Docs: Quest Helper row moved to the hub table; Hunter Rumours runs -> starts. progress.py: core
    63/65, hub 23/32.
  - Next (gen 22): friend-list probe run (above); verify the two untested changes; Virtual Levels
    `queueChangedSkill` (toggle its config while the skills tab is open); Inventory Setups highlighting
    (then promote); the Friend Notes name shift/world-column clip; GE search setters (banktags, GE,
    inventory-setups, quest-helper call them).
  - Gotchas: friends list is the smiley side tab (1843,853), not (1843,768) (that is chat-channel/grouping);
    an app restart right after a session often shows "account still logged in": wait ~20 s, Try again.
- **2026-10-17 (client dev gen 21, continued)**, commits fc7d962..4e88ffc (progress: core 67/71, hub 25/32):
  - **Entity Hider / renderable draw listeners** (`native/src/draw_hide.cpp`): the host asks Hooks'
    listeners about every NPC/player each cycle (`Host.applyDrawListeners`); rejected actors get an empty
    model from their vtable slot 0 (getModel) when `drawRenderable` asks (return addresses 0x72dbbc /
    0x72e6a4), which skips draw and picking. NPC hiding verified; player hiding not isolated (the desktop
    player was never in view in the retry; `draw_hide: N npc and M player pointers resolved` logs it).
  - Verified: KPH Tracker (Giant Mole duration + kills/hr), Kitten Tracker (grow-up timer), Mouse
    Highlight (inventory tooltip), Virtual Levels `queueChangedSkill` (2376 <-> 2380 live), Friend Notes
    clean prompt titles, Inventory Setups startup error gone (`getItemDefinition(-1)` = default "null"
    definition). Hunter and Smelting promoted from the server dev's device checks.
  - Test hook: `setprop debug.osrsrl.set "config <group> <key>=<value>"` (ConfigChanged, like the desktop
    config panel; colours as `#AARRGGBB`).
  - Rows added for the six converted core jars without rows (Barrows, Entity Hider, Herbiboar, Minimap,
    Mouse Highlight, Smelting).
  - BOTTOM_LEFT overlays start right of the HUD's bottom-left button (8.5% of the canvas width;
    unverified: the kitten overlay wasn't showing in the last check).
  - API: GE search setters write OC_FIND's results (offsets.md "GE item search results"; no effect
    without a geSearch callback), `getMapDots` placeholders + `applyMapDots` -> the game's minimap dot
    styles (offsets.md "Minimap dot styles"). **Open**: the Minimap plugin never replaced a dot on the
    device (no "map dot ... replaced" log after `npcColor` changes, no exception seen); check whether its
    startUp/replaceMapDots runs (add a log in `getMapDots`), then whether `Native.minimapDot` recolours.
  - Friends list loads normally with the server's resend off (OpenRune 424fc37; "desktop" listed).
  - Gotchas: The emulator then sometimes crashed the app at startup inside libhoudini (render thread, right after
    the overlay initialises); relaunch. `debug.osrsrl.set` values persist across restarts (clear it).
  - Next: the Minimap open item; player hiding with the desktop player in view (`hideOthers`, it is not
    a friend for the host since `Player.isFriend` is false); remaining `starts` rows (Bank Heatmap, Bank Tag
    Layouts, Spawn Predictor, Better Teleport Menu, Large Logout, Hunter Rumours, Inventory Setups
    highlighting, Metronome, Barrows, Herbiboar); scene hover entries for Mouse Highlight.
- **2026-10-17 (client dev gen 22)**, commits 17030de..:
  - **Minimap runs**: the plugin's keys are `minimap.item/npc/player/friend/team/clan/clanchat` (gen 21 used
    a wrong key). The mobile minimap orders its dot categories differently: items 5, NPCs 6, players 4,
    friend 0, team 1, friends chat 2, clan 3 (from the dot draw `0x58cab4`'s callers; the game's default
    colour per category matches desktop's role). **Crash lesson:** setDotStyle before the minimap's base
    dot exists (`*(mm+0x6610)`, null before login) faulted at 0x88 and killed the game at startup; the
    native side now refuses until it exists and Java applies only while LOGGED_IN.
  - **Scene rebuilds now post LOADING**: teleports/region crossings rebuild within one cycle, so the state
    poll never saw LOADING. On a scene-base change while logged in the host posts GameStateChanged(LOADING)
    (getGameState reads LOADING meanwhile), WorldViewLoaded, GameStateChanged(LOGGED_IN), as desktop.
    getMapRegions answers from LOADING on. Herbiboar's area check works after a plain teleport.
  - `Player.isFriend` from the friends list (Entity Hider hideFriends verified with the desktop player);
    `getScriptActiveWidget/DotWidget` (vm +0xded8/+0xdee8, unverified); `Widget.getFont().getTextWidth`
    through the game's PARAWIDTH measure (verified values); `getEnvironment` = 1 (not live);
    drag/selection setters, empty projectiles; playSoundEffect reports whether the game queued it.
  - **NetGuard** (`host/src/net/runelite/host/NetGuard.java`): the injected OkHttpClient refuses every
    non-GET/HEAD request and any `xp/update` path; read-only GETs pass. `debug.osrsrl.netwrite 1` allows all,
    `netlog 1` logs requests (tag osrsrl-net). Audit: Loot Tracker's upload threw at the getEnvironment stub
    before 5e04bdf; Chat Commands' submits need ChatInput (never posted); XP Tracker's `xp/update?username=`
    GET on logout (> 10k XP change) could have gone out in earlier relog sessions. Probe:
    `debug.osrsrl.set "net get|post|xp"` (post/xp go to the local sidecar, never RuneLite).
  - Sidebar icon nodes carry the background of the icon or its slot panel (`I` text `<id>:<sel>:<argb>`;
    the headless `getBackground()` is BLACK when unset, so only set backgrounds count, via the generated
    `__BackgroundSet`), and icon pixels are re-sent when they change (item images load after first use;
    replaced textures are deleted on their render thread). Inventory Setups highlighting verified.
    TOP_LEFT overlays start right of the RL/Shift toggle (verified at Barrows).
  - Second slot (09:14-09:29): NetGuard verified (`net get 1` -> HTTP 200, `net post 1` and `net xp 1`
    blocked; the probe needs three words). Metronome promoted (track frames advance while the tick is
    queued). Hunter Rumours promoted on the server dev's device run.
  - Verified: Minimap, Entity Hider players, Kitten BOTTOM_LEFT, Herbiboar, Barrows (client-side varbit),
    Large Logout (mobile logout is interface 182 with desktop's layout), NPC dialogue arrives as ChatMessage
    DIALOG "Name|text". Tables at the end of gen 22: core 73/82, hub 28/32, API 534/866.
  - Server gaps relayed: follower varp 447 is 0 after a relog (Kitten Tracker expects (npc id << 16) |
    index); games necklace Rub has no teleport menu (Better Teleport Menu untestable); Hunter Rumours needs
    the guild dialogue or the whistle message.
  - Third slot (09:45-10:01): **widget Java onOp listeners no longer swallow the game's ops**. Better Teleport
    Menu sets an empty listener on interface 187's options; WidgetListeners consumed every click on such a
    widget, so "Continue" never sent RESUME_PAUSEBUTTON. Now only ops a plugin wrote with setAction are
    consumed (`WidgetListeners.notePluginOp`); verified Rub → Burthorpe with BTM loaded. Ten more core
    plugins converted (Diary Requirements, Inventory Viewer, Runecraft, Boss Timers, DPS Counter, Friend List,
    Spellbook, Grand Exchange, Music, Instance Map): all start; Runecraft and Inventory Viewer run. The mobile
    bank (12) mirrors desktop's component ids; a tap on the bank wrench is CC_OP "Show menu" (Bank Heatmap's
    hook: show plugin entries created during a tap, next step). BOTTOM_RIGHT overlays sit under the tab
    stones (Inventory Viewer): start them left of the column. Instances aren't modelled (isInstance false;
    Spawn Predictor, Instance Map); `getArray` needs the frame's object locals (Spellbook also needs its
    runelite_callback).
  - Slot 4 (10:13-10:19) verified 0ea1a4d: Inventory Viewer left of the tab stones; the wrench tap shows Bank
    Heatmap's strip and its "Toggle GE Heatmap" runs, but no heatmap colours appeared (open). Strip window
    raised to 12 s.
  - After slot 3 (0ea1a4d): BOTTOM_RIGHT overlays start
    left of the tab stones (root child 50) and the open side panel (111); a plain tap on a single-op widget
    (one game entry besides Cancel/Walk) shows the entries plugins created during its MenuEntryAdded pass
    as the plugin strip at the tap point for 6 s (`AndroidMenu.publishTapEntries`; Bank Heatmap's toggles
    on the bank wrench 12:106).
  - **Next (gen 23)**:
    1. Bank Tags / Bank Tag Layouts drag feasibility: the mobile bank mirrors desktop's interface 12 ids;
       check Bank Tags' tag-tab column (create a tab, add an item via its "Edit-tags" strip entry, open it),
       then how the mobile bank drags items (WidgetDrag, setDraggedOnWidget/getDraggedWidget are host-side
       only today) before attempting Layouts.
    2. Bank Heatmap: after "Toggle GE Heatmap" (strip verified) no colours are drawn on bank items; check
       HeatmapItemOverlay (WidgetItemOverlay on 12:12 items, getWidget parent ids) and the prices it uses.
    3. Feature checks for the 8 new `starts` core rows (Boss Timers/DPS Counter on the Giant Mole, Friend
       List with the desktop friend, Diary Requirements text widths, Music, GE, Spellbook, Instance Map).
    4. API: instances (isInstance/getInstanceTemplateChunks/instanced getMapRegions; Spawn Predictor, Instance
       Map), `getArray` via the frame's object locals plus Spellbook's runelite_callback, GE offers from the
       game (UPDATE_GE_OFFERS), clan/friends-chat data (ClanChannel.findMember).
  - Gotchas: `debug.osrsrl.set` may hold another agent's leftover ("reload 0 0"): check `getprop` before
    launching. The varp probe sets before it logs the old value (`set varp N v` prints "before").
- **2026-10-17 (client dev gen 23)**, commits 2e66c74..f3dda4c (progress at start: core 73/82, hub 28/32):
  - **Bank Heatmap runs**: the host listed bank WidgetItems from 12:13; desktop's item layer is 12:12
    (Bankmain.ITEMS). WidgetItemOverlays now get only the items under their draw hooks (showOnBank = 12:12,
    showOnInventory = whole groups incl. bankside 15:3), as desktop.
  - **Instances verified** (GWD Bandos room: `::varbit godwars_counter_bandos 40`, `::tele 2862 5354 2`, Open
    the Big door 26503): `*(Client+0x4868)+0x54dd` flag, `+0x38` [4][13][13] template zones (offsets.md
    "Instances"); isInstance/getInstanceTemplateChunks/instanced getMapRegions. Probe `set 'inst log 1'`.
  - **getArray + spellbookSort verified**: script int arrays are frame object locals (offsets.md "Script arrays");
    the RETURN's caller frame is x0 of the locals swap (`script_hooks::return_frame`). Spellbook's callback is
    raised when proc 2621 returns to 2611 (log "spellbookSort: enum 1982 count 69"). Hiding not exercised.
  - **GE offers** read from the game (Client+0x4185c0 slot objects, GrandExchangeOfferChanged per change);
    unverified (OpenRune has no GE).
  - **isDead** = head bar ratio 0 (Boss Timers creates its Mole timer, DPS Counter "Boss has died!"). Boss Timers
    stays `starts` until its InfoBox is seen on screen (screenshot right after the kill).
  - **Bank Tags runs** (basics; drag/Layouts out of scope): mobile bank scripts are desktop's (274/276/277/279
    per item/505/840). Pieces: bankSearchFilter at proc 279's return (Host.bankSearchFilter; single-slot stack
    access `Native.scriptIntTop/scriptStringTop` — full stack copies per item had dropped the game to 3-5 fps);
    `CustomSpriteWidgets` draws graphic widgets with plugin sprite ids (negative, from getSpriteOverrides) on the
    overlay canvas; plugins see 12:9 shifted below the tab bar 12:10 (`AndroidWidget.bankColumnShift`, live
    geometry; applies to 12:9's height and its children's y); a desktop-like "Examine" MenuEntryAdded is posted
    (not shown) for mobile bank item menus, which have none (`AndroidMenu.postMissingExamine`);
    `Widget.setChildren` truncation via the game's vector resize `0x40a25c`; `setItemId/setItemQuantity`.
  - Widgets of unmounted groups are hidden (closed interfaces stay loaded on mobile; the heatmap hint stayed drawn).
  - Install recipe now: `OPENRUNE=1 SERVER=10.0.2.2:8080 GAME_PORT=43594 LOCAL_AUTH=1 tools/repack.sh --install`
    (the OPENRUNE defaults 8081/40255 no longer connect; relayed to the server dev, who owns repack.sh).
    `server/tools/device.sh` is denied to the client agent by the permission classifier; type chat commands with
    adb (`input tap 90 220` for the keyboard if needed, `input text '::tele%s...'`, `keyevent 66`).
  - Bank booth recipe: `::tele 3208 3220 2`, long-press (1120,470), "Bank Bank booth" (1100,562); bank wrench
    (295,343) = settings + Heatmap strip; search (843,962); test tags via `set 'config banktags item_<id>=<tag>'`.
    Clean up `banktags.tagtabs/tab/icon_<tag>/item_<id>` in settings.properties afterwards (app stopped).
  - **Next (gen 24)**: Boss Timers InfoBox screenshot; Spellbook hide (reorder mode); Instance Map's
    `drawInstanceMap`; getSearchingTagTab/bankBuildTab (277/276 callbacks; cosmetic); drag (WidgetDrag,
    getDraggedWidget/getDraggedOnWidget from the mobile drag state) if Layouts/Spellbook reorder are funded;
    clan/friends-chat data (ClanChannel.findMember).
- **2026-10-17 (client dev gen 24)**, commits c7d9812..543287e (progress at start: core 76/82, hub 29/32, API 541/866).
  Addresses below are ELF; Ghidra and re/decomp names are +0x100000.
  - **Plan for gen 25 (funded by the orchestrator): Bank Tag Layouts**, in this order:
    1. Java onDragComplete listeners: store `setOnDragCompleteListener` per widget (as WidgetListeners does for onOp)
       and run them on the release cycle of the native WidgetDrag event (touch state 4) with source = dragged and
       target = dragged-on, before the game's swap (LayoutManager's handler calls setDraggedOnWidget(null)).
    2. On the device: check that bank items (12:12 children) keep the game's own onDrag (+0x4d0, `Native.widgetListener`),
       so `setOnDragListener({284, ...})` can stay a no-op.
    3. Skip setSubOp (potion-storage "Doses") for now.
    4. Layout drawing at ScriptPreFired 505 on the device: `getChild(pos)` past the item count, bankColumnShift for
       12:12 children, scroll height; tab separators stay (bankBuildTab not emulatable, cosmetic).
    5. Enabling a layout on a tag through the tag-tab menu entries (plugin strip).
  - **Runs now:** Boss Timers (mole InfoBox "0:08" top-left right after a Giant Mole kill, screenshot), Spellbook
    (hide/unhide and drag reorder), Grand Exchange (server dev's run on OpenRune's GE: offer events and the
    sidebar panel; bb92e28's reader verified, `ge raw <slot>` probe).
  - **Tab-stone aliases** (`AndroidMenu.postDesktopAliases`): CC_OP entries on mobile stones 601:{101,71,102,97,98,99,100,
    74,76,75,44,77,72,73} are also posted (MenuEntryAdded only) with desktop's 161 stone ids {59..65,43..49}, so
    MenuManager options keyed to desktop ids reach the plugin strip (Spellbook's "Enable spell reordering" on a
    magic-stone tap). 12 of 14 tap-checked; clan 601:74 / logout 601:44 are not in the mobile column.
  - `Widget.get/setClickMask` = game widget flags +0x2b0 (WidgetListeners keeps its op bits apart).
  - **Widget drag** (docs/offsets.md "Widget drag"): drag state at Client+0x413ad8 (dragged), +0x413b00 (dragged-on),
    u8 +0x413b20 dragging, +0x413b24 cycles, touch state +0x40e988 (3 held, 4 released). The menu-open check
    `0x58da0c` (a 4-instruction leaf) is inline-hooked with `FULLHOOK` (script_stubs.S, saves x0-x18/q0-q7/q16-q31/
    NZCV); called from return address 0x41ffe8 (the drag block of `0x41d0a4`) it posts the native WidgetDrag event
    before the game processes the release. Host posts `WidgetDrag` while dragging; getMouseCurrentButton is 0 on the
    release cycle; getDraggedWidget/getDraggedOnWidget/isDraggingWidget/getDragTime; setDraggedOnWidget(null)
    clears the target (unverified). No fps cost at the GE. `setprop debug.osrsrl.draglog 1` logs each release.
  - **Canonical widgets**: `AndroidWidget.of(id[, index])` — getWidget and the drag getters return one instance per
    component/child (plugins compare with ==; Spellbook's drag needed it). Other `new AndroidWidget(...)` sites remain.
  - getSearchingTagTab emulated at clientscript 276's start (`ScriptCallbacks.onRootStart`; unverified). bankBuildTab
    cannot be emulated: RuneLite's 277 is restructured (its tag-tab path has no counterpart in the game's 277).
    Script overrides were declined by the orchestrator (risk). Scratch tools used for this: a JS5 reader for
    `openrune/src/.data/cache/LIVE` (index 12 = scripts) and a CS2 decoder (RuneLite's `runelite/12/*` in the
    client jar have the same format); rewrite them if needed (they lived in the session scratchpad).
  - **Friends chat / clan channels** (`ChatChannels`, docs/offsets.md): getFriendsChatManager, getClanChannel(),
    getGuestClanChannel, getClanChannel(id) from static RE of the CS2 handlers 0x4ebdb0 / 0x4ecf20. All null for the
    tester (no FC/clan); unverified. Probe `set 'chat log 0'`. Asked the server dev whether OpenRune supports them.
  - Parked: Instance Map's drawInstanceMap. The mobile minimap is not a 512x512 software image (Minimap
    `*(Client+0x40e970)` holds 200-entry tile/texture arrays; `redrawminimap` only sets `mm+0xc = -1`), so it needs
    either RE of the minimap tile renderer (around 0x5895d0/0x58a56c) or a host-side render from tile data.
  - **Next (gen 25)**:
    1. Bank Tag Layouts: the plan at the top of this entry.
    2. Verify setDraggedOnWidget(null) (Bank Tags preventTagTabDrags on a tag tab) and getSearchingTagTab.
    3. Friends chat / clan data once the server supports it.
  - Gotchas: `tools/repack.sh` needs `ADB=/mnt/c/.../adb.exe ANDROID_SERIAL=<serial>` (it found no device with
    WSL's adb). Mobile drags: `input swipe x1 y1 x2 y2 400` drags; 1500 ms opens the long-press menu instead.
    Spellbook test keys are `spellbook.spell_pos_book_*` / `spell_hidden_book_*` in settings.properties (remove with
    the app stopped). The tester has an open 1 gp lobster buy in GE slot 1; leave it.
- **2026-10-17 (client dev gen 25)**, commits bb9a6e2..(see git log; progress at start: core 79/82, hub 29/32, API 545/866).
  - **Bank Tags layouts (built-in) verified on the device**: "Enable layout" from the tag tab's long-press menu, the layout
    drawn in the tag tab (gaps kept), drag swap and drag to an empty slot, the real bank order unchanged
    (setDraggedOnWidget(null) verified), tagging by dragging an item onto a tag tab.
    - Java onDragComplete listeners: `WidgetListeners.setOnDragComplete/runDragComplete`, keyed by slot + the game's
      Widget*, run on the WIDGET_DRAG release cycle (touch state 4) after WidgetDrag, before the game's swap.
    - Bank items (12:12 children) have no CS2 onDrag/onDragComplete/onOp on mobile and flags 0 (`set 'wlisten 12:12 0'`
      probe), so plugin `setOnDragListener(284…)` stays a no-op.
    - Leak fixed: desktop's bank build replaces item drag listeners, the mobile one sets none, so layout listeners ran
      in the main tab and cancelled real swaps. They are dropped before ScriptPreFired 505 (`Host.onScript`).
    - Canonical widgets now also for getParent/getStaticChildren/getChild/script widgets/onOp sources
      (`AndroidWidget.of`; `fresh()` for CC_CREATE). Bank Tags compares `draggedOn.getParent() == parent`.
    - Mobile drag recipe: `input swipe x1 y1 x2 y2 1000` over ~100-200 px; faster/longer moves scroll the bank.
    - Bank booth: `::tele 3208 3220 2`, long-press (1120,470), "Bank Bank booth" (1100,562); tag tab column at x 252
      (first tab y 642); main tab ∞ (335,423). Test tag via `set 'config banktags item_<id>=t25'` + `tagtabs=t25`.
      Cleanup: remove `banktags.layout_t25/tagtabs/tab/item_*=t25` from settings.properties with the app stopped
      (pull, edit, push).
  - **Bank Tag Layouts (hub)**: its "Enable layout (hub)" entry is added in ClientTick from the hover menu
    (`addTabInterfaceMenuEntries`, menu closed). 8cf3916 posts one ClientTick inside `AndroidMenu.presort` (menu reported
    closed) as the long-press menu opens. **Verified, row `runs`**: "Enable layout (hub)" in the strip (core's entry renamed
    "(built-in)"), the hub layout applied, a drag moved coins to an empty slot (mouse position at the release point).
  - **Friends chat / clan events** (8cf3916, d7fcb7c): per-tick diff in `ChatChannels.postChanges` (FriendsChatChanged,
    member joined/left, ClanChannelChanged, ClanMemberJoined/Left); channel objects stable while unchanged (Chat
    Channel compares with ==); getClanSettings = view from the channel (no titles). Chat Channel / Team rows added
    (jars in re/host/plugins-extra). **Verified with the server's `::fakeclan join|add <n>|del <n>|leave`** (args lowercased;
    not kept over a relog): `set 'chat log 0'` lists 'Test Fc' (owner Tester, OWNER) and clan 0 'Test Clan' with members;
    **Chat Channel runs** (join/leave lines for FC and clan after its 10-tick delay; config group is `clanchat`, keys
    `showJoinLeave`, `clanChatShowJoinLeave`). Team `starts`. The FC name reads "Test Fc" where the server's text says "Test FC": FC names travel as base37 names, which decode to word-capitalised form (desktop shows the same), so this is probably correct. The clan tab itself stays
    "Connecting..." until the server sends CLANSETTINGS_FULL (server dev).
  - API (546 -> 570): Widget scroll x/y/size, model zoom/rotation, setPos/setSize, setItemQuantityMode (+0x13c, mobile
    encoding 0 never / 3 always / 1 if >1; unverified), Client getFPS, getCenterX/Y, getVarbitValue(int[],int), desktop
    defaults for stretched/oculus/camera mode. Use `tools/progress.py --dry-run` (never the full run: it rewrites the
    orchestrator's progress files). More in gen 25: ParamHolder getLongValue/setValue (item/NPC/loc/struct), Node
    getHash/next/previous for containers/messages, ItemComposition xan2d/yan2d/zan2d. API 590/866 at the end.
  - Later in gen 25: getWidgetConfig (IF_SETEVENTS table walk, as 0x460dac; `wlisten` prints it) and getSubops (null)
    for Menu Entry Swapper; a bytecode scan of tracked core + hub jars for calls into throwing stubs (scratch script:
    javap -c per class, match `Method net/runelite/api/X.m` against ApiCoverage's missing list) left only GPU/devtools/
    camera/login/stretched paths plus: Bank Tag Layouts `ItemComposition.getInventoryModel` (clue scrolls only; the
    mobile ObjType decoder 0x63bfe8 has no opcode-1 case where expected, not found), Quest Helper's fake-object paths
    (Scene.removeGameObject, Player.setAnimation, NPC.getModel), Better NPC Highlight `NPC.getModel` for unnamed NPCs
    (needs face colours), Improved Tile Indicators `getAnimationHeightOffset`. ObjType decoder confirmed: opcodes
    4/5/6/7/8 -> +0xbc/+0xc0/+0xc4/+0xcc/+0xd0, 11 -> +0xd4, 12 -> +0xd8. Five untested hub rows added (Tile Packs,
    Custom Menu Swaps, Resource packs, C Engineer: Completed, Runecrafting Utilities; not pinned/built).
  - **Next (gen 26)**: the orchestrator's queue (gen 25's was Bank Tag Layouts, FC/clan readers, API coverage where plugins
    hit stubs). Open items: Team's counts on screen (next slot); pin/build the five new hub rows; getWidgetConfig and setItemQuantityMode's
    drawn result; getSearchingTagTab still unverified. (A short-lived F2P goal set during gen 25 was withdrawn, 819f24e.)
- **2026-10-17 (client dev gen 26)**, commits 6a98646..726a3d0 (progress at start: core 80/84, hub 30/37; end: core 82/84, hub 32/37, API 592/866).
  - **Team runs**: its FC/clan counters count nearby players with `Player.isFriendsChatMember/isClanMember`, which
    were constant false; they now check the name against the friends chat / clan 0 channel (`ChatChannels.isFcMember/
    isClanMember`; fresh on the game thread, the last read elsewhere). Config group is **`teamCapes`**, not `team`.
    Recipe: headless `tools/multiclient/desktop_login.py --user desktop --password desktop --seconds 200 --cheat 'tele 3223 3218'`
    (background), `::fakeclan join`, `::fakeclan add desktop`: two InfoBoxes "1" top-left; `::fakeclan del desktop` removes them.
  - **Friend List runs** (rechecked by me; the server dev's evidence was the game's own UI): title "Friends - W255 (1/200)"
    (script 631), "desktop has logged in. (World 255)" with `friendlist.showWorldOnLogin=true`.
  - **Five hub rows pinned/built**: Tile Packs (runs: Barrows pack 42 drawn at `::tele 3565 3289 0`), Runecrafting Utilities
    (runs: "Fill Small pouch" first in the bank), Custom Menu Swaps (starts), C Engineer (starts), Resource packs (starts).
    `host/plugins/hub/deps.txt` lists transitive Maven deps Gradle would resolve (resource-packs: antlr4-runtime for tomlj).
    Bytecode stub scan (scratch: javap -c, match `Method net/runelite/api/X.m` against ApiCoverage's missing list): only
    Resource packs hits stubs (getCrossSprites, getWidgetRoots, setCompass, setLoginScreen, Widget getContentType/setType/
    setOnTargetEnter/LeaveListener).
  - Host fixes: Gson bound to `RuneLiteAPI.GSON` (Color adapter; Tile Packs); `host/src/net/runelite/client/audio/AudioPlayer.java`
    replaces RuneLite's (MediaPlayer; players held until completion, the first build was GC-finalized mid-clip; the hold is
    unverified); CompatRewrite maps `Path.of` and `Files.readString` (API 34+). `getGameState()` reports LOADING from state 30
    until GameStateChanged(LOGGED_IN) is posted (latched, `Host.loggedInPosted`; Runecrafting Utilities' ClientTick NPE).
    **Teleports posted LOGGED_IN twice** (native 30->25->30, then the scene-base replay): a LOGGED_IN after LOADING now waits
    for the new scene base (or 50 cycles). Verified: login and a far teleport each give one LOADING, WorldViewLoaded, LOGGED_IN
    (each posted state is logged: "GameStateChanged X (getGameState Y, local player Z)").
  - Menu Entry Swapper getWidgetConfig: faithful; mobile's IF_SETEVENTS for bank items (12:12) is 0x140000 (drag bits, no op
    bits), so "Swap left-click" lists the default op too (desktop leaves it out). getSearchingTagTab and the item quantity
    mode's drawn result are still unchecked.
  - Probe additions (`setprop debug.osrsrl.set '...'`): `config <group> <key>` without '=' unsets; `sprite <id> 0`;
    `players 0 0` (names, fc/clan/friend flags); `pfield <PluginClass> <field>` (plugin field by reflection).
    Config read at startUp (Tile Packs' `tilePacks.packs=[42]`): edit settings.properties with the app stopped
    (force-stop, `adb shell cat`, edit, push), then start. A relaunch soon after a force-stop shows "account still logged in":
    wait ~20 s and tap "Try again" (958,668), then Play Now.
  - **Next (gen 27)**: Custom Menu Swaps (done in gen 27; the recipe here was wrong: `examine,man` never matches "man (level-2)",
    use `examine,man*` or a level-less target such as `examine,cat`); C Engineer audio checked objectively via AudioFlinger frames during the clip
    (as Metronome was; trigger with `config cengineercompleted selectAnySoundToTestPlayIt=LEVEL_UP`, unset after, and also
    remove the plugin's own `cengineercompleted.needToInformAboutDelayedCoxColLogs`); getSearchingTagTab; the quantity mode
    drawn result; Resource packs needs a hub pack installed (GitHub GETs) and mostly targets desktop interfaces. Remaining
    tracked stub hits (gen 25 list): getInventoryModel, QH fake objects, NPC.getModel, getAnimationHeightOffset.
    The emulator was unreachable at the end of gen 26 (adb connect refused), so the last slot did not run.
- **2026-10-17 (client dev gen 27)**, commits c76a6cb..(see git log; progress at start: core 82/84, hub 32/37, API 592/866; end: hub 34/37, API 596/866).
  - **C Engineer runs** (726a3d0's held MediaPlayer): `config cengineercompleted selectAnySoundToTestPlayIt=LEVEL_UP` played
    LevelUpCompleted_r2.wav to MEDIA_PLAYBACK_COMPLETE; `dumpsys media.audio_flinger` during the clip showed a second active track
    from the app's pid (44100 Hz, 0 dB) advancing ~33k frames in ~0.7 s, gone after completion.
  - **Custom Menu Swaps runs**: the gen 26 recipe `examine,man` can't match (targets are compared as `Text.standardize(target)` =
    "man (level-2)", also on desktop); `examine,cat` (the tester's follower cat, level-less, stays close) put Examine on top of the
    long-press menu and redirected plain taps. Use `man*` (STARTS_WITH) for leveled NPCs. Wandering men are hard to long-press; the
    server dev's "desktop" player may stand in the courtyard.
  - **getSearchingTagTab verified** (tag tab open, `set 'varbit 4150 1'`, tap a quantity button to rebuild 276: "bank tab 1 -> 0").
    **setItemQuantityMode verified**, all three modes, with the new probe `set 'wqty <group:child> <slot> <mode>'` (15:3 = inventory
    beside the bank).
  - **Actor models**: `NPC/Player.getModel` = `AndroidModel` (posed vertices, face indices, lit colours 1/2/3 from Model
    `+0xd0/+0xe8/+0x100`, static RE of the light pass `0x72aea4`; per-cycle cache). Verified with `set 'npcmodel 3108 0'` (Man: 288
    verts, 510 faces, 18 flat). Improved Tile Indicators' actor cut-out (`currentTileBelowPlayer`) is gated on `client.isGpu()`,
    which we answer false; deciding whether isGpu should be true on mobile (the game does render with GL) needs a look at what
    else checks it (GPU plugin paths, draw callbacks).
  - **Constant defaults counted as implemented** (orchestrator: keep, tagged `// unverified: default`, find the real field or exclude
    from the metric): NPC/Player.getAnimationHeightOffset (0), Model.getFaceTransparencies (null; the ModelData keeps alpha at `+0xe8`,
    not found in Model), Model.getFaceTextures (null).
  - Still open: getInventoryModel (ObjType decoder `0x63bfe8`/Ghidra `0x73bfe8`: its switch starts at case 2; opcode 1 is not a
    case, look at the code before the loop and the "opcode < 0x18" range checks), QH fake objects, Resource packs (hub packs = GitHub
    GETs), the Quest Helper desktop-vs-Android A/B (queue item 4; plan it with the orchestrator first: both locks + server dev).
  - Gotcha: `debug.osrsrl.set` splits into 3 parts (`split(" ", 3)`), so a probe's extra arguments arrive together in a[2].
- **2026-10-17 (client dev gen 28)**, commits da20f14..c1cec5e (progress at start: core 82/84, hub 34/37, API 596/866; end: API 640/866).
  - **Quest Helper A/B, desktop vs Android: matching.** Restless Ghost stage 1 → 2 (`::varp 107 1/2`) and Imp Catcher
    (`::varp 160 1`, then `::item 1470/1472/1474/1476`) give the same step text, requirement colours and minimap arrow on both.
    The desktop's earlier "overlay vanishes" was a harness problem: QH **sideloaded** into the RSProx-launched desktop can't
    load client resources (`/util/clue_arrow.png`), so every ObjectStep threw in startUp. The fix was installing from the hub by
    config (`runelite.externalPlugins=quest-helper` in `.rlcustom\profiles2\default-*.properties`, client stopped);
    docs/multi-client.md has the recipe. Version caveat: hub QH 4.18.0 vs our `re/hub/quest-helper.jar` **4.17.1** (not 4.17.0).
    Desktop client.log is ISO-8859 + CRLF: use `grep -a`.
  - **Live item prices** (f43c2d1, b2699a2): ItemManager fetches `api.runelite.net/runelite-1.13.1/item/prices.js` like
    ItemClient. It refreshes every 30 min, follows desktop's lookup order (WORN_ITEMS copied from 1.13.1, ItemMapping), and
    uses store value only while offline. The host's shared OkHttp client now sends desktop's `User-Agent: RuneLite/1.13.1-e7fb1f9`
    (network interceptor) and nothing else identifying. Verified: one GET with only Host/Connection/Accept-Encoding/UA; Ground
    Items shows the desktop price (red bead 1,205 gp). Earlier "verified" price rows were store values (noted in each row).
    The second ground label "(1240 gp)" is the **official client's own** value label (stays with Ground Items' prices off).
  - **Item/NPC model fields** (verified with probes `set 'idef 1351 0'`, `set 'npcdef 3108 0'`): ObjType +0x04 inventory model
    (mobile opcode 0x2c; there is no opcode 1), recolours/retextures, ambient/contrast; NpcType models (opcode 61), chatheads (62),
    recolours, width/height scale (scale unverified). docs/offsets.md has the layouts.
  - **QH fake objects**: used only by the default-off quest-complete "fan" (showFan) and the empty player-made-quest framework.
    The orchestrator decided bookkeeping only: RuneLiteObject registration tracked, models inert, nothing drawn;
    `loadAnimation` returns a one-frame placeholder (FakeNpc NPE).
  - Widgets (b447b7c, c1cec5e; all unverified): drag dead zone/time `+0x320/+0x324`; model type/id/anim, line height and laid-out
    x/y/w/h setters; hasListener `+0x348`; getItemQuantityMode; revalidateScroll. Java onTimer runs each cycle, onDrag each
    held drag cycle, onVarTransmit after a varp transmit (host-side triggers, `WidgetListeners.runCycle`). Other listener kinds
    are stored only. Client: getWidgetRoots, getRevision 241.
  - isGpu stays false (orchestrator): Improved Tile Indicators' below-player cut-out is a known gap (noted in its row).
  - Remaining tracked stub hits (scan: javap over hub jars + client plugins vs ApiCoverage's missing list, excluding
    gpu/devtools/camera/skybox/interfacestyles/loginscreen): Widget.setType (QH chat boxes, Resource packs),
    Scene.removeGameObject (QH fake objects), getRasterizer (Better Teleport Menu), setInventoryDragDelay (Anti Drag),
    closeInterface (Bank), Widget.setSubOp (Bank Tags), drawInstanceMap, Widget.isIf3 (Inventory Grid),
    getFocusedInputFieldWidget (Key Remapping), Low Memory, Roof Removal (buildRoofs/setRoofRemovalMode/getMapRegions on Scene),
    World Hopper (hopToWorld etc.), getMapElementConfig (World Map).
  - Gotchas: the scratchpad holds older generations' PNGs (use a fresh subfolder); OpenRune logs an idle player out after
    ~5 min (tap Play Now); `setprop x ''` fails on this Android ("Need 2 arguments"), so clear props with `0`;
    `debug.osrsrl.sbclick '<quest>'` only works from the quest list (send `Close helper` first if a helper is open).
  - **Next (gen 29)**: Chat Commands `!price` didn't rewrite on the device (probably not in the default plugin set); check with
    EXTRA=chatcommands. Device-verify the gen 28 widget listeners (Anti Drag's dead time; QH bank tab onTimer/onDrag).
    Then the remaining stub hits above, in tracked-plugin order.
- **2026-10-17 (client dev gen 29)**, commits 2e80692..ab94e59 (progress at start: core 82/84, hub 34/37, API 640/866; end: core 84/85, hub 34/37, API 643/866).
  - **Verified on the device**: Chat Commands `!price lobster` = "GE average 100" (RuneLite wikiPrice; needed f1b2a58: the
    price index build threw on items without an ItemMapping, and the lookup now follows desktop's mapping-first order);
    **Anti Drag** (new core row, runs): widget dead time `+0x324` (game's own 6), `Client.setInventoryDragDelay` stored only;
    caveat: delays past the ~1 s long-press open the long-press menu instead (its release picked Drop once);
    **Quest Helper bank tab** ("Tab Rune Mysteries" via the icon in the bank header, 12:1 children);
    **SoundEffectPlayed** (inline hook at `0x4ae510` inside the sound queue `0x4ae508`; consume() drops the sound; Music's
    mutePrayerSounds verified, `debug.osrsrl.soundlog 1`); **Client.closeInterface** (IF_CLOSESUB function `0x462384`;
    probe `set 'wclose <group> 0'`; client-only like desktop: the server keeps the interface open until the player walks);
    **Instance Map runs** (`drawInstanceMap` = the game's minimap renderer `0x589180` into its raster, copied into desktop's
    736 px extended-scene layout; offsets.md "Minimap image"; static RE by a subagent).
  - Host fixes: a Java onTimer/onDrag/onVarTransmit callback (or null) clears the game's CS2 slot (`Native.widgetClearListener`;
    CS2 `{scriptId,...}` listeners such as Bank Tags' onDrag 284 leave the game's script; slot clearing itself unverified);
    BufferedImage TYPE_INT_RGB starts black and stores opaque (Instance Map's image drew transparent); probe `wlisten` also
    prints onTimer and the drag dead time/zone.
  - Music stays starts: its buttons in 239:2 are overlapped by the track-name text 239:4 (a tap runs the game's op; long-press
    lists "Open Search"); the search dialog opens but the list isn't filtered (needs the musicTrackFilter callback: the same
    opcode-hook pattern as fairyringFilter in ScriptCallbacks). Not done: AreaSoundEffectPlayed (SOUND_AREA zone prot 13,
    actor sounds), getFocusedInputFieldWidget, Widget.isIf3/setSubOp, the QH A/B on more quests (waiting for the desktop).
  - Gotchas: after a force-stop the relaunch shows "account still logged in": wait ~20 s, Try again (958,668), Play Now.
    Typing into chat while the QH sidebar is open goes to a sidebar text dialog (Cancel it, close the sidebar first).
    The world map orb's long-press strip lists plugin entries twice (two orb aliases). The machine rebooted mid-gen
    (scratchpad wiped, locks reset).
  - musicTrackFilter (7f6a5d5, d1f8c95; unverified): proc 9291 (one call per music-list track; seen in the scripts log on
    the device) takes its args at the GOSUB and raises the callback as it returns (as questFilter); the opcode watch can't
    see RETURN. RuneLite's patched 9291 (decoded from client-1.13.1.jar `runelite/12/9291`): override 0 returns 0
    (hidden), 1 skips the varbit 19733 locked/unlocked test but still applies the varbit 19731 area filter. On the device
    the search ("alo") did not filter; the first 3 calls are logged at login only, so check with `setprop
    debug.osrsrl.musiclog 1` whether the callback runs on the redraw (Music redraws by runScript of 239:8's
    onVarTransmit listener) and what the game returned. Script events are now on whenever ScriptCallbackEvent has
    subscribers (proc entry/return drive the callback emulation). The installed build lacks only d1f8c95's last edit.
  - **Next (gen 30)**: finish musicTrackFilter (promotes Music); Instance Map inside a real instance (GWD Bandos: check and restore `godwars_counter_bandos` first);
    the QH desktop-vs-Android A/B (Rune Mysteries 63: 1->2->3->5->6, Ernest 32) when the orchestrator frees the desktop;
    Music's musicTrackFilter callback and its button hit-testing; AreaSoundEffectPlayed; remaining tracked stub hits:
    Widget.setType (QH chat boxes, Resource packs), getRasterizer (Better Teleport Menu), Widget.setSubOp (Bank Tags potion
    doses), Widget.isIf3 (Inventory Grid, assertion only), getFocusedInputFieldWidget (Key Remapping), Roof Removal, World
    Hopper, getMapElementConfig (World Map).
- **2026-10-17 (client dev gen 30)**, commits 2858420..c5a59fd (progress at start: core 84/85, hub 34/37, API 643/866; end: core 85/90 after 5 new rows, hub 34/37, API 652/866).
  - **Music runs**: musicTrackFilter (proc 9291 GOSUB/return) verified; search "alo" and the Unlocked orb filter the list.
    Gen 29's "not filtered" was the Android dialog's OK closing the input: ChatboxTextInput now stays current after an OK
    whose onDone returns false (desktop's Enter), verified; long-press "Close Search" restores the list.
  - **Instance Map inside GWD Bandos** verified (`::varbit godwars_counter_bandos 40` (was 0; the door consumes it),
    `::tele 2862 5354 2`, `set debug.osrsrl.where 26503` gives the door's screen point, long-press, Open). Use `::god` (and
    turn it off after): the bodyguards killed the tester once without it.
  - **AreaSoundEffectPlayed** (f83f690): inline hook on the area-sound queue `0x4ae650` (offsets.md "Area sounds"); the
    animation frame-sound handler's vtable slot 2 (`0xa6c5b8` -> `0x47ff8c`) is patched to know the animated position,
    and the source is the actor at that fine x/z. Packet path verified (OpenRune SOUND_AREA 413/3044, Music's
    muteOtherAreaEnvironmentSounds consumes). Frame-sound path unseen: OpenRune sends skilling/combat sounds as SYNTH_SOUND.
  - New core rows (orchestrator: core saturated): World Hopper, Wiki, Key Remapping, Inventory Grid, World Map: all
    `starts` (World Map needed a non-null WorldMapRenderer, reports not loaded). **World Hopper: never hop**; its list is
    Jagex's real worlds. Load it with `worldhopper.ping/displayPing/menuOption=false` in settings.properties.
  - Stubs: getFocusedInputFieldWidget null, Widget.setSubOp/getSubOps host-side, isIf3 true (all unverified defaults).
    getRasterizer stays a known gap (orchestrator OK).
    Roof Removal stubs (Scene.setRoofRemovalMode/getRoofRemovalMode stored, buildRoofs no-op, getRoofs empty,
    Scene.getMapRegions): the plugin should start, but it hides no roofs. Its setGameState(LOADING) reloads the scene.
  - Gotchas: `tools/repack.sh --install` needs `ADB=... ANDROID_SERIAL=<serial>` (it otherwise starts the WSL adb
    daemon: `adb kill-server` after). Music buttons: long-press the search icon at (1663,640), menu row 1616,937; the
    list area ~1300..1640 x 700..1000. Config keys live in `files/settings.properties` on the device (edit with the game
    stopped; `set 'config g k=v'` writes it too).
  - **Next (gen 31)**: the QH A/B on Rune Mysteries / Ernest when the orchestrator frees the desktop; the frame-sound
    source (find content that plays seq frame sounds, e.g. an NPC with sound frames, or ask the server dev for one);
    Inventory Grid's drag grid (needs a real touch drag; adb swipe opens the long-press menu); World Map icons (read the
    game's world map regions); Roof Removal for real (PreMapLoad + the game's roof hiding, RE needed; untested on the device); World Hopper's world list getters (no hop).
- **2026-10-17 (client dev gen 31)**, commits 7180510..ad2fc4e (progress at start: core 85/90, hub 34/37, API ~650/866).
  - **Quest Helper A/B, desktop (hub 4.18.0, fresh qhab31a) vs Android (4.17.1): matching** on Rune Mysteries (varp 63
    1,2,3,4,5,6 with items 290/291) and Ernest the Chicken (varp 32 1,2,3 with 271/276): step text, requirement colours,
    NPC/object highlights and world arrows, overlay closing at completion. Shared QH quirks and the driving recipe are in
    docs/multi-client.md "A/B results, gen 31". The desktop devtools screenshot shows no QH minimap arrows at all
    (ALWAYS_ON_TOP layer probably not captured), so don't compare minimap arrows that way. `desktop.sh login` with no
    args reuses the last account.
  - **ExamineItem emulation** (c5111fa, f478897; verified with the probe and with real examines after the server
    revert 4282437): proc 6004's `mes` is watched always (`ScriptCallbacks` BASE_WATCHES); the type-0 line is removed
    and re-added as ITEM_EXAMINE (27), its type-0 event dropped (`takeReplaced`), and 6005's price lines in the same
    game cycle are removed. Probe `set 'examine <obj> <count>'` runs 6003 client-side. `Native.message` now reads the
    game's message directly on the client thread when it isn't captured yet.
  - **World map** (d05f4d6, verified): the game's world map object `*(Client+0x418660)` (offsets.md "World map"):
    getWorldMapPosition/Zoom, renderer isLoaded, surfaceContainsPosition (coordinmap on map 0, band fallback),
    setWorldMapPositionTarget (jump, unverified). Probe `set 'wmap 0 0'`.
  - **Inventory Grid runs** (grid seen mid-drag; recipe in its row). **Kitten Tracker**: follower varp after a relog fixed
    server-side (verified).
  - **Next (gen 32)**:
    1. World Map plugin points: none were visible on the open map. Check WorldMapPointManager has points (agility/
       fairy ring defaults), WorldMapOverlay's widget bounds vs our canvas scale (595:7 geo 191,44 533x450 in layout
       units), and the clip area (595 children 30/54). Then read the game's world map regions/icons
       (`WorldMapRegion`, `getMapElementConfig`) for quest-start tooltips.
    2. AreaSoundEffectPlayed frame-sound path (needs content with seq frame sounds; ask the server dev).
    3. Roof Removal: **parked** (orchestrator): needs RE of the mobile scene draw and the game's roof hiding.
    4. World Hopper world list getters (never hop).
- **2026-10-17 (client dev gen 32)**, commits cda40a1..1bedb66 (progress at start: core 86/90, hub 34/37, API 652/866;
  now core 88/90, hub 34/37, API 655/866).
  - **World Map runs.** Points never drew because the host never added `WorldMapOverlay` (desktop's OverlayManager.init does);
    Host now adds it. Region icons (`getMapRegions`, game.cpp `world_map_icons`) and mel categories (`getMapElementConfig`,
    table at ELF 0x1212620, category +0x180) feed World Map's quest-start points (14 near Lumbridge, 89 after panning: the
    game loads region icons lazily). `setWorldMapPositionTarget` verified (probe `wjump x y`). Probe `wmap 0 0` logs points,
    595 widgets, regions, icons and categories.
  - **Gotcha (crashed the game once):** the region icon list/hash hold shared_ptrs {control block, object}; the icon is node
    +0x18 (list) / +0x10 (hash). Calling vtable slot 1 on the control block frees it.
  - **getDBTableField**: a column the row lacks, whose game fallback is the generic -1/"", now reads empty (World Map skips
    quest rows whose COL_PARENT_QUEST 21 is set; quests read -1 there). Empty results are cached; getDBRowsByValue is cached.
    Widget geometry is cached within an overlay frame (`AndroidWidget.beginFrame/endFrame`): WorldMapOverlay asks 595:7's
    bounds once per point (1800+), which dropped the game to ~4 ticks/s.
  - **KeyRouter** (hardware/adb keys through RuneLite's KeyManager while any KeyListener exists; Ground Items registers one,
    so it is always on): keys are held on the main thread, run through the listeners on the client thread, and re-dispatched
    in order (unchanged, remapped, or dropped). Typing verified with adb key events and the on-screen LatinIME
    (`settings put secure show_ime_with_hard_keyboard 1` shows it; restore to 0). `setprop debug.osrsrl.keylog 1` logs remaps.
    **Key Remapping runs** (D -> RIGHT turns the camera) after a ConfigManager fix: Keybind values are "keyCode:modifiers".
  - `getServerVarps`, `setVarpValue` (unverified). **LinkBrowser** replacement opens web links via ACTION_VIEW (0dd6b23).
  - World Hopper stays `starts` (orchestrator): no plugin calls getWorldList; its gaps are the hop paths we must not use.
  - **LinkBrowser verified** (Wiki's skills-tab entry opens the wiki in Chrome). Wiki stays `starts`: the mobile skill widget's
    first action is "Toggle <skill> XP", so the page is wrong, and the entry is listed twice. Opening a browser backgrounds the
    game, which drops the session (relaunch with plugin-test.sh, wait ~20 s, Try again, Play Now).
  - **Next (gen 33)**: Wiki's WIKI orb lookup (targeting mode: the onTargetEnter listener on its CC_CREATE icon) and Search; F-key remaps / chat lock in Key Remapping; the AreaSoundEffectPlayed frame-sound path (needs server content);
    remaining Client API gaps (mostly GPU/stretch/camera/login setters no tracked plugin uses).
- **2026-10-18 (client dev gen 33)**, commits 13cc8cb..f715825 (progress at start: core 88/90, hub 34/37, API 655/866;
  now core 89/90, hub 34/44 with 7 new untested rows, API 655/866).
  - **Backgrounding** (static RE + device): the official client opens links through its Java
    `ApplicationContext.openBrowser(url)` (native caller ELF `0x98a214`: http(s)/market:// only; flag 1 = openOverlaidBrowser,
    a TWA/Custom Tab), which shows Jagex's "You are opening a link to an external site. If you remain inactive in game for
    too long, you will be logged out." dialog, then ACTION_VIEW. Before OpenRune 6ab7d07 the stock client (`debug.osrsrl.off 1`)
    dropped too after 15 s in the background ("Unexpected server response" on its reconnect). Since 6ab7d07 (reconnect login):
    Home 30 s comes back in place ("tester reconnected (ENHANCED_ANDROID)"); **while in the background the game reconnects
    every ~16 s** (its thread runs throttled); Home 90 s ends on the login screen, and a relaunch right after still gets
    "account still logged in" until the 60 s grace ends. After any force-stop: wait ~60 s, Try again, Play Now.
  - **LinkBrowser** now calls the game's openBrowser by reflection (field of type ApplicationContext on MainActivity),
    falling back to ACTION_VIEW (5eab9a5, verified: dialog, Chrome, back within ~34 s in place).
  - **Widget target mode** (f715825; offsets.md "Widget target mode"): state at `*(Client+0x4132b0)+2000`; Java
    onTargetEnter/onTargetLeave follow it (`WidgetListeners.runTargetMode`), isWidgetSelected reads it,
    setWidgetSelected(false) clears `+0x9f`. Target-mode entries carry op -1 (WIDGET_TARGET_ON_*), raw[9] byte 3 = 1 marks
    a widget's target verb (WIDGET_TARGET). **Wiki runs**: orb Lookup on inventory Logs and a Tree, Search. Wiki's
    skills-tab entry is a content difference (mobile 320:N ops = ["Toggle <skill> XP", "View <skill> guide"]); its search
    predictions NPE (no chatbox container under the Android text dialog). Probe `debug.osrsrl.tmodelog 1`.
  - Hub: 7 new pins built, `untested` (Mahogany Homes, Rogues' Den, Target True Tile, Monster Menu HP, Looting Bag, Action
    Progress, Consumable Cooldowns; recipes in their rows). Arc2D.setFrame(Rectangle2D) added to the shim.
  - Android token logins all map to one account: OpenRune `SPIKE_TOKEN_USERNAME` (default tester; ConnectionHandler.tokenLogin).
    A fresh Android account needs that env changed on the container (orchestrator) or a server code change.
  - Hub batch on device (3b7cfb6): Action Progress, Rogues' Den, Target True Tile run; Consumable Cooldowns, Mahogany Homes,
    Looting Bag, Monster Menu HP start (reasons in their rows). Hub 37/44.
  - 1c9bad2 (unverified): UsernameChanged posted once per login (Mahogany's saved contract), and
    ChatboxPanelManager.getContainerWidget returns a WidgetBag sink (Wiki search predictions NPE; also ChatboxItemSearch).
  - Device helpers used this gen (scratchpad, not committed): drop items by long-press + MenuOpened order (rows are
    52 px, the menu is shifted up when it would pass y 1078); login: Play Now only after a screenshot shows it, Try
    again after the 60 s grace. NPCs walk: long-press within ~0.3 s of the screenshot.
  - **Next (gen 34)**:
    1. Device-check 1c9bad2: Mahogany (keys `MahoganyHomes.<hash>.currentHome=JEFF`, `.currentTier=1`, hash of "tester"
       = 3417797823; `::tele 3239 3450 0`), Wiki search with predictions (no NPE).
    2. Consumable Cooldowns: `consumablecooldowns.showItemCooldownPreview=true` to split drawing from eat detection.
    3. QH on Android for Doric's Quest / Witch's Potion / Goblin Diplomacy (and Vampyre Slayer) on a fresh account once
       the server dev carries a username from the local sign-in into the token login (orchestrator asked for it);
       varp trails in docs/openrune-port.md. Never use `::empty` on the tester.
    4. Monster Menu HP with a low-level account (same per-login accounts).
- **2026-10-18 (client dev gen 34)**, commits f39c794..cfef142 (progress at start: core 89/90, hub 37/44, API 655/866;
  now core 89/90, hub 44/50, API 655/866).
  - **Message-inventory snapshot off-by-one** (f39c794, verified): the Message constructor `0x3f0a04` takes id = counter
    and then increments it; the hook keyed snapshots to next+1, so every chat line replayed the previous line's
    inventories (stale ItemContainerChanged in packet order; Consumable Cooldowns AIOOBE). Loot Tracker's casket rechecked
    after the fix.
  - **getAccountHash** uses the raw game state (LOGGED_IN or LOADING): the login UsernameChanged is posted while
    getGameState still reads LOADING (Mahogany Homes' saved contract).
  - **MenuEntryAdded sees desktop's growing menu** (007d26f, verified): `AndroidMenu.postAdded` (long-press and tap) shows
    getMenuEntries() ending with the entry just added; setMenuEntries within the pass keeps the not-yet-added entries
    (Monster Menu HP).
  - **Login ItemContainerChanged held** until the local player exists, like StatChanged (e21e882, verified: Looting Bag).
  - **Plugin startUp runs as the EDT** before cycles start (`Edt.runAsEdtAtStartup`; desktop SwingUtil.syncExec), **ClientUI
    bound to an unconstructed instance** (Unsafe.allocateInstance; RuneLite's constructor casts Client to Component),
    **GenHeadless JList(ListModel)** keeps the model (cfef142).
  - Runs now: Mahogany Homes, Consumable Cooldowns, Monster Menu HP, Looting Bag, Visual Ticks, Player Outline, Tree
    Count; Wiki search no longer NPEs. Starts: Ground Object Hider (`Tile.setGroundObject` gap), Annoyance Mute, Bank
    Memory (sidebar doesn't render JList items).
  - **QH on Android on a fresh account** (qhand1): Doric's Quest, Witch's Potion, Goblin Diplomacy, Vampyre Slayer,
    Pirate's Treasure complete and match desktop (docs/multi-client.md "QH on Android, gen 34", with the not-covered
    steps). Account choice: `docker exec osrs-openrune-http sh -c 'echo <name> > /tmp/local-auth-user'` before Play Now;
    `rm -f` it to return to tester. OpenRune commands: `::mypos`/`::pos`, `::heal`, `::god` (toggle), `::master`,
    `::tele`, `::item`, `::varbit`, `::quest`; unknown commands are silently ignored.
  - Device driving this gen: devtools (`setprop debug.osrsrl.devtools 1`, push `devtools.jar`, `tools/devtools_mcp.py
    --call tools/call '{"name":..,"arguments":..}'`): `interact_npc {npcIndex|npcName, option}`, `get_dialogue`,
    `continue_dialogue`, `select_option {option}`, `interact_object {objectId, option}`, `get_var`, `walk_to` (minimap,
    short legs only), `get_combat_history`. devtools `canvasX/Y` don't map to screen pixels (use screenshots).
    `get_inventory` threw a Gson null error. QH helper: `sbclick` fires once per value and needs the RL panel's quest list;
    `sbtext <quest>` + a tap on the filtered row works any time. The RL panel close button (43,26) is the game's logout
    button when the panel is closed. QH drops the active helper on relog on desktop too (checked; not a host gap).
  - **Next (gen 35)**:
    1. Ground Object Hider: `Tile.setGroundObject` (remove/restore a scene tile's ground decoration; RE the scene tile).
    2. Bank Memory's item list: render JList (and its cell renderer) in the sidebar model.
    3. Annoyance Mute: AmbientSoundEffectCreated / `AmbientSoundEffect` getters.
    4. More hub plugins by install count (ranked list: `api.runelite.net/runelite-1.13.1/pluginhub`, skip GPU/upload ones).
- **2026-10-18 (client dev gen 35)**, commits 83de8a1..(this entry) (progress at start: core 89/90, hub 44/50, API 655/866).
  - **Tile.setGroundObject(null)** (83de8a1, verified): the game's ground-decoration remover `0x6dca24(scene, plane, x, y)`
    (offsets.md, gen 35 table), then GroundObjectDespawned. A posted LOADING forgets SceneObjects' objects, so a
    setGameState(LOADING) reload spawns everything again (Ground Object Hider re-hides). Non-null throws.
  - **Sidebar JList** (verified, Bank Memory): rows from the cell renderer, one tappable 'H' line each (up to 400). A tap
    selects the row, then runs the list's mouse listeners. The headless JList has selection state (default -1;
    getSelectedValue from the model; setSelectedIndex notifies listeners; locationToIndex = the selected row). No item icons yet.
  - **Ambient sounds** (4dceb42, verified, Annoyance Mute): `getAmbientSoundEffects` is the live list at WorldView `+0xb78`.
    clear() is the game's `0x4b065c` (entries go to the pending vector `+0xb90`, and the updater stops them next cycle).
    addLast takes back an entry cleared in the same cycle. AmbientSoundEffectCreated is posted per new entry (before
    LOGGED_IN); during that batch, iteration and clear() cover only the entries announced so far (`game::ambient_remove` moves single entries).
  - **Menu entry removal** (verified, Not Empty): presort writes back a subset when plugins drop entries;
    `game::menu_reorder` shortens the node vector and drops only childless nodes. **Tap ClientTick** (verified, Custom
    Left Click Drop): MENU_TAP posts ClientTick with the menu reported closed, before PostMenuSort.
  - **WorldView.getCollisionMaps** (Attack Ranges): Scene `+0x2f8` maps (offsets.md "Collision maps"). Flags are read
    but not compared with desktop.
  - Hub: 8 new pins (corner-tile-indicators, npc-id, high-alc-highlight, bank-xp-value, zom-leftclick-dropper,
    instant-inventory, not-empty, attack-ranges). Not Empty, Custom Left Click Drop, NPC ID, High Alc Profit, Corner Tile
    Indicators, Ground Object Hider, Bank Memory and Annoyance Mute run; Attack Ranges, Instant Inventory and Bank XP
    Value start. Ranking: `curl https://api.runelite.net/runelite-1.13.1/pluginhub` (name -> installs), skip GPU/network/minigame ones.
    Static stub check: javap the hub jar for `net/runelite/api` calls and check the Android* class or its non-Stub supers
    (Client default methods like getNpcs are fine).
  - Looting bag on OpenRune a504b83 (sent to the orchestrator): Settings works; Store-X (use item on bag in the
    Wilderness) does nothing (server); the bank's bag shows no View option.
  - Gotchas: the safety check can deny `adb shell "rm -f .../plugins/*.jar"` when it's built with variables (it can't check the
    script). Remove jars by full name, or ask the orchestrator. Reinstalls log the client out: wait ~60 s before Play Now.
  - **Next (gen 36)**:
    1. Attack Ranges with a ranged weapon or autocast (`::item 841` shortbow + arrows): check the range tiles; compare collision
       flags with desktop (e.g. Shortest Path's or Attack Ranges' line of sight at the same tile).
    2. Sidebar JList icons (ItemListRenderer sets an ImageIcon: send it as an 'I' row or an icon next to the 'H' text).
    3. Bank XP Value's UI (find its toggle), Instant Inventory's fade (screenshot right after a drop).
    4. More hub plugins from the ranking (next generic ones: loot-logger, supplies-tracker, damage-counter, line-markers,
       random-event-hider, coal-bag, slayer-assistant, spirit-tree-map).
- **2026-10-18 (client dev gen 36)** (progress at start: hub 52/58, API 657/866; dry run now hub 59/73, API 657/866).
  - **Attack Ranges A/B with desktop** (shortbow + bronze arrows, 3224,3218, `attackRanges.displayMode=TILES`): both clients
    draw the same cross-shaped range cut by bushes/fountain/castle wall, including an isolated tile NW of the stairs, so
    `WorldView.getCollisionMaps` flags match desktop (offsets.md). Desktop camera is low and north-facing; Android top-down
    (tap compass, swipe down). Desktop hub plugins go in via `runelite.externalPlugins` in the isolated profile (back it up first and restore it).
  - **Sidebar JList icons** (verified, Bank Memory): rows whose renderer is a JLabel with an ImageIcon are sent as kind
    `R` ("<icon id>:<text>"; `Sidebar.iconId` pushes pixels once per change); overlay.cpp draws icon + text in a Selectable,
    textures only for visible rows.
  - **Colour config format = desktop**: the host wrote colours as bare hex ("66eaff67") and parsed everything as hex, so
    desktop-style values (decimal `String.valueOf(getRGB())`, read with `Integer.decode`) were misread (Attack Ranges' fill
    came out green). ConfigManager now writes decimal and reads decimal/#hex/0x; 8 bare hex digits still read as hex.
  - **Headless GridBagConstraints defaults**: GenHeadless emits JDK instance-field defaults for plain value classes
    (only GridBagConstraints so far: insets (0,0,0,0), gridx/gridy -1, anchor 10, gridwidth/height 1). Emote Clue Items no longer throws.
  - Bank XP Value (toggle: long-press the bank wrench -> "Toggle Banked XP"), Instant Inventory (one dimmed frame before a
    dropped item vanishes) and Attack Ranges now `runs`.
  - Hub: 16 new pins. Device: Damage Counter, Supplies Tracker, Emote Clue Items, Time Tracking Reminder, Pickpocket Helper and
    Loot Logger (with the core Loot Tracker, which posts LootReceived) run; Line Markers, Coal Bag (OpenRune's coal bag texts differ, sent to
    the server side), Spirit Tree Map, Fish Barrel, Clue Steps, Pickpocket Helper, Did I Compost, Menu Swapper Extended,
    Weapon Charges, Random Event Hider start. Slayer Assistant is blocked: FlatLaf's `FlatTabbedPaneUI` (host gap).
  - Tools: `host/plugins/hub/stubcheck.sh <jar>` (static API gap check, replaces ad hoc javap); hub build.sh now fails on
    javac errors instead of packing a partial jar.
  - Gotchas: `server/tools/device.sh login` taps (958,668) first, which on the Play Now screen is near "Sign out"; use it
    only on the "Try again" screen, else tap Play Now (957,563). Wielding spawned ammo merges it with the tester's own stack
    (tester had 50 bronze arrows; now 148 equipped, fix pending via the bank). The sbdump prop only takes effect at startup.
  - **Next (gen 37)**:
    1. (Done in slot 2: tester back to 50 bronze arrows equipped, bank unchanged at 35 items, quantity mode 1, last X 10000.
       The pickpocket test left one extra Coin pouch (2 instead of 1).) Device-check Supplies Tracker's item icons (icon-only labels now sent as kind I).
    2. Loot Logger with the core Loot Tracker loaded; Supplies Tracker item icons (its SuppliesBox grid); Line Markers drawing.
    3. FlatLaf shim for Slayer Assistant (`FlatTabbedPaneUI` etc.), GraphicsObject/Projectile API (Random Event Hider, Supplies Tracker).
    4. More hub plugins from the ranking (next generic ones: time-tracking done; tasks-tracker, door-kicker, gemstone-crab-timer,
       startierindicator, barrows-potential, pyramid-plunder-counter); device-check the 11 that only start.
- **2026-10-18 (client dev gen 37)**, commits bafb538..(this entry).
  - **Privacy (top priority this gen, docs/app-telemetry.md)**: the official app POSTed the OneTrust consent record
    with the AppAuth bearer token (on LOCAL_AUTH: the sidecar's unsigned test token) to
    `account.jagex.com/consent/sync` at every Play Now (Jagex answered 401). Fixed and verified: `tools/patch_telemetry.py`
    (repack step 1b for the APK + step 3 `--native` for the game library, every build): za.l.a no-op, both
    native sync URLs zeroed; Singular never starts; Firebase Analytics deactivated; Crashlytics collection off.
    Verified: no *.jagex.com DNS lookups and no sockets to Jagex across sign-in/relog/restart. Remaining by
    decision: OneTrust banner GET, Braze (left on), the host's runelite.net GETs. Pending: payments param 2
    (sidecar rewrite, orchestrator), the GIM score host -> `x.invalid` (5befe07, approved, not yet device-checked).
    Normal builds now decode resources (`apktool d` without -r). Never open store/membership/bond UIs until
    param 2 is rewritten.
  - **Projectile API** (4685867, verified): Client +0xcb30 list (offsets.md "Projectiles"); getProjectiles and
    ProjectileMoved per client tick. `debug.osrsrl.projlog 1` logs projectiles (and graphics objects).
  - **GraphicsObject** (7ba86aa, unverified): top-level WorldView +0x160 list, GraphicsObjectCreated; needs the
    sidecar/server `::mapanim` (in the re-pin bundle) to verify fields and the finished flag (Random Event Hider).
  - **Sidebar/Swing**: Container.add reparents (Supplies Tracker's triple empty panel); Shift toggle -> VK_SHIFT key
    events (Line Markers "Add line" works); FlatTabbedPaneUI stand-in + FlatLaf compile-only for hub builds;
    stateful JTable/DefaultTableModel/column model, JList.setListData, JTextPane styled document, JTabbedPane
    (58c0525, tab headers + selected tab, not device-checked). Slayer Assistant starts and lists tasks.
  - Hub: 8 new pins (one-tick-flick, afk-countdown, ring-of-recoil-notifier, spamfilter, effective-level,
    path-marker, line-of-sight, dropsounds); plugin-host.md rows updated.
  - Tester: 1 coin pouch, coins 100,289 (6 gp from an accidental Open-all, left as is), home tile 3219,3222.
  - Gotchas: the plugin strip under the long-press menu needs a held tap (`input swipe x y x y 150`); after a
    reinstall Play Now often shows "Try again" (tap 958,668 only on that screen, then Play Now). Socket check:
    `adb shell cat /proc/net/tcp /proc/net/tcp6`, state 01, app uid 10071.
  - **Next (gen 38)**:
    1. Device-check the build with the GIM `x.invalid` rewrite (5befe07, approved) and the sidecar's param 2 fix:
       no Jagex sockets, and no `pp-*.x.invalid` DNS lookup during normal play.
    2. `::mapanim` once deployed: GraphicsObject fields/finished flag (projlog dump), Random Event Hider.
    3. Slayer Assistant task tabs on device (58c0525); Menu Swapper Extended stray-dog swap (Varrock dogs absent).
    4. Line of Sight desktop A/B (desktop lock), Path Marker settings; more hub plugins from the ranking.
- **2026-10-18 (client dev gen 38)**, commits 11e69d2..(this entry).
  - **Privacy**: build c531d81+ verified with the GIM `x.invalid` rewrite (no `pp-*` lookups, no Jagex sockets). After a
    fresh sign-out/sign-in, one `secure.runescape.com` lookup and HTTPS socket appeared at the first LOADING (15:51:42).
    Traced in native code: jav_config `gedigesturl` (GE digest, most likely) and the web URL builder (ELF 0x41102c, bare
    `runescape.com` at 0xd6812: secure./services. URLs). The orchestrator redirected gedigesturl/advert/terms/privacy in
    the sidecar; `patch_telemetry.py --native` now rewrites the bare domain to `x.invalid` (85f7d62). Verified with a 0.5 s
    socket sampler across sign-out, sign-in, Play Now, restart and play: only runelite/braze/onetrust/Google and 10.0.2.2.
    The 15:51 source is still unconfirmed, because no digest fetch happened in the verification run. Payments param 2
    is served-config-only: "Upgrade Now" is a server CC_OP that OpenRune ignores, so no shop request was made.
    Details: docs/app-telemetry.md (gen 38 sections).
  - Verified on the device: Slayer Assistant tabs (58c0525) and per-paragraph text rows (Sidebar, 11e69d2); Menu Swapper
    Extended's stray-dog swap (spawn `::npc 500 2902`; the swap shows in tap/long-press sessions only).
  - `Client.getRenderOverview` (39e11a2) for herbi-afk. 7 new hub pins all start (see plugin-host.md).
  - Line of Sight desktop A/B: the bending over terrain matches desktop. At 3219,3222 Android draws stray lines that
    converge on two points (most likely extreme corner heights in Perspective); not traced yet.
  - Server gap sent: OBJ_ADD has no despawn/public timers or ownership (rsprot 5-argument constructor), so ground item timers read 0.
  - Gotchas: `adb.exe shell cat` adds CRLF, so take settings backups with `adb exec-out cat`. Don't combine `rm` with a
    `bash -c` wait loop in one command (the safety check denies it); run the by-name rm alone. The desktop client can
    be left running by another agent: check `desktop.sh status` before starting it.
  - **Next (gen 39)**:
    1. `::mapanim` (when deployed): GraphicsObject fields and the finished flag; Random Event Hider.
    2. Line of Sight stray lines: dump getTileHeights/settings around 3219,3222 (Probe `h`) and compare the corner
       heights Perspective uses.
    3. Path Marker settings; more hub plugins from the ranking (most of the remaining top ones are minigame, raid or
       network plugins).
    4. A sign-out/sign-in capture that includes a GE digest fetch, to confirm the gedigesturl redirect (it needs the
       client's digest refresh to fall due).
- **2026-10-18 (client dev gen 39)**, commits 0236375..(this entry).
  - **Line of Sight stray lines at 3219,3222: not a host bug.** Probe `tiles <range> 0|1` (16ef5e8) dumps every tile's corner
    heights, settings, `getCanvasTilePoly` and LoS's localToCanvas corners. The heights are sane (castle-side lawn -464, path -240,
    sloped tiles between). The logged polygons, re-rendered offline with Java2D, match the device picture line for line,
    so the fans are the terrain (raised lawn, uneven corners at the castle wall). Near-camera corners project to x/y around 21000; clipping
    them before Skia changed nothing (reverted). Path Marker verified (active path tiles after a tap-walk).
  - **GraphicsObject** (b0e9ea8, verified with `::mapanim`, which is deployed): AnimationState at obj +0x280 (+0x288 SeqType*, +0x290 seq id,
    +0x294 u16 frame cycle, +0x296 u16 frame). The walk at ELF 0x455f24 drops objects whose +0x288 is 0. setFinished(true) calls
    setSequence `0x659274(obj+0x280, -1)` (new kBuilds field `seq_set`). Probe `gfin <id> <n>` (change n to re-run the same id).
    A delayed `::mapanim 86 0 400 2 0` plus `gfin` left the list one cycle later. Random Event Hider still can't be staged on OpenRune.
  - **Hub**: 9 new pins start (max-hit-calculator shows "Max Hit: 11", ticktracker "100.00%"; fishing-notifier,
    slayer-task-sorter, cannon-highlighter, set-timer, death-charge-reminder, arceuus-timers, herblorerecipes not exercised).
    multi-lines dropped: it uses java.net.http.HttpClient to raw.githubusercontent.com. It failed on Android and sent nothing, but it wrote one
    config key (settings restored from the backup).
  - **Network**: Network QA rule (docs/app-telemetry.md): no per-slot DNS/socket check; QA only for network-relevant builds.
    `host/plugins/hub/netcheck.sh` (a60f352) gives NET-RAW / NET-OKHTTP / LINKS / clean verdicts. build.sh refuses NET-RAW unless
    `ALLOW_NET=1` (loot-lookup was rebuilt with it: its raw references are only in the bundled jsoup). The runtime gate R1
    (`host/src/net/runelite/host/NetScan.java`, written, **not wired or committed** until the orchestrator approves) refuses
    NET-RAW plugin jars at load time, except vetted ones (hub-loot-lookup, devtools) or those in `debug.osrsrl.netallow`.
  - NPCComposition.getStats (dc9a9c6): NpcType +0x1a0..+0x1b4 (ops 74-79, static RE). The `npcdef` probe prints the stats. Device check pending.
  - Gotchas: the `debug.osrsrl.set` probe runs once per distinct value (clearing to '' doesn't reset it), so change an argument
    to re-run it. `server/tools/device.sh cmd` can fail to press Enter (a WSL vsock error); check `mInputShown` and send keyevent 66 again.
    The hub build's old network grep missed `java.net.http`; use netcheck.sh.
  - **Next (gen 40)**:
    1. Device-check getStats (`npcdef 3108 0`: Man hitpoints 7) and Max Hit against an NPC. R1 if approved (wire `NetScan.refusal`
       into `Host.loadJar` before the DexClassLoader).
    2. When the OBJ_ADD timer fix is deployed: Clue Juggling Timers and Ground Items' despawn timer.
    3. More hub plugins from the ranking (scratchpad ranking.json from gen 38, or refetch). Run netcheck on every one.
    4. Opportunistic: a capture that includes a GE digest fetch (it should hit the sidecar).
- **2026-10-18 (client dev gen 40)**, commits 40c9ed2..(this entry). From here on, docs/client-state.md holds the current state.
  - R1 load-time network gate wired and device-verified (40c9ed2; netscan-check.sh). Network QA run: expected hosts plus
    `dns.google` (system Private DNS validation, now on the expected list). getStats verified (Man hp 7).
  - Hub: 9 pinned and loaded (1d496a2); Decimal Prices breaks chat Enter (open); No Examine can't remove native entries;
    Current World NPEs on world 255. Dropped: dude-wheres-my-stuff (NET-RAW), companion-pet, camera-smoothing, pet-info.
  - tools/device-restore-check.sh (pre/check) replaces the prose end-of-slot checklist.
- **2026-10-18..19 (client dev gens 41-46, then harness task host-menu-entry-removal by cd47)**: moved verbatim from
  docs/client-state.md by task client-docs-trim (2026-10-19). Its "Installed on the device" and "Recent state" sections
  and the open gaps resolved by then:
  - Installed on the device (end of task host-menu-entry-removal, 2026-10-19):
    - Build: client/host-menu-entry-removal's head (master 72d55d2 + the MenuOpened session), built in the cd47 worktree with
      `OPENRUNE=1 LOCAL_AUTH=1 MODULUS=<openrune.sh modulus> tools/repack.sh` (7 `telemetry:` lines; the worktree needs `re`,
      `base.apk` and `split_config.arm64_v8a.apk` symlinked from the main checkout, and the modulus passed explicitly because
      openrune/ isn't checked out there) and installed with `adb install-multiple -r` (Windows adb.exe, `wslpath -w` paths).
    - Plugins dir: the default 3 only (grounditems, opponentinfo, quest-helper). Tester LOGGED_IN at 3219,3222.
    - **Expected drift: a Fish barrel in the tester's inventory slot 15** (spawned gen 44; OpenRune's barrel Destroy does nothing,
      filed as a server task). Leave it (orchestrator, gen 44); destroy it once that fix is live. The restore check's MANUAL line says so.
    - Pre-existing plugin data under `files/.runelite/` (bossing-info, c-engineer-sounds, cache, pyramid-plunder-counter,
      resource-packs-repository) predates gen 40; leave it.
  - **Native menu entry removal from MenuOpened (client/host-menu-entry-removal, device 2026-10-19)**: `Host` posts `MenuOpened`
    inside a menu session (`AndroidMenu.beginSession`), and the order/removal logic presort used is now `AndroidMenu.applyOrder`,
    run after both PostMenuSort (presort) and MenuOpened: text edits are written back, then a permutation or subset of the game's
    entries goes to `Native.menuReorder` (logcat `menu reorder (MenuOpened) [...] of N: true`). Device with hub-no-examine: Genie +
    Overgrown cat menu 10 -> 8 entries (no Examine), Logs = Use/Drop/Cancel, fountain = Walk here/Cancel, "Talk-to Genie" in the
    shortened menu ran Talk-to. Evidence: `re/evidence/host-menu-entry-removal/` (`01-npc-menu.png`, `02-talk-to-after-removal.png`,
    `03-inventory-menu.png`, `04-object-menu.png`, `menu-logcat.txt`, `repack.log`). No network change (host Java only).
  - **Sprite overrides of the canvas size (client/poison-sprite-override, gen 46, device 2026-10-19)**: the game keeps sprites trimmed
    (15x14 heart at 6,6 in a 26x26 canvas, Sprite +0x20..+0x2c, new `Native.spriteInfo`); an override of the canvas size is now cropped to
    the trimmed block at its offset, as desktop draws the whole canvas. Poison's heart override is applied (logcat `26x26 canvas cropped to
    the game's 15x14 at 6,6`), green while poisoned, red after the cure. Evidence: gen 46 scratchpad `g46/poison-on.png`, `poison-off.png`,
    `poison-logcat.txt`. No network change (native read of game memory + host Java).
  - **javax.sound.sampled shim (client/host-javax-sound-shim, gen 46, device 2026-10-19)**: `host/awt/javax/sound/sampled` (AudioSystem,
    Clip, FloatControl, Control, Line, DataLine, AudioInputStream, the two exceptions; `HostClip` plays the file's bytes through
    AudioPlayer/MediaPlayer). Drop Sounds now starts and plays a valuable-drop clip from a real wav, but its pinned download URL is dead
    upstream (404, repo renamed; desktop is affected too), so it saves 404 text as its wavs: **pin dropped** (orchestrator's decision; removed from host/plugins/hub/plugins.txt).
    Network QA PASS (only the approved raw.githubusercontent.com GETs are new; docs/app-telemetry.md). Evidence: gen 46 scratchpad
    `g46/dropsounds-logcat.txt`, `netqa-logcat.txt`, `sockets-b4.txt`.
  - **ConfigManager.getRSProfiles (client/host-config-rsprofiles, gen 46, device 2026-10-19)**: Time Tracking's farming completion
    check no longer throws NoSuchMethodError. The host's one RS profile (`rsprofile.default`) gets desktop's profile keys
    (displayName, type, accountHash) at each login (`Host.postGameState`, before RuneScapeProfileChanged), and `getRSProfiles()`
    scans them as desktop. Device: keys written (`displayName=tester`, `type=STANDARD`); ~90 s of checks, no error. Evidence:
    gen 46 scratchpad `g46/full-b3.txt` (full logcat). No network change.
  - **No second LOGGED_IN after a released rebuild hold (client/rebuild-double-logged-in, gen 46, device 2026-10-19)**: `Host`
    drops a native 30 when the last posted state is already LOGGED_IN (the hold's release replay posted it), logging
    `GameStateChanged LOGGED_IN again: already posted (rebuild released)`. Never seen before or now (qa/client-hostfixes-gen45.md
    review note). Device: login, 6 teleports (Lumbridge <-> Fossil Island) and a `::drop` relog each post exactly one
    LOADING/WorldViewLoaded/LOGGED_IN (CONNECTION_LOST -> LOGGED_IN unchanged); the guard line didn't fire. Evidence: gen 46
    scratchpad `g46/rebuild-guard-logcat.txt`. offsets.md: Client vtable slot 6 `0x421a20` is the draw on doCycle's thread. No
    network change.
  - **Ground items that leave the scene despawn (client/scene-item-despawn, gen 46, device 2026-10-19)**: `SceneObjects.refresh`
    posted no ItemDespawned for an item whose tile is outside the new base after a rebuild, so Ground Items counted a dropped
    item twice after a teleport round trip (qa/client-hostfixes-gen45.md obs. 1). Those items now despawn on a tile with their
    old world location and item layer (local location null). Device: Bronze dagger dropped at 3219,3222, two round trips
    Lumbridge -> Fossil Island (3686,3870) -> Lumbridge: label "Bronze dagger (GE: 101 gp) (HA: 6 gp)" with no count, one
    LOADING/WorldViewLoaded/LOGGED_IN per teleport, no exceptions. Evidence: gen 46 session scratchpad `g46/`
    (`despawn-after-roundtrip.png`, `despawn-after-roundtrip2.png`, `scene-item-despawn-logcat.txt`). No network change.
  - **Rebuild LOADING waits for the new scene base (client/herbiboar-after-teleport, gen 45, stacked on client/host-prompt-enter;
    device 2026-10-19)**: OpenRune's teleports are 30 -> 25 -> 30 and the host posted LOADING at 25 with the old scene base, so
    getMapRegions (from the base) gave the old regions and Herbiboar's area check on LOADING failed. `Host` now holds a 25 seen
    while logged in until the base changes, then posts LOADING (scene objects forgotten first), WorldViewLoaded and LOGGED_IN in
    one cycle (`GameStateChanged LOADING (scene rebuild)` in logcat); same-base rebuilds get the pair after 50 cycles. Device:
    after `::tele 3686 3870 0` from Lumbridge, core Herbiboar draws the start and trail tiles and Herbi AFK its path line (scene and
    minimap). A `setGameState(LOADING)` reload (probe `reload 0 0`) shows no native 25 to the host at all, so it posts no LOADING
    (unchanged by this fix; desktop would). Evidence: `g45/herbiboar-after-tele.png`, `g45/herbiafk-trail-line-after-tele.png`,
    `g45/herbiboar-tele-logcat.txt`. No network change.
  - **Soft keyboard Enter for KeyListeners (client/host-prompt-enter, gen 45, device 2026-10-19)**: the game submits a chatbox
    input or prompt from its hidden EditText's editor action (IME_ACTION_DONE -> `com.jagex.android.t.a` -> key 66 through JNI
    `ru.hx`/`ru.wv`), which never passes the Window.Callback; on the emulator a hardware Enter's down also becomes that action (only
    its up reached KeyRouter). New `host/.../PromptEnter.java` wraps the listener when the EditText takes focus: VK_ENTER for
    KeyListeners first (KeyRouter.editorEnter, client thread, in order with held keys), then the game's submit once. Device:
    Decimal Prices' Withdraw-X "1.5k" withdrew 1500 (deposited back); chat "Gen45 once" with the keyboard shown sent once.
    Evidence: gen 45 session scratchpad `g45/` (`decimal-withdraw-1500.png`, `chat-enter-once.png`, `prompt-enter-logcat.txt`).
    No network change (host Java only).
  - **v0.1.0 release gate PASS at 09c05c9** (qa/client-0.1.0.md; notes draft qa/client-0.1.0-notes.md).
    `THIRD_PARTY_NOTICES.md` (repo root) gives the notices and licence texts for everything third-party in libosrsrl.so and
    its host.jar (Dear ImGui + stb + ProggyClean, the RuneLite client subset/API/http-api, the host's Maven jars from
    host/build.sh, the client jar's shaders and CC-licensed fonts) and says the rest of the APK is Jagex's app. Releases
    attach it (`tools/release-dev.sh`). When host/build.sh's jar list or `native/third_party` changes, update the file.
  - **Hub plugins after the cutover (client/hub-after-cutover, gen 44, device 2026-10-18 against integration with fix/75 + fix/76;
    rows in plugin-host.md, evidence in the gen 44 session scratchpad `g44/`)**: runs: Clue Juggling Timers (InfoBox countdown,
    warning, removed on despawn), Ground Items' despawn timer (`groundItemTimers=TICKS`), Coal Bag ("5" after Fill, "0" after
    Empty), Cannon Highlighter (hit grid + Single/Double/Unhittable labels; 1 of 3 set-ups drew nothing), Did I Compost
    (needs-compost icon after Inspect). Server-blocked (reported to main, filed as server tasks): dropped items all despawn after
    200 ticks (OSRS: 300, clues 61 min); Fish Barrel's Check text/interface and Destroy; Clue Steps' "Check steps" text lacks the
    tier; no compost on OpenRune. Host: Decimal Prices (the "." works, the submitting Enter never reaches KeyListeners, gap 6);
    core Herbiboar draws nothing after a `::tele` into Fossil Island from elsewhere but works after a relog there (gap 7);
    Herbi AFK draws no path line even then (cause not isolated). No build, no network change.
  - **Ghidra project rebuilt (client/ghidra-reanalyse, 2026-10-18)**: `re/ghidra` is now the re-analysed copy (auto-analysis,
    `tools/ghidra_scripts/FixSwitches.java` to complete jump tables auto-analysis left unresolved or partial, auto-analysis
    again). Functions 24957 → 24961, instructions 1954259 → 2057546, instructions outside any function 8944 → 36; all 217
    function entries offsets.md names are unchanged and 18 of its addresses that were undisassembled are now inside functions.
    The old project had no user-made symbols, comments, signatures or data types, so nothing was carried over; it is kept
    at `re/ghidra-old` until main deletes it. New `Callers.java` (reverse skill table); the objdump whole-binary check stays.
  - **World entry thread safety (client/world-entry-thread-safety, gen 43; merged in master, merge gate PASS at b4b11a4)**: `Native.worldEntry` no longer walks the game's
    world list off the game thread. The game frees and rebuilds that list (new address each time) whenever the login-screen
    world switcher opens, on the game thread (parser `0x4320dc`: callers `0x412e78` and the login handler `0x41615c`, both only under doCycle, and
    the CS2 world list ops 6500-6531 `0x4c2544`; offsets.md "World list").
    `client_thread::world_entry` serves other threads a copy of the current world's entry made in after_cycle (re-read on a
    world id or list change, else every 25 cycles); on the client thread it reads live. Logcat `osrsrl: world list: N entries
    at 0x...` marks each reload (a new address every time). Device (2026-10-18, World Hopper + Current World + Quick Prayer
    Preview loaded): 50 open/close cycles of the switcher (53 reloads in one process), then a relog (force-stop, 27 more
    cycles while the server released the session, Play Now): no crash; the off-thread "current world 255 from the world
    list" line came from a WorldService thread; Current World overlay "World 255 / Test server" and World Hopper's "255 0
    Test server" row unchanged; Quick Prayer Preview's held-orb tooltip shows both icons. No network change. Evidence: the
    gen 43 session scratchpad (`stress-logcat.txt`, `relog-logcat.txt`, `current-world-overlay.png`, `worldhopper-list.png`,
    `qpp-hold.png`, `relog-overlay.png`).
  - **Current World runs (merged in master, gen 42)**: the host replaces RuneLite's WorldService
    (`host/src/net/runelite/client/game/WorldService.java`, same api.runelite.net fetch and schedule as 1.13.1) and, when the
    fetched list lacks `client.getWorld()`, adds that world built from the game's own slr.ws world list (global `0xace6c0`,
    `game::world_entry`, offsets.md "World list"), falling back to Client id/flags/host. If the api fetch failed the result
    holds just the current world. No new requests. Device: no NPE; overlay "World 255 / Test server" with overlayActivity on.
  - **Gen 41 re-check of the 9 gen 40 hub plugins** (rows in plugin-host.md, evidence in the gen 41 session scratchpad `g41/`):
    runs: Monster Stats (panel opens from "Stats Man"), Multicolor Highlights, Pickpocket Guardian, Unpotted Reminder,
    Ground Item Sounds (MediaPlayer clip on a high-value drop), Quick Prayer Preview (after the fix below). Decimal Prices:
    starts, conversion not confirmed. No Examine: no effect (gap 2). Current World: NPE (gap 3).
  - **BufferedImage drawing without dispose() (gen 41, merged in master)**: `CanvasGraphics.forImage` used to copy drawing back
    into the image only on `dispose()`; plugins that never dispose (Quick Prayer Preview's `getGraphics().drawImage`) got blank
    images. Now the image keeps a live mutable Bitmap: each draw marks it dirty, pixel reads/draws of the image sync it back.
  - **R1 network gate (40c9ed2, device-verified)**: `Host.loadJar` runs `NetScan.refusal` on the cache copy before the
    DexClassLoader. NET-RAW jars are refused with `W osrsrl-host: netscan: refused <jar>: ...`. Vetted:
    `hub-loot-lookup` (raw refs only in bundled jsoup; approved), `devtools` (adb-only local server).
    Test override: `setprop debug.osrsrl.netallow <jar-name>`. Off-device check: `host/plugins/hub/netscan-check.sh <jars>`.
  - **Build-time netcheck**: `host/plugins/hub/netcheck.sh` (run by hub build.sh): NET-RAW stops the build unless `ALLOW_NET=1`
    (orchestrator's OK). NET-OKHTTP plugins need Network QA, and new outbound hosts need the orchestrator's OK.
  - **Network QA (v0.1.0 gate, 22:22-22:27)**: only the expected hosts (`dns.google` is an expected system lookup). With all hub
    plugins loaded, one GitHub socket (185.199.111.133:443), most likely C Engineer: Completed's approved sound GETs
    (docs/app-telemetry.md). No GE digest fetch captured yet.
  - Resolved open gaps (numbers as in client-state.md "Open gaps" at the time):
    2. **Removing native menu entries: fixed (client/host-menu-entry-removal, 2026-10-19).** `MenuOpened` now runs in a menu
       session, so a plugin's `setMenuEntries(shorter)` there persists and `AndroidMenu.applyOrder` shortens or reorders the
       game's open menu as presort already did for PostMenuSort (No Examine hides Examine on NPCs, inventory items and objects;
       docs/plugin-host.md row "No Examine"). Entries with submenus still can't be dropped (`game::menu_reorder`).
    4. **OBJ_ADD timers: done** (fix/76 live; host reads them). The durations are the server's (200 ticks for every player
       drop), filed as a server task.
    6. **Prompt Enter for KeyListeners: fixed (client/host-prompt-enter, gen 45).** The soft keyboard's Enter is an editor action
       on the game's hidden EditText, not a key event; `PromptEnter` wraps that EditText's listener (docs/plugin-host.md row "Soft keyboard
       Enter"). Decimal Prices converts "1.5k" to 1500.
    7. **Herbiboar after a teleport: fixed (client/herbiboar-after-teleport, gen 45).** A rebuild's LOADING now waits for the new
       scene base, so getMapRegions answers the new regions; Herbi AFK draws its path line too.
    8. Host API gaps found by the v0.1.0 gate: fixed. `javax.sound.sampled` shimmed (client/host-javax-sound-shim, gen 46; Drop Sounds' own
       download URL is dead upstream). `ConfigManager.getRSProfiles()`: fixed (client/host-config-rsprofiles, gen 46).
- Device protocol (2026-10-15, replaces the status file): see "Working with the server session".

## Layout

| Path | What |
|---|---|
| `native/src/main.cpp` | `JNI_OnLoad`: hooks, `game::init`, `client_thread::install`, agent, host |
| `native/src/game.{h,cpp}` | Offset table per build + accessors. Add new offsets here, nowhere else |
| `native/src/client_thread.{h,cpp}` | doCycle hook, task queue, event derivation (state, tick, varp diff, NPC diff) |
| `native/src/jni_bridge.{h,cpp}` | Unpacks/loads host.jar, registers `net.runelite.host.Native`, forwards events |
| `native/src/overlay.cpp` | Debug overlay (`setprop debug.osrsrl.overlay 0` hides it) |
| `native/src/agent.cpp`, `tools/agent.py` | Memory agent (`setprop debug.osrsrl.agent 1`, restart game) |
| `native/src/net_trace.*` | Owned by the server session (socket logging) |
| `host/src/net/runelite/host/` | Host: `Host` (entry/events), `AndroidClient`, `AndroidPlayer`, `AndroidNPC`, `AndroidWorldView`, `NpcCache`, `HostEventBus`, `Native` |
| `host/src/net/runelite/client/plugins/HostPluginManager.java` | Starts plugins (same package as `Plugin` to set `injector`) |
| `host/awt/` | java.awt shim (geometry, Color, Font, BasicStroke, Graphics/Graphics2D API) |
| `host/src/net/runelite/host/CanvasGraphics.java`, `OverlayRenderer.java` | Graphics2D over android.graphics.Canvas; overlay layout and frame hand-off |
| `host/src/net/runelite/client/ui/` | Same-name replacements for RuneLite's `OverlayManager` and `FontManager` |
| `host/compile-stubs/` | Compile-only `LambdaMetafactory` (android.jar lacks it; d8 desugars lambdas) |
| `host/gen/GenStub.java` | Generates `<Iface>Stub` classes that throw for every abstract method |
| `host/gen/GenHeadless.java`, `host/tools/awt_refs.py` | Generate a headless, stateful java.awt/javax.swing for everything the client + `re/hub/*.jar` reference (JDK hierarchy, property fields, listener lists, Container children); regenerated by build.sh when inputs change |
| `host/src/net/runelite/client/...` | Same-name replacements: EventBus, ConfigManager, ClientThread, OverlayManager, FontManager, Hooks, PluginManager, ClientToolbar, ItemManager, SpriteManager, ChatMessageManager, ChatboxPanelManager (keep the delete list in build.sh step 3 in sync) |
| `host/src/net/runelite/host/Sidebar.java` | Sidebar model (flattened Swing tree), tap dispatch, touch router (Window.Callback proxy) |
| `host/plugins/example/build.sh` | Builds the Tick Info example (`re/host/plugins/tickinfo*.jar`) |
| `tools/plugin-test.sh` | Push plugins, restart the game, Play Now, print host logs |
| `host/build.sh` | Builds `re/host/host.jar`: compiles host + stubs + shim with `-source 8 -bootclasspath android.jar`, adds the RuneLite client classes it reuses (list in step 3), dexes, adds fonts. Interfaces to stub are in its `for iface` loop |
| `tools/dex-plugin.sh` | Converts any RuneLite plugin jar into a host-loadable jar |
| `tools/ghidra_scripts/` | Headless helpers: `Decompile.java`, `Xrefs.java`, `StringXrefs.java` |

`re/` is git-ignored and holds inputs and outputs: `osclient.so`, the Ghidra project,
decompiled functions (`re/decomp/<ghidra addr>.c`), runelite jars and Maven deps
(`re/runelite/`, refetch from repo.runelite.net / Maven Central, versions in `host/build.sh`),
build outputs, and the debug keystore.

## Build, install, test

```
host/build.sh                                    # Java host → re/host/host.jar
cmake -S native -B re/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$HOME/Android/Sdk/ndk/android-ndk-r27d/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release   # once
ninja -C re/build                                # libosrsrl.so (embeds host.jar)
LOCAL_AUTH=1 SERVER=10.0.2.2:8080 ADB=/mnt/c/Users/Alex/AppData/Local/Android/Sdk/platform-tools/adb.exe \
    ANDROID_SERIAL=emulator-<port> tools/repack.sh --install
tools/dex-plugin.sh in.jar re/host/plugins/x.jar
adb push re/host/plugins/x.jar /sdcard/Android/data/com.jagex.oldscape.android/files/plugins/
adb shell am force-stop com.jagex.oldscape.android; adb shell monkey -p com.jagex.oldscape.android -c android.intent.category.LAUNCHER 1
adb shell input tap 1197 540                     # "Play Now" (Agent_Phone, 2400x1080)
adb logcat -s osrsrl osrsrl-host 'rl.*'          # plugin logs use tag rl.<Class>
```

Gotchas:
- Under WSL use Windows `adb.exe` (run `adb kill-server` in WSL first). Your own Agent_Phone emulator is `emulator-<port>`
  (`ANDROID_SERIAL`).
- Run `tools/repack.sh` bare. Don't prefix `cd … &&` or append a pipe, or the permission rule
  won't match.
- Sign-in: with `LOCAL_AUTH=1` builds the test server is the OAuth provider. If the app is
  signed out: "Sign in with Email / Username", close the privacy screen (X), then Play Now
  (1197,540). Without LOCAL_AUTH the app wants the real Jagex login, which only the user may do.
  `repack.sh --install` uses `install -r`, so reinstalls keep the login.
- Shared device: never touch adb without the lock (see "Working with the server session").
  Server container restarts log the client out; after one, run the login steps again.
- Chat commands over adb: `input text` (spaces as `%s`), then `keyevent 66`. Enter only works
  while the soft keyboard is shown (`dumpsys input_method | grep mInputShown`); tap the keyboard
  button (90,220) first if it isn't. `::talk` opens a dialogue (group 231), `::obj id n` drops an item. `::tele x y plane`, `::pos`, `::npc id`, `::npcsay text` and `::npcanim id` are useful.
- The example plugin logs a lot; run `adb logcat -G 16M` or the agent token scrolls out of the buffer.
- Ghidra addresses = ELF vaddr + 0x100000. `docs/offsets.md` uses ELF vaddrs.

## How to find the next thing

1. Find the packet handler or function in `re/decomp/00541dd4.c` (server packet dispatcher; the
   case numbers are Android opcodes, mapped in `docs/android-protocol.md`) or via string xrefs
   (`re/string_xrefs.txt`).
2. Decompile callees: `~/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless re/ghidra osclient -process osclient.so -noanalysis -readOnly -scriptPath tools/ghidra_scripts -postScript Decompile.java 0x<ghidra addr>`.
3. Confirm live with `tools/agent.py` (or a short script importing `Agent` from it): read,
   snapshot, change the game state (walk, open an interface), snapshot, diff.
4. Add the offset to `kBuilds` in `game.cpp`, then an accessor. If the data is a container the
   game rebuilds, snapshot it on the game thread in `client_thread.cpp`.
5. Expose it through `Native` (JNI in `jni_bridge.cpp`), implement it in the `Android*` class,
   add the interface to `host/build.sh`'s stub loop if it's a new type, and record it in
   `docs/offsets.md` and `docs/plugin-host.md`.

Calling game functions directly works (the varbit lookup `0x5d0040` is called from our code),
as long as the call happens on the game thread.

## Working with the server session

A separate Claude session develops `server/` (it hands over to fresh agents too; find the
current one with ListAgents); an orchestrator (the main session) coordinates both.
**Device lock** (`tools/device-lock.sh`): ask the orchestrator for "OK device"; it runs
`grant client`; then run `tools/device-lock.sh take client <what>` as its own step and touch adb
(installs, input, logcat, the memory agent) only if it prints "taking device" (otherwise DENIED).
Grants are single-use. When done: `tools/device-lock.sh free client` and tell the orchestrator.
`tools/device-lock.sh status` shows the holder. Never write `/tmp/osrs-device-status` directly
and never wait on it. Container restarts go through the orchestrator only.
File ownership: client = native/ (except net_trace.*), host/, tools/agent.py,
tools/dex-plugin.sh, tools/plugin-test.sh, tools/ghidra_scripts/, docs/offsets.md,
docs/plugin-host.md, docs/client-handoff.md; server = server/, tools/repack.sh,
tools/patch_server.py, docs/android-protocol.md, docs/server-handoff.md. Send it the
protocol gaps plugins hit (it has implemented dialogue, ground items, bank, OP* packets).
`server/tools/device.sh cmd '::talk'` types chat commands reliably.

## Suggested next steps

1. Verify (built in commit after e5487c9, not yet run): dialogue-option highlight (QH reads
   chatmenu 219:1's CC_CREATE children: `Widget.getDynamicChildren()` now returns the game's
   children, text/geometry/hidden), QH's NPC arrow after the extended-tile-settings fix (was an
   AIOOBE index 118 in `Perspective.getCanvasTileAreaPoly`).
2. Sheep Shearer with Quest Helper (server has it deployed): object highlights (spinning wheel,
   `GameObject` click boxes), NPC "Shear" steps, item counts (20 balls of wool).
3. Widget item ids/quantities (inventory items are CC_CREATE-like children of 149:0 in RuneLite;
   find the item id/quantity fields of the game's widget) for QH's inventory highlights.
4. Sidebar text input (QH search box), then minimap geometry (`getMinimapLocation`, QH arrows),
   NPC animation/orientation/interacting (currently -1/0/null), player hulls.
5. Small: the `NullPointerException ... WorldView.getPlane()/isInstance()` seen once at login
   (QuestHelperMinimapOverlay before the world view exists; guard `Host.worldView()` users).
