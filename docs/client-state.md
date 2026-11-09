# Client state

Short current-state doc for the client track (docs/workflow.md "State"). Rewrite it; don't append.
History: docs/client-handoff.md (per-gen log; the last entry holds gens 41-46 and host-menu-entry-removal, moved here
from this file). Tasks and blockers: `python3 ~/osrs-harness/hd.py status`. Last rewrite: 2026-10-31, client dev (client-login-index-log: the login screen pages); before that 2026-10-30, client dev
(the change that made the Agent_Phone emulator the only device in docs and scripts; the adb tools require `ANDROID_SERIAL`;
`tools/plugin-test.sh` signs in through `~/osrs-harness/bench/emulator_login.py`), merged with client-devtools-menu-actions.

## What this is

- The official OSRS Android app (build 241.4) repacked by `tools/repack.sh` to load `libosrsrl.so` (native/), which hooks
  the game thread and embeds the Java plugin host (host/). The host runs unmodified RuneLite plugins on ART: core
  plugins from the bundled client jar, Plugin Hub plugins built from source (`host/plugins/hub/build.sh`).
- Tested on each agent's own Agent_Phone emulator (Android Studio AVD, Medium Phone profile, x86_64 API 37 image with
  arm64 translation; harness skill `test-instances`; adb `emulator-<port>` in `ANDROID_SERIAL`; the app's uid is assigned at install, read it with `dumpsys package`) against OpenRune (game `10.0.2.2:43594`,
  sidecar `10.0.2.2:8080` serving jav_config, digest and payments param 2; LOCAL_AUTH sign-in at Central's http
  `10.0.2.2:8084`, jav_config params 11, 20, 22 and 28: discovery, oauth2/auth in Chrome, oauth2/token, game-session
  sessions, accounts and tokens, then the login as the signed-in account).
- API coverage and each plugin's status: docs/plugin-host.md. Addresses: docs/offsets.md (ELF; Ghidra = ELF + 0x100000).
- Privacy patches and Network QA: docs/app-telemetry.md (read it before any build that touches networking).

## Login screen pages

The host logs `osrsrl-host: LoginIndex <n>` once on entering LOGIN_SCREEN and on each change of the page while there
(`Host` CLIENT_TICK, `Native.loginIndex`); device and desktop login tools wait on it. Read on Agent_Phone (2400x1080
landscape, canvas = screen points) on a fresh install (client-login-index-log, evidence/client-login-index-log):

| n | page | buttons (bounds) |
|---|------|------------------|
| 0 | welcome, signed out | Sign in with Google [905,475][1495,558]; Sign in with Email / Username [905,575][1495,658] |
| 12 | terms dialog (after either sign-in button on a fresh install, not at launch) | Accept [882,603][1184,686]; Decline [1213,603][1515,686]; its text links (terms of use, privacy policy, EULA) are at y 455-525 |
| 30 | waiting on the browser sign-in (AppAuth opens Chrome at Central's 10.0.2.2:8084/oauth2/auth; Chrome's first run asks "Use without an account") and briefly after Play Now, before LOGGING_IN | none on the canvas |
| 10 | welcome, signed in | Play Now [973,475][1422,612]; Sign out [1048,645][1350,727] |

On every page the footer links (Terms and Conditions, Privacy Policy, Software Licence, Do Not Sell) are at y 960-1012
and World / Tap to switch at [450,925][655,995]: never tap there. The api-probe at this build (logged in at spawn,
no run set-up) is 91.8% (1149/1252): the 8 Widget listener setters (click, hold, release, mouse over/leave/repeat,
scroll wheel, dialog abort) run from the game's own dispatch and pass (plugin-host.md); openInterface,
setAllWidgetsAreOpTargetable, setInventoryDragDelay, PlayerComposition.setHash, Scene.removeGameObject/removeTile and
Widget.setForcedPosition write the game's own state (offsets.md "Game-state writes") and pass; the 4 playSoundEffect
rows pass: the positional forms queue an area sound (AreaSoundEffectPlayed at the asked tile, range and delay) and
(id, volume) plays at the sound-effect setting, or the given volume when muted, raising no event, as desktop; the
GraphicsObject and fight-dependent rows are unreached without the run set-up below.

## Releases and tooling

- **v0.1.0**: release gate PASS at 09c05c9 (qa/client-0.1.0.md; notes draft qa/client-0.1.0-notes.md). Everything since
  (gens 41-46, host-menu-entry-removal) is on master on the dev's check.
- Android Studio emulator (Windows SDK, AVD `Agent_Phone`, x86_64 API 37 image with arm64 translation) runs the single
  APK: `env -u HOME emulator.exe -avd Agent_Phone -read-only -gpu swiftshader_indirect -port <free even port>` (the first sign-in opens Chrome: "Stay signed out"; `pm grant com.android.chrome android.permission.ACCESS_LOCAL_NETWORK`
  or Chrome shows ERR_LOCAL_NETWORK_PERMISSION_MISSING for 10.0.2.2:8080; close the "Your Privacy" dialog with its X; a
  shell-pushed `files/plugins` dir needs `chmod 777` or the host loads 0 plugins; `adb uninstall com.jagex.oldscape.android`
  before installing, as a guard). Verified 2026-10-24: single APK installs, hooks load, sign-in and LOGGED_IN.
- **The release artifact is ONE universal APK**, `re/out/osrs-runelite.apk` (client-single-apk, 2026-10-24): `tools/repack.sh`
  builds the base with the split-required flags removed (`android:requiredSplitTypes`/`splitTypes` on `<manifest>`,
  `isSplitRequired`, the `com.android.vending.splits.required` meta-data; the decoded manifest is text, so a copy of
  `re/apk/base` is edited, no AXML tool), folds in every `lib/arm64-v8a/*` of the arm64 split (patched game lib, hook
  lib, crashlytics), embeds `assets/THIRD_PARTY_NOTICES.md`, aligns and signs with `re/debug.keystore`. Install:
  `adb install osrs-runelite.apk` (`tools/repack.sh --install`). arm64-v8a only, as the split was; a build without the
  META-INF/services entries crashes at sign-in (kotlinx Main dispatcher), so only signature files are dropped. The split
  pair `re/out/{base,split}.apk` is still built (`--install-split`) but is not released. Failure modes: an installed
  official/Play copy (other signer) gives `INSTALL_FAILED_UPDATE_INCOMPATIBLE`: uninstall it first; a device with
  no arm64 ABI cannot run the native libs. `tools/release-dev.sh` attaches the single APK and `THIRD_PARTY_NOTICES.md`.
  The release gate re-runs on this exact artifact.
- **API probe** (`tools/api-probe.sh [out-dir]`, goal client-api): with `ANDROID_SERIAL` set to your own instance running
  this repo's build, logged in, it builds `host/plugins/api-probe` (jar + `api-methods.txt`, the denominator from
  `ApiList.java`: every abstract method of every net.runelite.api interface in runelite-api-1.13.1.jar, 1286, less the
  5 caller-implemented interfaces = 1253), pushes it, loads it into the running host with `debug.osrsrl.set 'load
  api-probe.jar <n>'` (no restart, no login), waits for `files/api-probe.tsv`, pulls it, removes the jar and report by
  name and prints `pass N / 1253 = P%`. The probe seeds live instances (local player = Actor, a root widget with no
  dynamic children, inventory container, menu, an NPC with a loaded model, the scene's objects) and registers every value a getter
  returns; per row: pass, throw, inert (resolves to a *Stub/*Bag/Inert path), mismatch (setter read back otherwise),
  unchecked (native void action), unreached (no live instance), skipped (the state it needs is missing; the
  reason is the evidence). Setters: altered value, read back, restored. The `native` column comes from
  `host/gen/NativeCalls.java` (host methods reaching `Native.*`), which host/build.sh writes into host.jar as
  `api-native.txt` beside `osrsrl-build.txt` (`git describe` of the host build, the report's `# build:`). The probe
  stays loaded until the app restarts (unregistered); it adds one chat line "api-probe: addChatMessage". Model rows run
  on the live NPC's first model (`loadModel`/`loadModelData` of its composition's model id; model 0 isn't in OpenRune's
  cache) and `applyTransformations` on that NPC's idle sequence (evidence: vertices moved; a run whose seeded NPC gives loadModel a model the cache lacks returns null there and fails applyTransformations: re-run); `Mesh.scale` uses 128 (1:1)
  so the live model keeps its geometry for the Model rows. 2026-10-25: 60.9% (765/1256) at aba2ced, 66.3% (833/1256)
  after client-api-model (every Model/Mesh/ModelData/AABB row and applyTransformations pass, no pass lost), 70.8%
  (889/1256) after client-api-scene-tile (Scene terrain, Tile paint/model, createSceneTilePaint and every
  SceneTilePaint/SceneTileModel row pass, no pass lost), 75.9% (953/1256, every pass of the three reports kept) after client-api-render (probe effect checks for the
  Rasterizer draws and a size-matched image for `SpritePixels.toBufferedImage`). After client-api-cache-config (report and diff in its evidence dir): every
  cache, index and config row listed in that change passes, no pass lost; the probe checks `NodeCache.reset` and
  `resetHealthBarCaches` by the game cache's entry count, the IndexDataBase rows on the first group with files, and seeds
  NpcOverrides from any NPC that has one; host objects that override toString describe themselves in the evidence. The
  health bar check needs a health bar seen first (attack anything): with an empty cache it reads `0 -> 0`. After client-api-actor-anim, 81.4% (1023/1256, every pass of main's and the branch's reports kept; report and diff in its evidence dir): the Actor
  animation/pose/spot anim setters, Player.setSkullIcon, Renderable/Node on actors and every ActorSpotAnim row pass,
  no pass lost; the probe adds spot anim 199 with a 10000-cycle delay in slot 7 for the ActorSpotAnim rows and removes
  it at the end. After client-api-client-state, 84.7% (1064/1256 on the merge with client-api-actor-anim, every pass of main's and the branch's reports kept; report and diff in its evidence dir): every session, settings and menu row listed in that change passes, no pass lost; the probe checks `getSocketFD` by the fd's peer against `getWorldHost` and the menu draws by the pixels drawn into the frame copy (a long-press menu open, `input swipe x y x y 900` on the scene; with none open they pass with "no menu open"); `getWorldList` is null until the login screen's world switcher has loaded the list, and the MidiRequest rows need a song playing (OpenRune plays none by itself here: play an unlocked track, e.g. "Alone", from the music tab, which sets the music mode to single). The probe jar must not reference java.net classes: the host's netscan refuses it. After client-api-probe-setters, 86.8% (1090/1256) (report and diff in its evidence dir, no pass lost): no row is skipped for "no getter" or unchecked as a native hook any more. Each setter or hook reads its effect where it lands: getters under another name (`setActionFrame` -> getAnimationFrame, ParamHolder get*Value, `getVarbitValue(int[], int)`, getSubOps), the SoundEffectPlayed/AreaSoundEffectPlayed events (read on the next tick), game fields (`Native.clientInt` CHAT_CYCLE 3 = Client +0x413e6c and CHANGED_SKILL_COUNT 4 = +0x413de4; `Native.geSearch()` = {count, index, ids...} of the GE search results), host state (WidgetListeners kinds; a setter with no getter and no check is inert when none of the instance's fields change) or a probe-owned slot (spot anim slot 9, children created on the first visible root, the scene's ambient sounds cleared and re-added in one cycle, so some must be in range: Lumbridge's goblin field has 41). `setWorldMapPositionTarget` moves the map only while it is open: open the world map (globe by the minimap) before loading the probe; no long-press menu is open then, and the menu draw rows pass with "no menu open". Real gaps it reports: `setInventoryDragDelay` leaves the inventory items' drag dead time (mismatch); the 8 STORED widget listeners (click, hold, release, mouse over/leave/repeat, scroll wheel, dialog abort) are kept but never run (inert); `setAllWidgetsAreOpTargetable`, `PlayerComposition.setHash` and WidgetBag's `setForcedPosition` change nothing, and 17 Client setters are ClientStub throws (inert). After client-api-probe-reach, 88.5% (1109/1253 on the merge with client-api-probe-setters, no pass of main's or the branch's reports lost; report and diff in its evidence dir; ClientConfiguration is excluded like the other caller-implemented interfaces: RuneLite's RSAppletStub implements it, so the denominator is 1253): the probe registers the first hitsplat and graphics object from HitsplatApplied/GraphicsObjectCreated (only as their own interface), a text widget's font for FontTypeFace, the world map's first region with icons for WorldMapRegion/WorldMapIcon, hands overlay-style arguments a host Graphics2D (a BufferedImage's) and Color.RED, seeds the NPC whose first model the game's store holds (applyTransformations stable), and round-trips GraphicsObject.setFinished through finished(). Its run set-up, all before loading it, in Lumbridge's goblin field (ambient sounds in range): a fight running (`::item scythe_of_vitur`, wield it, `::npc 1000 poh_combat_dummy_npc` beside you and attack it: hitsplats every swing and the scythe's map spotanim per swing; the probe waits for a hitsplat while the player is interacting), a song playing and the world map open while it runs (globe by the minimap). Afterwards close the map, re-wield the tester's staff and drop the scythe. Still unreached: clan/friends chat (server change server-friends-chat-clan-channels) and world entities (server-world-entities). After client-api-projectile, 90.8% (1138/1253 on the merge with client-api-probe-reach, no pass of main's or the branch's reports lost; report and diff in its evidence dir): `Client.createProjectile` (both forms) and `WorldView.createProjectile` spawn into the game's list (the probe sends spotanim 100 from the local player's tile to 3 tiles east for 3000 cycles; evidence: listed in getProjectiles with the given fields, then moved), and every Projectile row runs on the first one; finish ends them (setEndCycle now). After client-api-host-gaps-reach, 92.2% (1155/1253 on the merge with client-api-projectile, no pass of main's or the branch's reports lost; report and diff in its evidence dir): every DynamicObject, HealthBarConfig, Script, ColorTextureOverride, PendingLogin row and FontTypeFace.getBaseline/drawWidgetText pass. The probe registers the first animated loc renderable it finds in the scene (DynamicObject), the HealthBarConfig of PostHealthBarConfig and the Script of ScriptCallbackEvent, and checks setPadding by the game type's field and drawWidgetText by the pixels it lights. Instances that appear only while it runs are waited for when named in `debug.osrsrl.probewait` (comma-separated, e.g. "PendingLogin,Script", set before loading it): once the other rows are done (the log stops), close the world map, open the skills tab (its "Total level" settext raises skillTabTotalLevel: Script) and change a listed friend's presence: the tester's list holds `desktop`, whose private chat is "Friends" (the tester is not on its list, so the server shows it offline, as official); on a desktop window logged in as `desktop` (OpenRune resets its private chat to "Friends" at each login), set its private chat to "Show all" while the probe waits (a login notice: PendingLogin) and back to "Friends" afterwards. Both sessions idle out after 5 minutes without input, so run the set-up and the waits without pauses. ColorTextureOverride passes on the composition's createColorTextureOverride (short[0]): OpenRune sends no obj customisation, so the game-read ones stay null. After client-api-probe-effects, 92.6% (1160/1253, no pass of main's report lost; report, diff, account and network check in its evidence dir): no row is skipped any more but `Client.stopNow` (skipped: it ends the client before the report is written). The other ClientStub/SceneStub methods are called and report inert (hopToWorld, openWorldHopper, changeMemoryMode, openInterface, setUsername/Password/Otp, GameEngine initialize/resizeCanvas/setConfiguration/unblockStartup, Scene removeTile/removeGameObject: the mobile client never hops, so the session and world 255 stay); changeWorld only logs (inert, world unchanged); runScript and ScriptEvent.run run quest_status_get (4029, reads varps) and read its int stack. The rows that change what the server or the session sees run last, one per tick, each once the previous one's check is done: closeInterface closes the open modal client-side (desktop's behaviour), menuAction walks onto the player's own tile through the game's menu (MenuOptionClicked WALK at that scene tile; OpenRune's move handler closes the modal server-side too and stops the fight), setGameState(LOADING) reloads the scene (a ground object the probe removed is back on the next tick; logcat `scene load: map arrays copied`). closeInterface needs "modal" in `debug.osrsrl.probewait` (else skipped): it then waits for a mode-0 sub-interface; open equipment stats (worn equipment stone, then "View equipment stats", 84 + side 85) once it logs `closeInterface waits`. Run set-up as above plus "modal" (probewait "PendingLogin,Script,modal"); with the probe's log cleared first, wait for its `seeded` line before taking the log's pause as "rows done" (a stale log closes the map too early). The `desktop` account must not be online elsewhere (use a free desktop instance, `desktop.sh free`). After client-api-widget-fields, 93.2% (1168/1253, no pass of main's report lost; report and diff in its evidence dir): Widget type, content type, target priority and drag parent are the game widget's (offsets.md "Widget fields (CC_SET)") and getItemQuantity is the raw field as on desktop; the probe hands setDragParent the widget itself (a null -> null round trip shows nothing). The menuAction walk row reads mismatch when the player is still stepping (a dummy spawned on the player's tile: wait until the attack has settled the player on its tile before loading the probe). Probe `debug.osrsrl.set 'terrain <scene x> <scene y>'` logs
  that tile's underlay/overlay/shape, paint and model and the scene's draw distance, min level, tint and skybox.
- `THIRD_PARTY_NOTICES.md` (repo root) covers everything third-party in libosrsrl.so and host.jar (Dear ImGui + stb +
  ProggyClean, the RuneLite client subset/API/http-api, host/build.sh's Maven jars, the client jar's shaders and
  CC-licensed fonts); the rest of the APK is Jagex's app. Releases attach it (`tools/release-dev.sh`). **Update it when
  host/build.sh's jar list or `native/third_party` changes.**
- Ghidra: `re/ghidra` is the re-analysed project (2026-10-18: auto-analysis, `tools/ghidra_scripts/FixSwitches.java` for
  unresolved jump tables, auto-analysis again; 24961 functions, 36 instructions outside functions; offsets.md's entries
  unchanged). No user symbols/comments in it. The old project stays at `re/ghidra-old` until main deletes it.
  `Callers.java` is in the reverse skill's table; the objdump whole-binary check stays.

## Installed on the device (last recorded: end of change hub-clue-steps-tier, 2026-10-21)

- Build: client/hub-clue-steps-tier (master 3aca471's host and native, no code change: the change is the device
  check of the Clue Steps hub plugin against the live server), `OPENRUNE=1 LOCAL_AUTH=1 MODULUS=<openrune.sh modulus>
  tools/repack.sh` (7 `telemetry:` lines), installed (then as the split pair with `adb install-multiple -r`; now `adb install -r` of the single APK; Windows adb.exe, `wslpath -w`
  paths; relative WSL paths fail). Building in a harness worktree: `re/` must point at the main checkout's `re/`
  (`re/harness/wt/<name>` has the symlink; a `.claude/worktrees/` one needs `ln -s /home/alex/osrs-plugins-reverse/re re`
  plus symlinks for `base.apk` and `split_config.arm64_v8a.apk`, which repack.sh reads from the repo root), then
  host/build.sh, `ninja -C re/build` (the main checkout's native/, same as master when it has no native edits) and
  repack work as in the main checkout; MODULUS comes from `$SERVER_TOOLS/scripts/openrune.sh modulus` (the fork's
  local/server-tools; pipe through `tr -d '\n'`, don't truncate it). The worktree
  hook refuses shell commands that pass a runtime-computed value to a script: put the build in a script file under
  the session scratchpad and run that.
- Plugins dir: the default 3 only (grounditems, opponentinfo, quest-helper). Tester LOGGED_IN at 3226,3215 (moved
  there during hub-ground-items-despawn). Clue Steps wrote `clueSteps.rsprofile.default.{idEasy,clueEasy}` to
  settings.properties during hub-clue-steps-tier; the slot restore pushed the backup back. The spawned easy clue
  (2677) was dropped at 3226,3215 (61-minute private timer) and the server varp `trail_easy_steps_done` reset to 0.
- Tester inventory: 9 items (slots 0-8). The gen 44 Fish barrel (it held 3 raw shrimps) was destroyed
  through OpenRune's destroy confirmation (interface 584) in hub-fish-barrel-check.
- settings.properties: every login writes `rsprofile.rsprofile.default.{type,displayName,accountHash}`, which the slot
  backup of 2026-10-20 lacks, so the restore check reports OFF after a login; push the backup back while logged in to
  `files/settings.properties` (not `files/.runelite/`; the next login re-adds the 3 keys).
- Pre-existing plugin data under `files/.runelite/` (bossing-info, c-engineer-sounds, cache, pyramid-plunder-counter,
  resource-packs-repository) predates gen 40; leave it.

## Host behaviour worth knowing (details in docs/plugin-host.md rows)

- **Models**: `Client.loadModelData` decodes through the game's model store (`game::model_data`, a fresh ModelData per
  call, null if the model isn't loaded); `AndroidModelData.light` runs the game's normal and colour passes in Java;
  `loadModel` = loadModelData + recolours + light(64, 768, -50, -10, -50). Actor models (`NPC/Player.getModel`) are
  read from vtable slot 0 with alpha, textures, priorities and texture triangles (`game.cpp pack_mesh`). Bounds and
  `getAABB` use the game's formulas (diameter has the game's extra +1). `applyTransformations` re-lights the model id
  natively, writes the Java vertices in, poses with the game's animate (classic sequences only; skeletal or an
  out-of-range frame gives null) and copies the scratch result. `drawFrustum`/`drawOrtho` draw nothing.

- **Scene terrain**: `Scene` draw distance/min level/tint/skybox and `Tile.getSceneTilePaint/getSceneTileModel` read the
  game's scene on the client thread (docs/offsets.md "Scene terrain"; the last value off it, setters only on it).
  Underlay/overlay ids and tile shapes are raw map values ([plane][104][104], overlay shape = value + 1 as desktop),
  copied by a hook on the scene loader `0x70d30c` because the game frees them after the build (logcat
  `scene load: map arrays copied (43267 values)` per rebuild). SceneTilePaint colour/texture setters write the game's
  paint; `Tile.setSceneTilePaint/Model` accept only the tile's own (the game owns them).

- **Actor writes** (client-api-actor-anim): `Actor.setAnimation/setPoseAnimation` go through the game's
  AnimationState::setSequence (+0x4c8 / +0x4a8) and restart; frames, the 8 pose ids and `Player.setSkullIcon` (+0x730)
  are plain field writes; the Java copy changes at once (no AnimationChanged for a plugin's own write). Spot anims are
  the game's Actor +0x6f0 vector read live (`ActorSpotAnims`, a view per slot); `createSpotAnim/removeSpotAnim/
  clearSpotAnims` call the game's adder `0x4714e8` (as PLAYER_SPOTANIM_SPECIFIC); the deprecated
  getGraphic/setGraphic/graphic height/spot anim frame work on slot 0. `getOverheadText` keeps the text after its timer
  (desktop's field). `isDead` changes only on a head bar update or `setDead`. Probe `debug.osrsrl.set 'spotanim <id>
  <delay>'` (local player's slot 0, -1 removes, -2 lists) and `'anim <id> 0'` log the result. docs/offsets.md "Actor spot anims".
- **Menus**: `PostMenuSort` (presort) and `MenuOpened` both run in a menu session (`AndroidMenu.beginSession`); afterwards
  `AndroidMenu.applyOrder` writes text edits back and sends a permutation or subset of the game's entries to
  `Native.menuReorder` (logcat `menu reorder (MenuOpened) [...] of N: true`). Entries with submenus can't be dropped
  (`game::menu_reorder`). Row "No Examine".
- **Game state at start**: native posts a state only on a change, and changes before `host ready` are dropped, so the
  host's first event posts the game's current state (a start already on 1000, the error page, logs `GameStateChanged
  UNKNOWN (raw 1000` once).
- **Game state on rebuilds**: OpenRune's teleports go 30 -> 25 -> 30. A 25 while logged in is held until the scene base
  changes, then LOADING (scene objects forgotten first), WorldViewLoaded and LOGGED_IN go out in one cycle (`GameStateChanged
  LOADING (scene rebuild)`); same-base rebuilds after 50 cycles. A native 30 after the replay already posted LOGGED_IN is
  dropped (`... again: already posted (rebuild released)`). A `setGameState(LOADING)` reload shows no native 25, so no
  LOADING is posted (desktop would). Row "Herbiboar".
- **Scene items**: items whose tile leaves the scene on a rebuild get `ItemDespawned` on a tile with their old world
  location and item layer. Ground item despawn/visible timers and ownership come from OBJ_ADD (fix/76 live), now
  with OSRS durations (server fix/ground-item-despawn-times live): a dropped tradeable reads `visible in 99, despawn
  in 299` ticks and despawns exactly at its despawn tick, a dropped clue `despawn in 6099, private` (checked
  2026-10-21, hub-ground-items-despawn). `SceneObjects` logs `ground item spawned: TileItem[id xN] at x,y,plane tick T:
  visible in A, despawn in B ticks, ownership O[, private]` and `ground item despawned: ... tick T (despawn tick D, T-D
  late)`; world spawns read -1 timers. After a relog the server re-sends the player's own drops with timeUntilDespawn
  0xFFFF (despawn tick 65535 in the host log): server task ground-item-relogin-timers.
- **Devtools input** (`DevInput`): the osrs-devtools actions that drive the game (item_action, widget_action,
  interact_*, select_option, continue_dialogue, invoke_menu_action) dispatch their touches and keys in-process to the
  game window's `Window.Callback` on the main thread, as a real touch arrives (Sidebar's router, then the game).
  Instrumentation injection is refused on the Agent_Phone AVD (API 37: INJECT_EVENTS), which left every such action
  timed out (`menuAction ... timed out`, `devinput: touch failed: SecurityException`). Verified on Agent_Phone
  on build v0.1.1-89-gff933f7629a1 (evidence client-devtools-menu-actions): Wield/Wear/Drop, dialogue options
  (a tutor's and the valuable-drop prompt), a worn-slot widget op and click_component (side tabs, music row, world map
  orb and close) take effect; the api-probe at that build is 1168/1253, no row changed against main's.
  widget_action reaches item slots and worn-equipment slots only (hover menus for other widgets: use
  invoke_menu_action CC_OP with the widget id). Every devtools touch also sets the virtual mouse for 3 s
  (`VIRTUAL_MOUSE_MS`), and `client.menuAction` WALK takes the Walk here under a fresh virtual mouse (the hovered tile,
  as on desktop): in the api-probe set-up, open equipment stats with `adb shell input tap` (canvas x 2.07 on
  Agent_Phone), not a devtools click, or the menuAction walk row reads mismatch.
- **Soft keyboard Enter** (`PromptEnter`): the IME's Done is an editor action on the game's hidden EditText, not a key
  event; with KeyListeners registered the host sends VK_ENTER through KeyManager first, then the game's submit once.
- **Projectiles** (Projectiles.java, offsets.md "Projectiles"): `Client.createProjectile` (and the deprecated
  local-coordinate form, `WorldView.createProjectile`) spawns through the game's own PROJANIM_SPECIFIC path, so it is
  drawn and listed at once; `getProjectiles().addLast` of it is a no-op (already listed). Cycles are absolute
  (`getGameCycle() + n`); the deprecated form maps start/end height to the height offsets and ignores startZ. x/y/z stay
  0 until the game draws a frame after the start cycle (at ~4 fps on the AVD, a second or two). Projectile.getAnimation
  is the spotanim's sequence (`loadAnimation`).
- **Camera targets**: `setCameraPitchTarget/YawTarget` write the game's targets (Client `+0x8949c/+0x894a0`); the game
  eases pitch/yaw towards them (Camera Smoothing runs). `debug.osrsrl.camlog 1` also logs every 30th target write.
- **Camera settings**: `setCameraPitchRelaxerEnabled` writes the game's own relaxer flag (u8 `+0x8951c`, pitch up to 520),
  `setOculusOrbNormalSpeed` and `setFreeCameraSpeed` the one focal point speed the game uses in both modes (`+0x894c4`, 12),
  `setCameraSpeed` scales the touch drag sensitivity (settings `+0x82c`) from the game's own value, re-based each cycle when
  the game's settings change it (offsets.md "camera speeds"). The game has no invert flag: `setInvertPitch/Yaw` turn
  the touch camera drag the other way per axis after each cycle in touch state 8 (the step applied to the target undone
  and applied negated, the delta negated so the glide after release follows; arrow keys unchanged). The api-probe drags
  the scene (8 moves 60 ms apart through the host canvas) with the axis plain, then inverted: pitch 966 then -942, yaw
  -2300 then 2309. `setCameraMouseButtonMask` is a no-op, excluded from the probe (denominator 1252): the game's input
  dispatcher sends a mouse's right and middle buttons to their own listeners, never to the touch state that starts the
  camera drag, so no button can be selected.
- **Sprite overrides** of the sprite's canvas size are cropped to the game's trimmed block at its offset
  (`Native.spriteInfo`, Sprite +0x20..+0x2c); other sizes are still refused. Row "Poison".
- **Audio**: `javax.sound.sampled` shim (`host/awt/javax/sound/sampled`, HostClip plays through AudioPlayer/MediaPlayer).
  Drop Sounds' pin is dropped (its download URL 404s upstream).
- **ConfigManager.getRSProfiles()**: one profile `rsprofile.default` gets displayName/type/accountHash at each login
  (`Host.postGameState`, before RuneScapeProfileChanged).
- **Worlds**: the host's WorldService (same api.runelite.net fetch as 1.13.1) adds the current world from the game's
  slr.ws world list when the fetched list lacks it. The game rebuilds that list on the game thread whenever the
  login-screen world switcher opens, so off-thread `Native.worldEntry` returns a copy made in after_cycle
  (`client_thread::world_entry`; logcat `world list: N entries at 0x...` per reload). offsets.md "World list".
- **Rendering surface** (`AndroidRender.java`, plugin-host.md row "Rendering surface"): the mobile game draws on the GPU,
  so `getBufferProvider` is a canvas-sized copy of the last captured frame (`getPixels` requests the next capture;
  HostDrawManager hands every captured frame to it), `getRasterizer` draws into that copy (not shown on screen), the
  texture provider, skybox colour, viewport clip and camera focal point are the game's own (offsets.md "Render state"),
  projections are the main camera. Stretched flags, draw2DMask and DrawCallbacks are stored only.
- **Cache, index and config** (`CacheConfig.java`, plugin-host.md row "Cache, index and config lookups"): js5 archives
  through the game's getFile, the game's own LRU cache clears for the NodeCache getters and resetHealthBarCaches (between
  cycles only: getLocType hands out pointers into its nodes), varc map, IF_SETEVENTS table, params, ops with sub-ops, NPC
  overrides and head icons (offsets.md "js5 archives", "Game caches", "NPC overrides / head icons"). Off the client thread
  the reads answer null/empty. OpenRune sends no NPC body/head customisation, so NpcOverrides stays unreached on the test
  server; only one Android client can hold the tester account at a time (LOCAL_AUTH): a second gets "Either your account
  is still logged in" until the first logs out.
- **Widget fields** (client-api-widget-fields): `setType` writes +0x28 but never into or out of 12 (the text input
  type keeps an object at +0xba8; refused natively), `get/setContentType` +0x30, `get/setTargetPriority` +0x308 (the
  target verb's op index, default 4, 0..31), `get/setDragParent` the CC_SETDRAGGABLE shared_ptr (+0x310/+0x318,
  referenced and released as the game does; a non-host Widget clears it).
- **Client state** (plugin-host.md row "Client session, settings and menu state"): world host/socket/list, tick count, music volume and playing songs, oculus orb/camera mode, camera shake suppression, long varps, cross-world message ids, login page/field, minimap zoom and zoom input, menu scroll, target mode flag are the game's own (offsets.md "Client state"); mouseover text, animation interpolation filter and expanded map loading are stored only (desktop injects them too).
- **Host gaps closed** (client-api-host-gaps-reach, docs/offsets.md rows "Animated locs" .. "Appearance obj
  customisation"): scene objects' `getRenderable*` give an `AndroidDynamicObject` for an animated loc (vptr `0xa77620`;
  null for a static model); `PostHealthBarConfig` is posted when a health bar type first shows on an actor's head bar
  (after a hitsplat; again after `resetHealthBarCaches`), not at decode as on desktop, and `setPadding` writes the game's
  type; `ScriptCallbackEvent.getScript()` is the running script (opcodes and int operands from the game's script cache);
  `PlayerComposition` colour/texture overrides are the appearance's obj customisations (`createColorTextureOverride` is
  kept in the snapshot only); `getPendingLogins()` iterates the game's notice queue (addLast/clear don't change it);
  `FontTypeFace.getBaseline` is the game font's ascent and `drawWidgetText` draws RuneLite's RuneScape TTF of the font's
  style into the Rasterizer's pixels (the frame copy), not the game's sprite glyphs.
- **BufferedImage drawing**: `CanvasGraphics.forImage` draws into a live Bitmap shared with the image (no `dispose()`
  needed).
- **Network gates**: at load, `Host.loadJar` runs `NetScan.refusal` before the DexClassLoader and refuses NET-RAW jars
  (`W osrsrl-host: netscan: refused <jar>: ...`); vetted: `hub-loot-lookup` (raw refs only in bundled jsoup), `devtools`
  (adb-only local server); test override `setprop debug.osrsrl.netallow <jar-name>`; off-device
  `host/plugins/hub/netscan-check.sh <jars>`. At build, `host/plugins/hub/netcheck.sh` (run by hub build.sh) stops on
  NET-RAW unless `ALLOW_NET=1` (orchestrator's OK); NET-OKHTTP plugins need Network QA, new outbound hosts the
  orchestrator's OK.
- **Destroy confirmations** (interface 584): devtools `widget_action` can't hit its Yes on mobile (the hover lands on the
  scene); tap the Yes component's canvas centre scaled by 1920/894 (`dump_interface 584`, Yes = 584:1).
- **Network QA** (v0.1.0 gate, and gen 46 for Drop Sounds): only expected hosts (`dns.google` is an expected system
  lookup); the one GitHub socket is an approved raw.githubusercontent.com sound GET. No GE digest fetch captured yet
  (task netqa-ge-digest).

## Open gaps (client)

1. **Chat Enter stall (game side, not a plugin bug).** With no hub plugins and no KeyListeners: once the soft keyboard has
   been opened in a session (chat keyboard button 90,215, or a Withdraw-X prompt), Enter only submits chat while the
   soft keyboard is shown (`dumpsys input_method | grep mInputShown`). Before the keyboard was ever shown, hidden-keyboard
   Enter works. Check with devtools `get_var varcstr 335` (non-empty after a send = stalled); recover by showing the
   keyboard and pressing Enter. In that state the hidden-keyboard Enter reaches KeyListeners and is re-dispatched to the
   Activity, but the game doesn't submit (not investigated further).
2. **Current World**: its world switcher title (69:2) is unchecked: the in-game world switcher and the logout button
   weren't found on mobile (the bottom-left popout's door is "Minimise Popout"; the settings tab's sub-tabs are
   display/audio/controls). Relog = force-stop, wait ~50 s, Play Now.
3. Random Event Hider can't be staged on OpenRune; Line of Sight's stray lines at 3219,3222 are terrain, not a bug.
4. Herbi AFK, Cannon Highlighter: a gen 44 run each that drew nothing (Herbi AFK most likely the teleport gap, fixed;
   Cannon Highlighter 1 of 3 set-ups, not reproduced). Server-blocked hub plugins are tasks in the store.

## Gotchas

- Device: start your own client (harness skill `test-instances`); no lock. The osrs-devtools MCP drives the Android device. It needs `devtools.jar` in plugins/ and `setprop debug.osrsrl.devtools 1` plus an app restart. If the MCP server
  failed to connect at session start, `python3 -I tools/devtools_mcp.py --call tools/call '{"name":...,"arguments":{}}'`
  reaches the same tools. adb.exe from WSL fails now and then (`UtilAcceptVsock accept4 failed 110`): retry in an
  `until` loop, or batch taps and `screencap -p /sdcard/...` in one `adb shell "..."` call.
- **The AVD image ships the official, unpatched app**: `adb uninstall com.jagex.oldscape.android` first, install the repack, stop on any install error, and launch only after `pm path` shows the repack (signer = `re/debug.keystore`). A train deploy restarts the test server and disconnects every client ("You were disconnected from the server": Try again 1197,645, Play Now).
- Own Android instance: the AVD above with `-read-only -port <free even port>` (adb `emulator-<port>`, 2400x1080 landscape in game) is a
  separate instance with its own app data, gone when it stops. Fresh each boot: `adb install` the single APK, `pm grant`
  Chrome, push the default jars, then "Sign in with Email / Username" (1200,640), Accept (1026,668), Chrome "Stay signed
  out" (540,2087, portrait), the privacy X (2310,165), Play Now (1197,540); it signs in as tester.
- Slot start/end: `~/osrs-harness/bench/device-restore-check.sh pre <dir>` before touching anything, `check <dir>` at the end (OK/OFF per
  item). Remove test jars and files by name, in a plain `adb shell rm` call of their own, never with wildcards. Hub plugins
  go on the device as the dexed `re/host/plugins-extra/hub-<name>.jar`, not `re/hub-build/<name>.jar` ("no
  plugin-classes.txt"). Back up with `adb exec-out cat` (`shell cat` adds CRLF). Don't use `tools/plugin-test.sh` for
  cleanup (it rm's `*.jar`).
- Login: `ANDROID_SERIAL=emulator-<port> python3 -I ~/osrs-harness/bench/emulator_login.py` (text reads, no screenshot;
  signed out: only "Sign in with Email / Username"). After a force-stop the server often still holds the session ("Either
  your account is still logged in"): it retries Try again (1197,620) and Play Now (1197,540) until LOGGED_IN. A session resumed after `::drop` ("Reconnect
  resumed") ends the same way: its force-stop logs "Game session lost: user='tester'", the player leaves ~9 s later
  ("Logout: player=tester") and the next login succeeds (evidence client-resumed-session-relog).
- Agent_Phone refuses the host's Instrumentation input (devtools `click`, `type_chat`, tap-based `item_action`/`select_option` log
  `devinput: ... INJECT_EVENTS`): tap with `adb shell input tap` (canvas x,y times the device/canvas ratio) and type chat
  with the keyboard button (90,215 canvas), `input text`, `input keyevent 66`. Never send BACK (4) in game: it leaves the
  activity and the game restarts at the login screen. `interact_npc` and `click_menu_option` on an open menu still work.
- Chat commands: devtools `type_chat` (or the fork's `tools/device.sh cmd`) submits only while the soft keyboard is shown once
  it has been opened in the session (gap 1); verify with `get_var varcstr 335` or a screenshot. Item spawn is
  `::item <id> <n>` (no `::obj`), `::openbank`, `::mypos`. **`get_chat_history` under-reports repeated lines**; confirm
  by screenshot.
- Long-press = `input swipe x y x y 900`. Plugin MenuEntryAdded entries show in the plugin strip under the game menu,
  not in devtools' `get_npc_menu`.
- Probe hooks (`debug.osrsrl.set`, `probe`, `say`, `wdump`) run once per distinct value; `setprop` values with spaces
  or parentheses need `shell "setprop key 'value'"`. Probe output tag: `osrsrl-probe`. Plugin config:
  `debug.osrsrl.set 'config <group> <key>=<value>'` (`config <group> <key>` unsets it), not `probe`. InfoBox texts
  (Clue Juggling Timers and the like, which no data tool reads): `debug.osrsrl.set 'infoboxes <n> 0'` logs every
  InfoBox's class, text and tooltip (`infoboxes #n: <count> at tick T`); bump `n` for each dump. A failing `probe`
  value (e.g. an unknown command) is retried every few seconds: clear it with `setprop debug.osrsrl.probe ''`.
- Mobile tab stones (interface 601, two 7-stone columns at canvas x 799 and 838, rows 39 px from y 221): the
  inventory stone is **601:71** (top of the right column; device tap `input tap 1842 517` at 1920x1080), magic is
  601:100. `item_action` needs it open (`get_widget 9764864` = 149:0 `hidden:false`); devtools `click_component` on a
  stone and `press_key F4` don't open it, an adb tap does. `list_interfaces` doesn't list the open side panel's group.
- Idle logout: the client logs out after 5 min without real input; devtools `press_key` doesn't reset it, an adb
  `input tap` (e.g. the inventory stone) every 2 min does. Long waits (despawn timers) need that keep-alive.
- adb from WSL: `input tap` prints nothing, so a retry-until-output wrapper taps repeatedly (toggling panels); retry
  only reads. Plugin debug logs: `setprop log.tag.rl.<23-char tag> DEBUG` (tag = `rl.` + the simple class name cut to
  23, e.g. `rl.ClueScrollJugglingPl`). `logcat -d` through adb.exe loses lines now and then; for evidence run a
  device-side `logcat -v threadtime -s <tags> -f /sdcard/Download/<file> -r 32768 -n 20` in the background and
  `exec-out cat` it (remove the files at the end of the slot).
- Typing into a prompt: `adb shell input keyevent KEYCODE_1 ...` one key at a time (digits, `KEYCODE_PERIOD`); read the
  input with `get_var varcstr 359`. The bank's long-press menu gains a "Withdraw-<last X>" entry after a valid X and loses
  it after an invalid one, so rows shift: screenshot the menu before tapping (gen 44 withdrew all bank coins by tapping a
  shifted row). Valuable drops ask "Really put it down?". Untradeables have Destroy (coal bag and fish barrel work). `::tele` within the same zone posts no LOADING.
- Ground Item Sounds writes 3.6 MB of wavs to `.runelite/plugin-data/ground-item-sounds` on first start.
- Builds: host/build.sh + `ninja -C re/build` + repack (no docker). A worktree with native/ edits needs its own `re/`
  (not the symlink): symlink every entry of the main `re/` except `host`, `build`, `out`, copy `host/plugins-extra`,
  and configure `re/build` with cmake from the worktree's native/ (NDK r27d toolchain, arm64-v8a, android-26, Ninja);
  the default plugin jars are in the main `re/host/plugins/`. repack still edits the shared `re/apk/base` smali (same
  idempotent patch). Any build container goes through
  `tools/buildq.sh` (not gradle.lock). Every APK goes through `tools/repack.sh` (7 `telemetry:` lines).
- Network QA only when a build can change network behaviour (list in app-telemetry.md); any new outbound host: stop and
  report at once.
