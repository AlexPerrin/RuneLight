package net.runelite.host;

/** Game state read by libosrsrl (native/src/jni_bridge.cpp). Callable from any thread. */
final class Native {
    private Native() {}

    static native int gameState();
    /** Children created by CC_CREATE under component `id` (count; then per index). */
    static native int widgetDynamicCount(int id);
    static native boolean widgetDynamicHidden(int id, int index);
    static native String widgetDynamicText(int id, int index);
    static native int[] widgetDynamicGeometry(int id, int index);
    /** {itemId, quantity} of component id (index < 0) or its CC_CREATE child; null if absent. */
    static native int[] widgetItem(int id, int index);
    /** Widget +0x98 (0xRRGGBB); index -1 = the component itself. */
    static native int widgetColor(int id, int index);
    /** Writes the colour and marks the widget dirty (client thread only; ignored elsewhere). */
    static native void widgetSetColor(int id, int index, int rgb);
    /** Scene objects, all planes: 5 ints each (see jni_bridge.cpp n_scene_objects). Client thread only. */
    static native int[] sceneObjects();
    /** Ground items, all planes: 3 ints each (plane << 16 | x << 8 | y, id, quantity). Client thread only. */
    static native int[] groundItems();
    static native int gameCycle();
    /** Walk destination {x, y} in scene tiles, -1 when there is none. */
    static native int[] walkDestination();
    /** Definition param (kind 0 obj, 1 npc, 2 loc): Integer, String, or null when absent. */
    static native Object typeParam(int kind, int id, int key);
    /** Writes a whitelisted widget field (CC_SET* offset); index >= 0 = CC_CREATE child. */
    static native boolean widgetSetField(int id, int index, int offset, int value);
    /** One widget field (+0x320/+0x324/+0x13c int, +0x348 byte); Integer.MIN_VALUE if unreadable. */
    static native int widgetGetField(int id, int index, int offset);
    /** {component, CC_CREATE index (-1 static)} of the widget's drag parent (Widget +0x318), or null. */
    static native int[] widgetDragParent(int id, int index);
    /** CC_SETDRAGGABLE's store; parent < 0 clears it. Client thread only. */
    static native boolean widgetSetDragParent(int id, int index, int parent, int parentIndex);
    /** Re-runs the game's layout for the widget (CC_SETPOSITION/SETSIZE follow-up). */
    static native boolean widgetRevalidate(int id, int index);
    static native int serverTick();
    static native int varp(int id);
    static native int[] varps();
    static native int weight();
    static native int world();
    /** [0] = 1 if the scene is an instance, then [level][x][z] packed template chunks (-1 none); null before login. */
    static native int[] instanceTemplates();
    /** 8 GE slots x {state (0 empty, 1 adding, 2 stable, 5 finished, -1 none), type (0 buy, 1 sell), item, price, total, done, spent}; null before the client exists. */
    static native long[] geOffers();
    /** Probe: GE slot object address (8 bytes LE) + its first 0x28 bytes, or null. */
    static native byte[] geSlotRaw(int slot);
    /** Widget drag: {dragged comp, CC_CREATE index, dragged-on comp, index, dragging, cycles, touch state}, or null (client thread). */
    static native int[] dragState();
    /** setDraggedOnWidget(null): the release does no swap (client thread). */
    static native void dragClearTarget();
    /** Client+0x40e988: 3 held, 4 released. // unverified: other values */
    static native int touchState();
    /** Friends chat (-2) or clan channel (-1 listened, 0.. affined): {name, owner, int[] {myRank, kickRank, talkRank}, String[] names, int[] worlds, int[] ranks}, or null (client thread). */
    static native Object[] chatChannel(int which);
    static native int worldFlags();
    /** The world list (slr.ws) entry for a world: {String host, String activity, int[] {id, flags, players, location, listed}}, or null. */
    static native Object[] worldEntry(int id);
    /** Every world list entry: worldEntry's layout, info[5] = list index (game thread live, else the cached copy). */
    static native Object[] worldList();
    /** {String host, int[] {port, port2}} of the connected world; game thread only, else null. */
    static native Object[] connection();
    /** Writes the SERVER_TICK_END count serverTick() reads; game thread only. */
    static native boolean setServerTick(int tick);
    /** Client int fields by index (game.cpp kClientInts): ORB_STATE, ORB_X, ORB_Y, CHAT_CYCLE, CHANGED_SKILL_COUNT, ORB_SPEED, PITCH_RELAXER. Set: game thread only. */
    static final int ORB_STATE = 0, ORB_X = 1, ORB_Y = 2, CHAT_CYCLE = 3, CHANGED_SKILL_COUNT = 4, ORB_SPEED = 5, PITCH_RELAXER = 6;
    static native int clientInt(int which);
    static native boolean setClientInt(int which, int value);
    /** Music volume 0-255 (Client +0x2f58); set applies it to the playing songs; game thread only. */
    static native int musicVolume();
    static native boolean setMusicVolume(int v);
    /** Display {surface width, height, UI canvas width, height, scale * 1000}; null before it exists. */
    static native int[] display();
    /** The game's UiScaling setting (percent, 25..400); set (game thread) writes it unsaved and lays the UI out. */
    static native int uiScaling();
    static native boolean setUiScaling(int percent);
    /** The game's own UI layout from the surface size and UiScaling (desktop's resizeCanvas); game thread. */
    static native boolean relayout();
    /** The frame limit the game sleeps to (its FpsLimit setting applied; 0 = none); set (game thread) leaves the setting as it is. */
    static native int fpsLimit();
    static native boolean setFpsLimit(int fps);
    /** While on, minimapDrawn returns the plane the game's minimap build drew since the last call, else -1. */
    static native void setMinimapWatch(boolean on);
    static native int minimapDrawn();
    /** The minimap raster {size, zoom, ARGB...} and its write-back (size*size pixels); game thread. */
    static native int[] minimapRaster();
    static native boolean setMinimapRaster(int[] px);
    /** {group id, jingle 0/1} per song the music player is playing; game thread only, else null. */
    static native int[] midiRequests();
    /** While on, the camera shake flags are cleared after every cycle. */
    static native void setCameraShakeDisabled(boolean off);
    /** Client.setCameraSpeed: the touch camera's drag sensitivity is the game's own times this, kept each cycle. */
    static native void setCameraSpeed(float multiplier);
    /** The touch camera's drag sensitivity (settings +0x82c); 0 before the settings exist. */
    static native float cameraSensitivity();
    static native boolean cameraShakeDisabled();
    /** Every varp as a long (the long table where the game flags it long, else the int table). */
    static native long[] varpsLong(boolean server);
    /** Client.setVarpLongValue; game thread only. */
    static native boolean setVarpLong(int id, long value);
    /** The cross-world message id ring: 100 ids, then the next index (element 100). */
    static native long[] crossWorldIds();
    /** Login screen page and input field (0 username, 1 password). */
    static native int loginIndex();
    static native int loginField();
    /** Minimap zoom (px per tile, 2..8; -1 before the minimap exists); set through the game's setter, game thread only. */
    static native int minimapZoom();
    static native boolean setMinimapZoom(int zoom);
    /** The minimap's zoom-input flag (CS2 7250/7251). */
    static native int minimapZoomInput();
    static native boolean setMinimapZoomInput(boolean on);
    /** Top-level long-press menu scroll offset in pixels; set: game thread only. */
    static native int menuScroll();
    static native boolean setMenuScroll(int v);
    /** Sets the widget target state's active byte; game thread only. */
    static native boolean beginTargetMode();
    /** {zoom, x, y, width, height} of the 3D viewport in UI canvas pixels, or null. */
    static native int[] viewportZoom();
    /** GPU ViewTransforms block (camera pos @0, view @4, inverse view @20, projection @36, view-projection @52), or null. */
    static native float[] viewTransforms();
    /** One tuple of a DB row column (Integer/Long/String values), or null. Game thread only. */
    static native Object[] dbField(int row, int column, int tuple);
    /** Table of DB row `row`, or -1 if there is no such row. Client thread only. */
    static native int dbRowTable(int row);
    /** Tuples in a row's column (db_getfieldcount), -1 if missing. Client thread only. */
    static native int dbFieldCount(int row, int column);
    static native int runEnergy();
    /** {height, x0, y0, z0, ...}: an NPC's or player's posed model this frame (unrotated); client thread only. */
    static native int[] actorModel(boolean player, int index);
    /** Posed model, packed as game.cpp pack_mesh (AndroidModel parses it); client thread only. */
    static native int[] actorMesh(boolean player, int index);
    /** Model id's ModelData from the game's model store, packed as actorMesh; null if not loaded; client thread only. */
    static native int[] modelData(int id);
    /** Model id lit, its vertices set to xyz {x[n], y[n], z[n]}, posed by sequence a/b (b -1: none); packed as actorMesh; null if not possible. */
    static native int[] poseModel(int id, int[] xyz, int seqA, int frameA, int seqB, int frameB);
    /** Posed model height (desktop Model.height), -1 if unknown or off the game thread. */
    static native int actorHeight(boolean player, int index);
    /** Font <img=N> table: meta 7 ints per icon {gameIndex (-1 = custom), w, h, ox, oy, fullW, fullH}. */
    static native void setModIcons(int[] meta, byte[][] pixels, int[][] palettes);
    /** {fullW, fullH} per game mod icon, or null (not loaded / off the game thread). */
    static native int[] modIconSizes();
    /** Client varcs (0 / null when unset). */
    /** {x, y, height, orientation, modelHeight, vx, vy, vz...} of a scene object's model this frame, or null. */
    static native int[] objectModel(int plane, int sceneX, int sceneY, int layer, long tag);
    /**
     * Game thread: an animated loc's renderable in slot 0/1 of a scene object, {loc id, shape, orientation, plane,
     * scene x, scene y, seq id, last cycle, frame, frame cycle}, or with mesh its model (actorMesh layout); null if
     * that renderable is not an animated loc's.
     */
    static native int[] dynamicObject(int plane, int sceneX, int sceneY, int layer, long tag, int slot, boolean mesh);
    /** {valid, zoom, centreX, centreY, clip x0, y0, x1, y1, far} of the scene renderer's viewport. */
    static native int[] viewport();
    /** {name, targetVerb, op1..opN} of a widget (or its CC_CREATE child), null entries for empty strings. */
    static native String[] widgetStrings(int id, int index);
    static native void widgetSetHidden(int id, int index, boolean hidden);
    static native void widgetSetText(int id, int index, String text);
    /** which: -1 name (opbase), -2 target verb, 0..31 op (RuneLite's 0-based action index). Game thread. */
    static native void widgetSetString(int id, int index, int which, String text);
    /** Widget event flags (+0x2b0): bit i+1 shows op i in the game's menu. */
    static native int widgetFlags(int id, int index);
    /** The IF_SETEVENTS range covering a widget: {flags, op mask}, or null (none; game thread only). */
    static native int[] widgetEvents(int id, int index);
    static native void widgetSetFlags(int id, int index, int flags);
    /** The game's Widget* (0 if absent): identifies a CC_CREATE child across re-creations. */
    static native long widgetPtr(int id, int index);
    /** A widget's CS2 listener {scriptId, args...} stored at `offset` (0x590 onVarTransmit, ...), or null. */
    static native Object[] widgetListener(int id, int index, int offset);
    /** Clears the game's CS2 listener at `offset` (0x4d0 onDrag, 0x590 onVarTransmit, 0x668 onTimer), as CC_SETON with -1. Client thread. */
    static native boolean widgetClearListener(int id, int index, int offset);
    /** IF_CLOSESUB for `group` open in `component` (`0x462384`); false if not open. Client thread. */
    static native boolean closeSubInterface(int component, int group, boolean unload);
    /** The game's minimap image of `plane` as desktop's drawInstanceMap layout (736x736 RGB), or null. Client thread. */
    static native int[] drawMinimap(int plane);
    /** A widget's var/inv/stat transmit trigger ids (offset 0x5c0/0x608/0x650), or null. */
    static native int[] widgetTriggers(int id, int index, int offset);
    /**
     * Watched {script, opcode, ...} pairs: Host.onScript gets phase 3/4 around each (id = opcode << 16 |
     * script). Script -1 = any script whose object stack top starts with prefixes[i] (id script 0xffff).
     */
    static native void setOpWatches(int[] pairs, String[] prefixes);
    /** Replaces the string at object stack slot index (0 = bottom) of the running script. */
    static native boolean setScriptString(int index, String text);
    static native boolean playSound(int id, int loops, int delay);
    static native int varcInt(int id);
    static native String varcString(int id);
    /** Client-side writes through the game's setters; false off the client thread or for unknown ids. */
    static native boolean setVarbit(int id, int value);
    static native boolean setVarp(int id, int value);
    static native boolean setVarcInt(int id, int value);
    static native boolean setVarcString(int id, String value);
    /** {varp, lsb, msb} of a varbit definition, or null. */
    static native int[] varbitDef(int id);
    /** The game's addChatMessage; client thread only (returns false elsewhere). */
    static native boolean addChatMessage(int type, String name, String text);
    static native int serverVarp(int id);
    /** The server's varp table, whole (getServerVarps). */
    static native int[] serverVarps();
    /** Game thread only (throws IllegalStateException elsewhere). */
    static native int varbit(int id);
    /** {worldX, worldY, sceneX, sceneY, localX, localY, playerPlane, scenePlane}, or null when not logged in. */
    static native int[] playerPosition();
    static native boolean isClientThread();
    /** Edt only: while on, the calling thread counts as the client thread natively (the game thread is parked). */
    static native void setClientDelegate(boolean on);
    /** Client.queueChangedSkill: the game's changed-skills ring (onStatTransmit redraws). Client thread only. */
    static native boolean queueChangedSkill(int skill);
    /** Renderable draw listeners: these NPC / player indices are not drawn (or picked) until the next call. Client thread only. */
    static native void setHiddenActors(int[] npcs, int[] players);
    /** GE item search results: ids (null = keep), count / index (Integer.MIN_VALUE = keep). Client thread only. */
    static native boolean setGeSearch(short[] ids, int count, int index);
    /** {count, index, ids...} of the GE search results the setters write; game thread only, else null. */
    static native int[] geSearch();
    /** Minimap dot style of category 0-6: sets it when thickness >= 0, returns {styled, dot rgb, outline rgb, thickness} (null if unavailable). Client thread only. */
    static native int[] minimapDot(int cat, int dot, int outline, int thickness);
    /** Turns the script VM callbacks (Host.onScript) on or off. */
    static native void setScriptEvents(boolean on);
    /** The VM's live stacks (game thread): {int[] intStack (size entries), String[] objectStack}. */
    static native Object[] scriptStacks();
    /** Writes the first `size` ints back to the VM's int stack and sets its size (game thread). */
    static native boolean setScriptIntStack(int[] values, int size);
    /** Int array `slot` (desktop array id) of the running frame, or with caller = true of the frame a RETURN restores (inside ScriptPostFired); null if none. */
    static native int[] scriptArray(boolean caller, int slot);
    /** Writes an int array back in place (its length is kept). */
    static native boolean setScriptArray(boolean caller, int slot, int[] values);
    /** Int local `index` (written first when write); Integer.MIN_VALUE if unavailable. */
    static native int scriptIntLocal(boolean caller, int index, boolean write, int value);
    /** Innermost running script id (inside a RETURN's ScriptPostFired: the caller), -1 if none. */
    static native int currentScript();
    /** Int stack slot `depth` from the top (written first when write); Integer.MIN_VALUE if none. Game thread. */
    static native int scriptIntTop(int depth, boolean write, int value);
    /** The top object stack value if it is a string, else null. Game thread. */
    static native String scriptStringTop();
    /** Arguments of the root script being started (inside Host.onScript phase 2), or null. */
    static native Object[] scriptEventArgs();
    /** {component id, CC_CREATE index or -1} of the running ScriptEvent's source widget, or null. */
    static native int[] scriptEventSource();
    /** {component id, CC_CREATE index or -1} of the VM's active component (dot = the .cc_ one), or null. Game thread. */
    static native int[] scriptActiveWidget(boolean dot);
    /** Width of one line of text in game font `font` (CS2 PARAWIDTH's measure), -1 if unavailable. Game thread. */
    static native int textWidth(int font, String text);
    /** Game thread: a game font's ascent (FontTypeFace.getBaseline), -1 if the font isn't available. */
    static native int fontAscent(int font);
    /** Game thread: {int[] opcodes, int[] int operands} of a client script through the game's script cache, or null. */
    static native Object[] scriptCode(int id);
    /** Game thread: {id, front sprite, back sprite, width, padding} per health bar type on a live actor's head bar. */
    static native int[] healthBarTypes();
    /** Game thread: writes the width padding of in-use health bar type `id`; false if none is shown. */
    static native boolean healthBarSetPadding(int id, int padding);
    /** CC_CREATE under static layer `id` at `index` (-1 = append); the index, or -1. Game thread. */
    static native int widgetCreateChild(int id, int index, int type);
    /** CC_DELETEALL on static component `id`. Game thread. */
    static native boolean widgetDeleteChildren(int id);
    /** Drops the game children of `id` from index `count` on (setChildren truncation). Game thread. */
    static native boolean widgetTruncateChildren(int id, int count);
    /** {root scripts, gosubs, returns, events delivered, hooks installed (0/1)}. */
    static native long[] scriptStats();
    /**
     * Runs a client script in the game's VM (args[0] = script id; Integer/String args). Game
     * thread only. Returns {int[] intStack, String[] objectStack} as left by the script.
     */
    static native Object[] runScript(Object[] args);
    /**
     * Item definition (game thread only): {int[] {price, weight, category, stackable, members,
     * tradeable?, linkedNote, noteTemplate, placeholder, placeholderTemplate}, String[] {name,
     * inventory actions x5 (null = none)}}, or null.
     */
    static native Object[] itemDef(int id);
    /** NPC definition (game thread only): {int[] {id, size, combatLevel, minimapVisible, interactable}, String[] {name, actions x5}}, or null. */
    static native Object[] npcDef(int id);
    static native Object[] locDef(int id);
    /** Sidebar (overlay.cpp): replaces the model shown (format in Sidebar). */
    static native void sidebarSetModel(String model);
    /** Taps since the last call as (kind, index) pairs, or null. */
    static native int[] sidebarPollEvents();
    static native boolean sidebarOpen();
    static native void sidebarSetOpen(boolean open);

    /** Sidebar icon pixels (ARGB, row-major) for model nodes of kind I with this id. */
    static native void sidebarIcon(int id, int width, int height, int[] argb);
    /** The virtual Shift toggle under the RL button. */
    static native boolean shiftHeld();
    /** Replaces a chat message's text in the game (game thread). */
    static native boolean messageSetText(int id, String text);
    /** Removes a message (type, id) from the game's chat channel; client thread only. */
    static native boolean chatRemove(int type, int id);
    /** Makes the chatbox rebuild (the game's chat cycle bump); client thread only. */
    static native void chatChanged();
    /** The id the game's next chat message will get (its global message counter). */
    static native int nextMessageId();
    /** Client thread: the game's world map {present, loaded, zoom * 1000, display x, y, current map id}, else null. */
    static native int[] worldMap();
    /** Client thread: worldmap_coordinmap(map, level, x, z) = 0/1, -1 when the map area isn't known. */
    static native int worldMapContains(int map, int level, int x, int z);
    /** Client thread: worldmap_jumptosourcecoord (smooth scroll). */
    static native boolean worldMapJump(int level, int x, int z);
    /** Client thread: {rows, cols, then per icon row, col, element, level, x, y} of the current map's regions, null when not loaded. */
    static native int[] worldMapIcons();
    /** Map element config {category, sprite}, null for an unknown id. */
    static native int[] mapElement(int id);
    /** Sprite by id, frame 0: {width, height, ARGB pixels...}, null while not loaded; client thread only. */
    static native int[] sprite(int id);
    /** Sprite frame 0's {width, height, x offset, y offset, full width, full height}, null while not loaded; client thread only. */
    static native int[] spriteInfo(int id);
    /** The game's sprite arrays (0 crosses, 1 mapscenes): count, then per frame {w, h, ox, oy, cw, ch, n, pixels or indices, n palette}; game thread. */
    static native int[] spriteArray(int which);
    /** Texture provider: 8 ints per texture {present, avg HSL, flag, sprite, anim direction, anim speed, loaded, pixel count}; game thread. */
    static native int[] textures();
    /** A texture's pixels (load: through the game's provider, else only if loaded); game thread. */
    static native int[] texturePixels(int id, boolean load);
    static native double textureBrightness();
    /** The scene's skybox colour as ARGB (alpha 0: none set); game thread. */
    static native int skyboxColour();
    static native boolean setSkyboxColour(int argb);
    static native boolean setTextureBrightness(double b);
    /** Writes same-size ARGB pixels into the game's cached sprite; client thread only. */
    static native boolean spriteWrite(int id, int[] pixels, int w, int h);
    /** The game's idle logout timeout (AFKTimeout setting) in minutes, -1 if unknown. */
    static native int afkTimeout();
    /** Sets it (1..35000 minutes); client thread only. */
    static native boolean setAfkTimeout(int minutes);
    /** Plugin menu entries shown under the game's menu at (x, y), width w (UI canvas); null/empty hides. */
    static native void pluginMenu(String[] labels, int x, int y, int w, int canvasHeight);
    /** Closes the open long-press menu (game thread). */
    static native boolean menuClose();
    /** Game thread: {planes, sizeX, sizeY, heights[planes][sx+1][sy+1]..., settings[planes][sx][sy]...}, or null. */
    static native int[] sceneHeights();
    /** Game thread: {int[] {keyType, valueType, defaultInt}, String defaultString, int[] keys, int[] intValues, String[] stringValues}, or null. */
    static native Object[] enumDef(int id);
    /** UI thread: offers a touch to the overlay; true if the overlay takes it (the game won't see it). */
    static native boolean overlayTouch(int action, float x, float y);
    /**
     * NPCs as of the last client cycle: {int[] packed, String[] names}, where packed holds
     * {index, id, localX, localY, worldX, worldY} per NPC.
     */
    /**
     * {int[] packed, String[] names, String[] overhead texts}; 22 ints per NPC: index, id, fine x, fine y,
     * world x, world y, animation, animation frame, orientation, current orientation, interacting
     * (NPC index or player index + 32768, -1 none), overhead cycle, 8 pose animations (idle, idle
     * rotate left/right, walk, walk 180/left/right, run), pose animation, pose frame, health ratio, health
     * scale (-1 without a head bar), 4 hitmark slots {type, amount, end cycle} (-1 empty) = 36. players() is the same with
     * `local` (1/0) in place of id.
     */
    static native Object[] npcs();
    /** Friends (false) or ignores (true): {String[] names, String[] previous names, int[] {world, rank} pairs}; null until loaded. */
    static native Object[] friends(boolean ignores);
    /** FriendSystem +0x150: 0 none, 1 FRIENDLIST_LOADED, 2 list received. */
    static native int friendListState();
    /** Game thread: {String[] names, int[] worlds} of the friend login/logout notices not shown yet (0 = logged out). */
    static native Object[] pendingLogins();
    /** Game thread: per equipment slot a {recolour-to, retexture-to} pair or null, of player `index`'s appearance. */
    static native short[][][] playerOverrides(int index);
    /** Probe: the first `len` bytes of the game's widget target-mode state (game.cpp target_state_bytes); null if unreadable. */
    static native byte[] targetState(int len);
    /** The game's widget target mode: {active 0/1, selected component id, CC_CREATE index}; null if unreadable. Game thread. */
    static native int[] targetMode();
    /** Leaves target mode (Client.setWidgetSelected(false)). Game thread. */
    static native boolean endTargetMode();
    /** The game's settings-changed scene reload (state 30 -> 25); client thread only. */
    static native boolean reloadScene();
    /** Removes the top-level scene tile's ground decoration with the game's remover; client thread only. */
    static native boolean removeGroundObject(int plane, int x, int y);
    /** Scene terrain data (game::scene_terrain: what 0 scene fields, 1 tile paint, 2 tile model, 3/4/5 map underlays/overlays/shapes, 6 skybox), or null. Client thread. */
    static native int[] sceneTerrain(int what, int plane, int x, int y);
    /** what 0: scene field (0 draw distance, 1 min level); 1: field of the tile paint at ptr (0 sw .. 3 nw, 4 texture, 5 rgb). Client thread. */
    static native boolean sceneSet(int what, int plane, int x, int y, long ptr, int field, int value);
    /** Ambient (loc) sounds: per entry obj low, obj high, sound id, plane, n, n background ids; client thread. */
    static native int[] ambientSounds();
    /** Projectiles (Client +0xcb30 list): 27 ints each, layout in jni_bridge.cpp n_projectiles; client thread. */
    static native int[] projectiles();
    /** Graphics objects (top-level WorldView +0x160): 32 ints each, layout in jni_bridge.cpp n_graphics_objects; client thread. */
    static native int[] graphicsObjects();
    /** Projectile.setEndCycle on a listed projectile; client thread. */
    static native boolean projectileSetEndCycle(long obj, int cycle);
    /** Spawns a projectile into the game's list: {source level, x, y (world), start height, source index,
     *  target level, x, y, end height, target index, start cycle, end cycle, slope, start pos, spotanim id};
     *  returns its object, 0 if none; client thread. */
    static native long projectileCreate(int[] s);
    static native boolean graphicsObjectFinish(long obj);
    /** Clears the live ambient sound list (the game stops and frees them next cycle); client thread. */
    static native boolean ambientClear();
    /** Puts an ambient sound cleared this cycle back into the live list; client thread. */
    static native boolean ambientRestore(long obj);
    /** Moves one live ambient sound to the pending list (stopped next cycle); client thread. */
    static native boolean ambientRemove(long obj);
    /** size*size collision flags of a top-level plane (scene x major), or null; client thread. */
    static native int[] collisionFlags(int plane, int size);
    /** Writes an actor's overhead text (null keeps it) and timer (-1 keeps it); client thread only. */
    static native boolean actorSetOverhead(boolean player, int index, String text, int cycle);
    /** Writes actor state (game.h kActor*: animation, frames, pose, skull); client thread only. */
    static native boolean actorSet(boolean player, int index, int what, int value);
    /** {slot, id, height, start cycle, frame, frame cycle} per spot anim, or null; client thread only. */
    static native int[] actorSpotAnims(boolean player, int index);
    /** Writes one spot anim field (0 id, 1 height, 2 start cycle, 3 frame, 4 frame cycle); client thread only. */
    static native boolean actorSpotAnimSet(boolean player, int index, int slot, int field, int value);
    /** The game's spot anim adder: replaces the slot's spot anim (id -1 removes it); client thread only. */
    static native boolean actorCreateSpotAnim(boolean player, int index, int slot, int id, int height, int delay);
    /** Open sub-interfaces: {component, group, modal mode} triples. */
    static native int[] subInterfaces();
    static native boolean widgetExists(int componentId);
    static native boolean widgetHidden(int componentId);
    /** Widget text, or null if the widget's group isn't loaded. */
    static native String widgetText(int componentId);
    static native int rootInterface();
    /** {x, y, width, height, parentId} (x/y relative to the parent), or null if not loaded. */
    static native int[] widgetGeometry(int componentId);
    /** Number of components in a loaded interface group. */
    static native int widgetChildCount(int group);
    /** An Android system property ("" if unset). */
    static native String property(String name);
    /** {x, y, z, pitch, yaw}: camera in local units (z = height), 14-bit angles. */
    static native int[] camera();
    /** Camera pitch (0) or yaw (1) target, which the game eases the camera towards (client thread only). */
    static native boolean setCameraTarget(int which, int value);
    /** Camera focal point axis 0 x, 1 y, 2 height (local units); game thread. */
    static native boolean setCameraFocal(int axis, int value);
    /** {width, height} of the game surface in pixels (0 before the first frame). */
    static native int[] screenSize();
    /** Direct buffer over the native back buffer, sized w*h*4 (RGBA, premultiplied). */
    static native java.nio.ByteBuffer overlayBuffer(int width, int height);
    /** Makes the back buffer the displayed overlay frame; (0, 0) hides overlays. */
    static native void overlayPublish(int width, int height);
    /** Players as of the last cycle: {int[] packed, String[] names}, packed = {index, local, localX, localY, worldX, worldY}. */
    static native Object[] players();
    /** Item ids then quantities (2 x slots) of an inventory as of the last cycle, or null. */
    static native int[] inventory(int id);
    /** Ids of every inventory the client holds. */
    static native int[] inventoryIds();
    /** {current (boosted) levels[25], real levels[25], experience[25]}, indexed by Skill.ordinal(). */
    static native int[] skills();
    /** {name, sender, text} of a captured chat message, or null. */
    static native String[] message(int id);
    /** Game thread: the shown menu's entries, {int[] {kind, hasChildren, raw[10]} per entry, String[] {option, target} per entry}. */
    static native Object[] menuEntries();
    /** Rewrites an open menu entry's option/target (null keeps it); index in menuEntries() order. */
    static native boolean menuSetText(int index, String option, String target);
    /** The entry being selected (valid while MenuOptionClicked is dispatched), same format. */
    static native Object[] clickedMenuEntry();
    static native boolean menuOpen();
    /** {width, height, ARGB pixels...} of an item icon (game thread), or null while not loaded. */
    static native int[] itemSprite(int id, int quantity, int border, int shadow, int textMode, boolean noted);
    /** Widget properties (component, or its CC_CREATE child when index >= 0): scrollX, scrollY,
     *  scrollWidth, scrollHeight, modelZoom, rotationX, rotationY, rotationZ, opacity, modelType,
     *  modelId, animationId; null if absent. */
    static native int[] widgetProps(int id, int index);
    /** {x, y} where the open menu was requested (UI-canvas units), null when closed. */
    static native int[] menuPoint();
    /** From a MenuOptionClicked subscriber: the game won't run the entry. */
    static native void consumeMenuClick();
    /** During a SOUND_EFFECT event: the game drops the sound. */
    static native void consumeSound();
    /** The area sound being posted: {id, sceneX, sceneY, range, delay, loops, sourceKind (0 none, 1 NPC,
     *  2 player), sourceIndex, plane, fromAnimation}. */
    static native int[] areaSound();

    /** During a MenuTap event: run this top-level node (Native.menuEntries index) instead of the default. */
    static native void redirectMenuTap(int index);
    /** Game thread: shows the open menu reordered; order[i] = old top-first native index at position i. */
    static native boolean menuReorder(int[] order);
    /** Game thread: builds the long-press menu at (x, y) (canvas units) without showing it. */
    static native boolean menuBuildAt(int x, int y);
    /** Game thread: the next frame ray-picks (x, y) in screen pixels, as a touch down does. */
    static native void pickAt(int x, int y);
    /** Whether the pick asked by pickAt is done (menuBuildAt then sees its hits). */
    static native boolean pickReady();
    /** Game thread: drops a menu built by menuBuildAt (never shown). */
    static native void menuDiscard();
    /** Game thread: runs node `index` (top first) of the current menu as a pick of that row. */
    static native boolean menuSelect(int index);
    /** Asks the next frame to be captured (glReadPixels); takeFrame() returns it once. */
    static native void requestFrame();
    /** {width, height, argb...} of the captured frame, or null while none is ready. */
    static native int[] takeFrame();

    /** True while a MenuTap event is dispatched (menuEntries() then reads the tapped menu). */
    static native boolean menuTapping();

    // Cache, index and config lookups (CacheConfig). Client thread only: they throw elsewhere.
    /** Param keys of a definition (Params kinds), or null when the definition is missing. */
    static native int[] typeParamKeys(int kind, int id);
    /** File ids of a group of archive `id`, or null when the group is absent. */
    static native int[] archiveFileIds(int id, int group);
    /** A file's bytes through the game's getFile, or null (absent, or its group not loaded yet). */
    static native byte[] archiveFile(int id, int group, int file);
    /** Entry count of game cache `which` (CacheConfig.Cache order), -1 for a bad id. */
    static native long cacheSize(int which);
    /** Runs the game's clear of that cache. */
    static native boolean cacheReset(int which);
    /** {int[] models, short[] recolours, short[] retextures, int[] {mirror local player}} of an NPC's head or body override, or null. */
    static native Object[] npcOverrides(int index, boolean head);
    /** An NPC's head icon archive ids then sprite indices (equal halves), or null. */
    static native int[] npcHeadIcons(int index);
    /** Sub-op texts under op `op` of an npc (Params.NPC) or loc (Params.LOC); null when it has none. */
    static native String[] subOps(int kind, int id, int op);
    /** {int[] ids, Object[] values (Integer/String)} of the varc store, or null. */
    static native Object[] varcEntries();
    /** IF_SETEVENTS ranges, 5 ints each {component, start, end, flags, op mask}. */
    static native int[] widgetEventRanges();
}
