package net.runelite.host;

import net.runelite.api.GameState;
import net.runelite.api.Player;
import net.runelite.api.WorldView;

/** net.runelite.api.Client backed by the native game. Methods not overridden here throw. */
public class AndroidClient extends ClientStub {
    private final AndroidWorldView worldView;

    AndroidClient(AndroidWorldView worldView) {
        this.worldView = worldView;
    }

    @Override
    public WorldView getWorldView(int id) {
        // Only the top-level world view (WorldView.TOPLEVEL = 0; -1 accepted too) exists until
        // world entities (boats) are mapped.
        return id == WorldView.TOPLEVEL || id == -1 ? getTopLevelWorldView() : null;
    }

    @Override public net.runelite.api.SceneTilePaint createSceneTilePaint(int sw, int se, int ne, int nw, int texture, int rgb, boolean flat) {
        return new AndroidScene.TilePaint(0, 0, 0, 0, sw, se, ne, nw, texture, rgb, flat);
    }

    /**
     * Never null, as in RuneLite: plugins (QH's minimap overlay) call getPlane()/isInstance() on it
     * before login. Its accessors fall back to zeros/empty sets while no scene is loaded.
     */
    @Override
    public WorldView getTopLevelWorldView() {
        return worldView;
    }

    @Override
    public GameState getGameState() {
        if (Host.sceneReloading) return GameState.LOADING;
        GameState s = GameState.of(Native.gameState());
        // Desktop is LOGGED_IN only once the local player exists. GameStateChanged(LOGGED_IN) waits
        // for it (Host.pendingLoggedIn); the game's player list can still be empty for a few cycles
        // after its state turns 30, and plugins guarded by LOGGED_IN then read a null local player
        // (Runecrafting Utilities' ClientTick). Latched to the posted event, not the live list.
        if (s == GameState.LOGGED_IN && !Host.loggedInPosted) return GameState.LOADING;
        return s != null ? s : GameState.UNKNOWN;
    }

    @Override
    public int getGameCycle() {
        return Native.gameCycle();
    }

    @Override
    public int getTickCount() {
        return Native.serverTick();
    }

    @Override
    public int[] getVarps() { return lastVarps = Native.varps(); }
    /** The array getVarps() last returned: RuneLite hands out the live array, which plugins edit
     *  (setVarbitValue) and then publish with queueChangedVarp. */
    private int[] lastVarps;

    @Override public void setVarbit(int varbit, int value) { Native.setVarbit(varbit, value); }
    @Override public void setVarbitValue(int[] varps, int varbit, int value) {
        int[] d = Native.varbitDef(varbit);
        if (d == null || varps == null || d[0] >= varps.length) return;
        int bits = d[2] - d[1] + 1;
        int mask = bits >= 32 ? -1 : (1 << bits) - 1;
        varps[d[0]] = (varps[d[0]] & ~(mask << d[1])) | ((value & mask) << d[1]);
    }
    @Override public void queueChangedVarp(int varp) {
        if (lastVarps != null && varp >= 0 && varp < lastVarps.length) Native.setVarp(varp, lastVarps[varp]);
    }
    @Override public net.runelite.api.VarbitComposition getVarbit(int id) {
        int[] d = Native.varbitDef(id);
        if (d == null) return null;
        return new net.runelite.api.VarbitComposition() {
            @Override public int getIndex() { return d[0]; }
            @Override public int getLeastSignificantBit() { return d[1]; }
            @Override public int getMostSignificantBit() { return d[2]; }
        };
    }
    @Override public void playSoundEffect(int id) { Native.playSound(id, 1, 0); }
    @Override public void playSoundEffect(int id, int volume) { Native.playSoundVolume(id, volume); }
    @Override public void playSoundEffect(int id, int x, int y, int range) { Native.playAreaSound(id, x, y, range, 0); }
    @Override public void playSoundEffect(int id, int x, int y, int range, int delay) { Native.playAreaSound(id, x, y, range, delay); }
    @Override public void setVarcIntValue(int id, int value) { Native.setVarcInt(id, value); }
    @Override public void setVarcStrValue(int id, String value) { Native.setVarcString(id, value); }

    @Override public int getWeight() { return Native.weight(); }
    @Override public int getEnergy() { return Native.runEnergy(); }

    @Override public int getVarcIntValue(int id) { return Native.varcInt(id); }
    @Override public String getVarcStrValue(int id) { String s = Native.varcString(id); return s != null ? s : ""; }

    /**
     * Through the game's own addChatMessage, so the line shows in the chatbox (and comes back as
     * a ChatMessage event). Off the client thread it is queued to it. Latin-1 text; the game's
     * tags (<col=...>) work as on desktop.
     */
    @Override
    public net.runelite.api.MessageNode addChatMessage(net.runelite.api.ChatMessageType type, String name, String message, String sender) {
        return addChatMessage(type, name, message, sender, true);
    }

    @Override
    public net.runelite.api.MessageNode addChatMessage(net.runelite.api.ChatMessageType type, String name, String message, String sender, boolean postEvent) {
        final int t = type != null ? type.getType() : 0;
        final String n = name != null ? name : "", m = message != null ? message : "";
        AndroidMessageNode node = new AndroidMessageNode(net.runelite.api.ChatMessageType.of(t), n, m, sender, postEvent);
        synchronized (PENDING) {
            PENDING.add(node);
        }
        if (isClientThread()) Native.addChatMessage(t, n, m);
        else Host.invoke(() -> Native.addChatMessage(t, n, m));
        return node;
    }

    /** Nodes returned by addChatMessage whose game message hasn't been seen yet (oldest first). */
    private static final java.util.ArrayDeque<AndroidMessageNode> PENDING = new java.util.ArrayDeque<>();

    /** The pending node for a new game message of this type and text, or null; drops stale ones. */
    static AndroidMessageNode takePending(net.runelite.api.ChatMessageType type, String text) {
        long now = System.currentTimeMillis();
        synchronized (PENDING) {
            while (!PENDING.isEmpty() && now - PENDING.peekFirst().pendingSince > 10000) PENDING.pollFirst();
            for (java.util.Iterator<AndroidMessageNode> it = PENDING.iterator(); it.hasNext(); ) {
                AndroidMessageNode p = it.next();
                if (p.getType() == type && java.util.Objects.equals(p.sentText, text)) {
                    it.remove();
                    return p;
                }
            }
        }
        return null;
    }

    @Override
    public int getVarpValue(int id) {
        return Native.varp(id);
    }

    @Override
    public int getServerVarpValue(int id) {
        return Native.serverVarp(id);
    }
    /** A copy of the server's varp table (desktop hands out the live array; nothing writes it here). */
    @Override public int[] getServerVarps() { return Native.serverVarps(); }
    /**
     * As desktop: writes the client's varp value (no VarbitChanged until queueChangedVarp). Here the
     * game's own write-back, which also queues the change (as setVarbit). // unverified: on the device
     */
    @Override public void setVarpValue(int varp, int value) {
        if (lastVarps != null && varp >= 0 && varp < lastVarps.length) lastVarps[varp] = value;
        Native.setVarp(varp, value);
    }

    @Override
    public int getVarbitValue(int id) {
        return Native.varbit(id);
    }

    // --- client scripts: run in the game's VM; the stacks are a copy taken when the script returns ---

    private final int[] intStack = new int[1000];
    private final Object[] objectStack = new Object[1000];
    private int intStackSize, objectStackSize;

    // While a ScriptPreFired/PostFired is dispatched, the getters read the VM's live stacks
    // (loaded on first use) and changes to the int stack are written back afterwards.
    private boolean liveStacks, stacksLoaded;
    private int[] loadedInts;

    /**
     * Host-made stacks for a callback the game's scripts don't have (RuneLite injects
     * runelite_callback ops into its own copies of the chat scripts): the getters return these
     * until the next script event.
     */
    void fakeStacks(int[] ints, Object[] objs) {
        liveStacks = false;
        System.arraycopy(ints, 0, intStack, 0, ints.length);
        System.arraycopy(objs, 0, objectStack, 0, objs.length);
        intStackSize = ints.length;
        objectStackSize = objs.length;
    }

    // --- getArray: script int arrays (frame object locals). Arrays a plugin fetched during a
    // callback are written back afterwards (desktop hands out the live array). arrayFrameCaller
    // selects the frame a RETURN restores (callbacks raised from ScriptPostFired).
    boolean arrayFrameCaller;
    private final java.util.Map<Integer, int[]> fetchedArrays = new java.util.HashMap<>();

    @Override
    public int[] getArray(int id) {
        if (!isClientThread()) return null;
        int[] a = fetchedArrays.get(id);
        if (a != null) return a;
        a = Native.scriptArray(arrayFrameCaller, id);
        if (a != null) fetchedArrays.put(id, a);
        return a;
    }

    @Override
    public int getArraySizes(int id) {
        int[] a = getArray(id);
        return a != null ? a.length : 0;
    }

    /** Writes back the arrays fetched since the last call (end of a callback). */
    void flushArrays() {
        for (java.util.Map.Entry<Integer, int[]> e : fetchedArrays.entrySet())
            Native.setScriptArray(arrayFrameCaller, e.getKey(), e.getValue());
        fetchedArrays.clear();
    }

    void beginScriptEvent() {
        liveStacks = true;
        stacksLoaded = false;
    }

    void endScriptEvent() {
        if (stacksLoaded && loadedInts != null
            && (intStackSize != loadedInts.length || !java.util.Arrays.equals(java.util.Arrays.copyOf(intStack, intStackSize), loadedInts))) {
            Native.setScriptIntStack(intStack, Math.max(0, Math.min(intStackSize, intStack.length)));
        }
        liveStacks = false;
        stacksLoaded = false;
        loadedInts = null;
    }

    private void loadStacks() {
        if (!liveStacks || stacksLoaded) return;
        stacksLoaded = true;
        Object[] r = Native.scriptStacks();
        if (r == null) return;
        int[] ints = (int[]) r[0];
        Object[] objs = (Object[]) r[1];
        System.arraycopy(ints, 0, intStack, 0, ints.length);
        System.arraycopy(objs, 0, objectStack, 0, objs.length);
        intStackSize = ints.length;
        objectStackSize = objs.length;
        loadedInts = ints;
    }

    @Override
    public void runScript(Object... args) {
        if (liveStacks) {
            // The VM is mid-script; run it after the current one instead of corrupting its state.
            android.util.Log.w("osrsrl-host", "runScript(" + (args.length > 0 ? args[0] : "") + ") from a script event; deferred");
            Host.invoke(() -> runScript(args));
            return;
        }
        Object[] r = Native.runScript(args);
        int[] ints = (int[]) r[0];
        Object[] objs = (Object[]) r[1];
        System.arraycopy(ints, 0, intStack, 0, ints.length);
        System.arraycopy(objs, 0, objectStack, 0, objs.length);
        intStackSize = ints.length;
        objectStackSize = objs.length;
    }

    /**
     * Client.getMessages: the messages seen since the host started (the same nodes ChatMessage
     * carried), newest 1000 kept, keyed by message id.
     */
    private static final java.util.LinkedHashMap<Long, net.runelite.api.MessageNode> MESSAGES =
        new java.util.LinkedHashMap<Long, net.runelite.api.MessageNode>() {
            @Override protected boolean removeEldestEntry(java.util.Map.Entry<Long, net.runelite.api.MessageNode> e) { return size() > 1000; }
        };

    static void rememberMessage(net.runelite.api.MessageNode node) {
        synchronized (MESSAGES) { MESSAGES.put((long) node.getId(), node); }
    }

    @Override public net.runelite.api.IterableHashTable<net.runelite.api.MessageNode> getMessages() {
        return new net.runelite.api.IterableHashTable<net.runelite.api.MessageNode>() {
            @Override public net.runelite.api.MessageNode get(long id) { synchronized (MESSAGES) { return MESSAGES.get(id); } }
            @Override public void put(net.runelite.api.MessageNode n, long id) { synchronized (MESSAGES) { MESSAGES.put(id, n); } }
            @Override public java.util.Iterator<net.runelite.api.MessageNode> iterator() {
                synchronized (MESSAGES) { return new java.util.ArrayList<>(MESSAGES.values()).iterator(); }
            }
        };
    }

    /**
     * Client.getChatLineMap: per chat type, the messages seen since the host started, newest
     * first (as the game's buffers). removeMessageNode also removes the line from the game's chat
     * channel (Native.chatRemove) and makes the chatbox rebuild.
     */
    @Override public java.util.Map<Integer, net.runelite.api.ChatLineBuffer> getChatLineMap() {
        java.util.Map<Integer, java.util.List<net.runelite.api.MessageNode>> byType = new java.util.HashMap<>();
        synchronized (MESSAGES) {
            for (net.runelite.api.MessageNode n : MESSAGES.values()) {
                byType.computeIfAbsent(n.getType().getType(), k -> new java.util.ArrayList<>()).add(n);
            }
        }
        java.util.Map<Integer, net.runelite.api.ChatLineBuffer> out = new java.util.HashMap<>();
        for (java.util.Map.Entry<Integer, java.util.List<net.runelite.api.MessageNode>> e : byType.entrySet()) {
            java.util.List<net.runelite.api.MessageNode> list = e.getValue();
            java.util.Collections.reverse(list);
            final net.runelite.api.MessageNode[] lines = list.toArray(new net.runelite.api.MessageNode[0]);
            out.put(e.getKey(), new net.runelite.api.ChatLineBuffer() {
                @Override public net.runelite.api.MessageNode[] getLines() { return lines; }
                @Override public int getLength() { return lines.length; }
                @Override public void removeMessageNode(net.runelite.api.MessageNode node) {
                    synchronized (MESSAGES) { MESSAGES.values().remove(node); }
                    final int type = node.getType().getType(), id = node.getId();
                    if (id < 0) return;
                    if (isClientThread()) removeFromGame(type, id);
                    else Host.invoke(() -> removeFromGame(type, id));
                }
            });
        }
        return out;
    }

    @Override public net.runelite.api.ScriptEventBuilder createScriptEventBuilder(Object... args) {
        return new AndroidScriptEvent(this, args);
    }

    @Override public int[] getIntStack() { loadStacks(); return intStack; }
    @Override public int getIntStackSize() { loadStacks(); return intStackSize; }
    @Override public void setIntStackSize(int size) { loadStacks(); intStackSize = size; }
    @Override public Object[] getObjectStack() { loadStacks(); return objectStack; }
    @Override public int getObjectStackSize() { loadStacks(); return objectStackSize; }
    @Override public void setObjectStackSize(int size) { loadStacks(); objectStackSize = size; }

    @Override
    public net.runelite.api.ItemComposition getItemDefinition(int id) {
        return AndroidItemComposition.lookup(id);
    }

    @Override
    public net.runelite.api.NPCComposition getNpcDefinition(int id) {
        return AndroidNPCComposition.lookup(id);
    }

    /** Map regions spanned by the loaded scene (13 x 13 chunks from the scene base). */
    @Override public int[] getMapRegions() {
        WorldView wv = getTopLevelWorldView();
        // Desktop has the new scene's regions while LOADING (Herbiboar checks them on GameStateChanged(LOADING)).
        if (wv == null || getGameState().getState() < net.runelite.api.GameState.LOADING.getState()) return new int[0];
        if (AndroidWorldView.instanced()) return AndroidWorldView.instanceRegions();
        int x0 = wv.getBaseX() >> 6, x1 = (wv.getBaseX() + 96) >> 6, y0 = wv.getBaseY() >> 6, y1 = (wv.getBaseY() + 96) >> 6;
        int[] out = new int[(x1 - x0 + 1) * (y1 - y0 + 1)];
        int i = 0;
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++) out[i++] = x << 8 | y;
        return out;
    }

    /** Prayers are varbits (the prayer orbs' state), as RuneLite's own client reads them. */
    @Override public boolean isPrayerActive(net.runelite.api.Prayer p) { return getVarbitValue(p.getVarbit()) == 1; }

    /** Walk destination (Client +0xecdc/+0xece0), null when not walking. */
    @Override public net.runelite.api.coords.LocalPoint getLocalDestinationLocation() {
        int[] d = Native.walkDestination();
        if (d == null || d[0] < 0 || d[1] < 0 || d[0] >= 104 || d[1] >= 104) return null;
        return net.runelite.api.coords.LocalPoint.fromScene(d[0], d[1], getTopLevelWorldView());
    }

    /** Idle time since the last touch or key event (client cycles of 20 ms), as RuneLite counts them. */
    private static int idleCycles() { return (int) Math.min(Integer.MAX_VALUE, (android.os.SystemClock.uptimeMillis() - Sidebar.lastInputMillis) / 20); }
    @Override public int getMouseIdleTicks() { return idleCycles(); }
    /** Wall-clock time of the last touch/key event (desktop compares it with currentTimeMillis). */
    @Override public long getMouseLastPressedMillis() {
        return System.currentTimeMillis() - (android.os.SystemClock.uptimeMillis() - Sidebar.lastInputMillis);
    }
    // The game's own AFK logout isn't driven from here; plugins only read/raise this value.
    private int idleTimeout = 15000;
    private boolean idleTimeoutSet;
    /**
     * The game logs out after its AFKTimeout setting (whole minutes) without input; RuneLite counts
     * in 20 ms ticks. Set values are rounded up to minutes and re-applied at each login.
     */
    @Override public int getIdleTimeout() {
        int m = Native.afkTimeout();
        // The game holds whole minutes: a set value that rounds to the game's minutes reads back as set.
        if (m > 0 && idleTimeoutSet && idleMinutes(idleTimeout) == m) return idleTimeout;
        return m > 0 ? m * 3000 : idleTimeout;
    }
    @Override public void setIdleTimeout(int ticks) {
        idleTimeout = ticks;
        idleTimeoutSet = true;
        if (isClientThread()) applyIdleTimeout();
        else Host.invoke(this::applyIdleTimeout);
    }
    private static int idleMinutes(int ticks) { return (int) Math.max(1, Math.min(35000, ((long) ticks * 20 + 59999) / 60000)); }
    void applyIdleTimeout() {
        if (!idleTimeoutSet) return;
        int minutes = idleMinutes(idleTimeout);
        int before = Native.afkTimeout();
        boolean ok = Native.setAfkTimeout(minutes);
        android.util.Log.i(Host.TAG, "idle timeout: " + before + " -> " + minutes + " min (" + (ok ? "set" : "not set") + ")");
    }
    /** Varbit over the server's copy of its varp (RuneLite's getServerVarbitValue). */
    @Override public int getServerVarbitValue(int id) {
        net.runelite.api.VarbitComposition v = getVarbit(id);
        if (v == null) return 0;
        int lsb = v.getLeastSignificantBit(), msb = v.getMostSignificantBit();
        int bits = msb - lsb + 1;
        int mask = bits >= 32 ? -1 : (1 << bits) - 1;
        return (getServerVarpValue(v.getIndex()) >>> lsb) & mask;
    }
    @Override public int getKeyboardIdleTicks() { return idleCycles(); }
    /**
     * 1 while the screen is held, 0 otherwise; during a WidgetDrag the release cycle reads 0 (desktop:
     * the left button is up on the frame the drag completes). // unverified: touch state values outside drags
     */
    @Override public int getMouseCurrentButton() {
        if (dragEventTouch >= 0) return dragEventTouch == 4 ? 0 : 1;
        return Native.touchState() == 3 ? 1 : 0;
    }
    /** Touch state of the WidgetDrag being posted (Host), -1 outside it. */
    int dragEventTouch = -1;

    /** The game canvas is the overlay canvas (UI-canvas units). */
    @Override public java.awt.Dimension getRealDimensions() { return new java.awt.Dimension(getCanvasWidth(), getCanvasHeight()); }
    @Override public java.awt.Dimension getStretchedDimensions() { return getRealDimensions(); }

    // Widget drag from the game's drag state (docs/offsets.md "Widget drag"); null when nothing is held.
    private static net.runelite.api.widgets.Widget dragWidget(int comp, int index) {
        if (comp < 0) return null;
        return AndroidWidget.of(comp, index);
    }
    @Override public net.runelite.api.widgets.Widget getDraggedWidget() {
        int[] d = Native.dragState();
        return d != null ? dragWidget(d[0], d[1]) : null;
    }
    @Override public net.runelite.api.widgets.Widget getDraggedOnWidget() {
        int[] d = Native.dragState();
        return d != null ? dragWidget(d[2], d[3]) : null;
    }
    @Override public boolean isDraggingWidget() {
        int[] d = Native.dragState();
        return d != null && d[4] != 0;
    }
    @Override public int getDragTime() {
        int[] d = Native.dragState();
        return d != null ? d[5] : 0;
    }
    /**
     * null clears the game's target, so the release does no swap (Bank Tags' preventTagTabDrags,
     * Layouts; verified 2026-10-17 gen 25: a tag-tab layout swap left the real bank order unchanged). Setting
     * another widget is not written to the game. // unverified: non-null targets
     */
    @Override public void setDraggedOnWidget(net.runelite.api.widgets.Widget w) {
        if (w == null) Native.dragClearTarget();
        else android.util.Log.w(Host.TAG, "setDraggedOnWidget(" + w.getId() + "): not written to the game");
    }
    /** The game's widget target mode (+0x9f of the target state, game.cpp target_mode). */
    @Override public boolean isWidgetSelected() { int[] t = Native.targetMode(); return t != null && t[0] != 0; }
    /**
     * The game's target state's active byte, as desktop's flag: false leaves target mode (Wiki after a
     * lookup), true sets it without a selected widget (desktop's flag alone, too). Client thread only.
     */
    @Override public void setWidgetSelected(boolean selected) {
        if (!selected) Native.endTargetMode();
        else if (isClientThread()) Native.beginTargetMode();
    }
    /** As desktop's flag: in target mode every widget is a target (the menu builder's flags read gets bit 21). */
    @Override public void setAllWidgetsAreOpTargetable(boolean yes) {
        if (isClientThread()) Native.setAllOpTargetable(yes);
        else Host.invoke(() -> Native.setAllOpTargetable(yes));
    }
    /**
     * Non-zero = not the live game. The host runs against a private test server, and Loot Tracker
     * and Grand Exchange skip their runelite.net submissions when this is non-zero, which keeps
     * test-server data out of RuneLite's datasets.
     */
    @Override public int getEnvironment() { return 1; }
    /** The game's projectile list (Projectiles, refreshed per client tick). */
    final Projectiles projectiles = new Projectiles();
    @Override public net.runelite.api.Deque<net.runelite.api.Projectile> getProjectiles() { return projectiles; }
    /** Spawns the projectile into the game's list, as the PROJANIM_SPECIFIC packet does (client thread). */
    @Override public net.runelite.api.Projectile createProjectile(int spotanimId, net.runelite.api.coords.WorldPoint source, int sourceHeightOffset,
            net.runelite.api.Actor sourceActor, net.runelite.api.coords.WorldPoint target, int targetHeightOffset, net.runelite.api.Actor targetActor,
            int startCycle, int endCycle, int slope, int startPos) {
        return projectiles.create(new int[] {source.getPlane(), source.getX(), source.getY(), sourceHeightOffset, AndroidProjectile.index(sourceActor),
                target.getPlane(), target.getX(), target.getY(), targetHeightOffset, AndroidProjectile.index(targetActor),
                startCycle, endCycle, slope, startPos, spotanimId});
    }
    /** The pre-WorldPoint form: local start/target coordinates on `plane`; startZ is implied by the tile and start height. */
    @Override public net.runelite.api.Projectile createProjectile(int id, int plane, int startX, int startY, int startZ, int startCycle, int endCycle,
            int slope, int startHeight, int endHeight, net.runelite.api.Actor target, int targetX, int targetY) {
        WorldView wv = getTopLevelWorldView();
        return createProjectile(id, net.runelite.api.coords.WorldPoint.fromLocal(wv, startX, startY, plane), startHeight, null,
                net.runelite.api.coords.WorldPoint.fromLocal(wv, targetX, targetY, plane), endHeight, target, startCycle, endCycle, slope, 0);
    }
    /** The game's ambient (loc) sound list (AmbientSounds). */
    final AmbientSounds ambientSounds = new AmbientSounds();
    @Override public net.runelite.api.Deque<net.runelite.api.AmbientSoundEffect> getAmbientSoundEffects() { return ambientSounds; }
    /** Desktop's world-map loading percentage; the mobile world map isn't modelled. */
    private int expandedMapLoading;
    @Override public int getExpandedMapLoading() { return expandedMapLoading; }
    @Override public void setExpandedMapLoading(int percent) { expandedMapLoading = percent; }
    /** The 8 GE slots read from the game (GeOffers; desktop never returns null here). */
    @Override public net.runelite.api.GrandExchangeOffer[] getGrandExchangeOffers() {
        return GeOffers.read();
    }
    @Override public net.runelite.api.widgets.Widget getSelectedWidget() { return null; }
    // unverified: default (no CS2 input field focus is read from the mobile client yet; Key Remapping only checks it for null)
    @Override public net.runelite.api.widgets.Widget getFocusedInputFieldWidget() { return null; }
    /** The script VM's active component (CC_CREATE/CC_FIND), as desktop's scriptActiveWidget. Game thread. */
    @Override public net.runelite.api.widgets.Widget getScriptActiveWidget() { return scriptWidget(false); }
    @Override public net.runelite.api.widgets.Widget getScriptDotWidget() { return scriptWidget(true); }
    private static net.runelite.api.widgets.Widget scriptWidget(boolean dot) {
        int[] w = Native.scriptActiveWidget(dot);
        if (w == null) return null;
        return AndroidWidget.of(w[0], w[1]);
    }
    /** The game redraws its chat itself; nothing to rebuild. */
    private static void removeFromGame(int type, int id) {
        boolean removed = Native.chatRemove(type, id);
        if (removed) Native.chatChanged();
        android.util.Log.i(Host.TAG, "chat remove type " + type + " id " + id + (removed ? ": removed" : ": not found"));
    }

    /** The game's chat cycle bump: the chatbox rebuilds (shows rewritten or removed lines). */
    @Override public void refreshChat() {
        if (isClientThread()) Native.chatChanged();
        else Host.invoke(Native::chatChanged);
    }

    /**
     * No physical keyboard on a touch screen: Shift is the virtual toggle under the RL button
     * (Ground/Object Markers add their entries to the long-press menu while it is on).
     */
    @Override public boolean isKeyPressed(int keycode) {
        return keycode == net.runelite.api.KeyCode.KC_SHIFT && Native.shiftHeld();
    }
    @Override public net.runelite.api.StructComposition getStructComposition(int id) { return new AndroidStruct(id); }

    /** No "Use" / spell target selection is tracked yet. */

    /**
     * The touch point stands in for the mouse: while a finger is down and for a few seconds after
     * it lifts (Sidebar.hoverPoint), so hover tooltips (Run Energy, Item Stats) show on a tap or a
     * long-press. Otherwise (-1, -1), what RuneLite reports with the mouse off the canvas.
     */
    @Override
    public net.runelite.api.Point getMouseCanvasPosition() {
        java.awt.Point v = DevInput.virtualMouse();
        if (v != null) return new net.runelite.api.Point(v.x, v.y);
        float[] p = Sidebar.hoverPoint();
        int[] screen = Native.screenSize();
        java.awt.Dimension c = OverlayRenderer.canvasSize();
        if (p == null || screen == null || screen[0] <= 0 || screen[1] <= 0 || c.width <= 0) return new net.runelite.api.Point(-1, -1);
        return new net.runelite.api.Point(Math.round(p[0] * c.width / screen[0]), Math.round(p[1] * c.height / screen[1]));
    }

    @Override
    public net.runelite.api.ObjectComposition getObjectDefinition(int id) {
        return AndroidObjectComposition.lookup(id);
    }

    // --- models: decoded by the game's model store (AndroidModelData), lit in Java (AndroidModel) ---

    @Override public net.runelite.api.ModelData loadModelData(int id) { return AndroidModelData.load(id); }
    @Override public net.runelite.api.ModelData mergeModels(net.runelite.api.ModelData... m) { return AndroidModelData.merge(m, m.length); }
    @Override public net.runelite.api.ModelData mergeModels(net.runelite.api.ModelData[] m, int n) { return AndroidModelData.merge(m, n); }
    @Override public net.runelite.api.Model loadModel(int id) { return loadModel(id, null, null); }

    @Override public net.runelite.api.Model applyTransformations(net.runelite.api.Model m, net.runelite.api.Animation a, int fa, net.runelite.api.Animation b, int fb) {
        return m instanceof AndroidModel ? ((AndroidModel) m).transformed(a, fa, b, fb) : null;
    }

    /** Desktop's loadModel: the ModelData, recoloured find[i] -> replace[i], lit with the defaults; null if not loaded. */
    @Override public net.runelite.api.Model loadModel(int id, short[] find, short[] replace) {
        net.runelite.api.ModelData md = loadModelData(id);
        if (md == null) return null;
        if (find != null && replace != null) {
            md = md.cloneColors();
            for (int i = 0; i < Math.min(find.length, replace.length); i++) md.recolor(find[i], replace[i]);
        }
        return md.light();
    }
    @Override public net.runelite.api.RuneLiteObject createRuneLiteObject() { return new net.runelite.api.RuneLiteObject(this); }
    // RuneLiteObjects (Quest Helper's fake NPCs): registration is tracked so isActive() answers as on
    // desktop, but nothing is drawn (no scene renderables are injected; orchestrator decision, gen 28).
    private final java.util.Set<net.runelite.api.RuneLiteObjectController> runeLiteObjects =
            java.util.Collections.synchronizedSet(java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<net.runelite.api.RuneLiteObjectController, Boolean>()));
    @Override public void registerRuneLiteObject(net.runelite.api.RuneLiteObjectController o) { if (o != null) runeLiteObjects.add(o); }
    @Override public void removeRuneLiteObject(net.runelite.api.RuneLiteObjectController o) { runeLiteObjects.remove(o); }
    @Override public boolean isRuneLiteObjectRegistered(net.runelite.api.RuneLiteObjectController o) { return runeLiteObjects.contains(o); }

    // Animations aren't read from the game (SeqType): an inert one keeps fake-NPC code that reads
    // getNumFrames() after setAnimation() from failing (Quest Helper's FakeNpc). Nothing animates.
    // unverified: placeholder values (one frame, no frame lengths, no hand items)
    @Override public net.runelite.api.Animation loadAnimation(final int id) {
        if (id < 0) return null;
        return new net.runelite.api.Animation() {
            private int restartMode;
            @Override public int getId() { return id; }
            @Override public boolean isMayaAnim() { return false; }
            @Override public int getNumFrames() { return 1; }
            @Override public int getRestartMode() { return restartMode; }
            @Override public void setRestartMode(int m) { restartMode = m; }
            @Override public int getDuration() { return 0; }
            @Override public int getFrameStep() { return 0; }
            @Override public int[] getFrameLengths() { return new int[] {1}; }
            @Override public int getLeftHandItem() { return -1; }
            @Override public int getRightHandItem() { return -1; }
        };
    }

    // Hint arrows: not read from the game (HINT_ARROW packet); plugin-set arrows are remembered only.
    private Object hintArrow;
    @Override public boolean hasHintArrow() { return hintArrow != null; }
    @Override public int getHintArrowType() { return hintArrow instanceof net.runelite.api.NPC ? 1 : hintArrow instanceof Player ? 10 : hintArrow != null ? 2 : 0; }
    @Override public void clearHintArrow() { hintArrow = null; }
    @Override public void setHintArrow(net.runelite.api.coords.WorldPoint p) { hintArrow = p; }
    @Override public void setHintArrow(net.runelite.api.coords.LocalPoint p) { hintArrow = p; }
    @Override public void setHintArrow(Player p) { hintArrow = p; }
    @Override public void setHintArrow(net.runelite.api.NPC n) { hintArrow = n; }
    @Override public net.runelite.api.coords.WorldPoint getHintArrowPoint() { return hintArrow instanceof net.runelite.api.coords.WorldPoint ? (net.runelite.api.coords.WorldPoint) hintArrow : null; }
    @Override public Player getHintArrowPlayer() { return hintArrow instanceof Player ? (Player) hintArrow : null; }
    @Override public net.runelite.api.NPC getHintArrowNpc() { return hintArrow instanceof net.runelite.api.NPC ? (net.runelite.api.NPC) hintArrow : null; }

    @Override
    public net.runelite.api.EnumComposition getEnum(int id) {
        return AndroidEnum.lookup(id);
    }
    /**
     * The game's world map (Client +0x4186a0, the object behind the CS2 worldmap_* opcodes; static RE
     * gen 31, docs/offsets.md "World map"): position, zoom, loaded flag, the surface map area test and
     * the smooth jump, and the regions' map icons (gen 32; region tiles are not read).
     */
    static final int SURFACE_MAP = 0;  // map area id 0 = the main surface map // unverified: from desktop
    private final WorldMapBag worldMap = new WorldMapBag() {
        private int[] state() {
            int[] s = Native.worldMap();
            return s != null && s[0] != 0 ? s : null;
        }
        private final WorldMapDataBag data = new WorldMapDataBag() {
            /**
             * worldmap_coordinmap on the surface map area, level 0. When the area table isn't
             * readable, the surface coordinate band (underground and separate areas sit at
             * y >= 4160 or x < 1024). Quest Helper's far minimap arrow and World Map's overlay use it.
             * // unverified: map id 0 / level 0 as desktop's surfaceContainsPosition
             */
            @Override public boolean surfaceContainsPosition(int x, int y) {
                int r = Native.worldMapContains(SURFACE_MAP, 0, x, y);
                if (r >= 0) return r == 1;
                return x >= 1024 && x < 4160 && y >= 2304 && y < 4160;
            }
        };
        @Override public net.runelite.api.worldmap.WorldMapData getWorldMapData() { return data; }
        @Override public net.runelite.api.Point getWorldMapPosition() {
            int[] s = state();
            return s == null || s[3] < 0 ? null : new net.runelite.api.Point(s[3], s[4]);
        }
        @Override public float getWorldMapZoom() {
            int[] s = state();
            return s == null ? 4f : s[2] / 1000f;
        }
        @Override public void setWorldMapPositionTarget(net.runelite.api.coords.WorldPoint p) {
            if (p != null) Native.worldMapJump(p.getPlane(), p.getX(), p.getY());  // unverified: on the device
        }
        private final net.runelite.api.worldmap.WorldMapRenderer renderer = new net.runelite.api.worldmap.WorldMapRenderer() {
            @Override public boolean isLoaded() {
                int[] s = state();
                return s != null && s[1] != 0;
            }
            private net.runelite.api.worldmap.WorldMapRegion[][] regions = new net.runelite.api.worldmap.WorldMapRegion[0][0];
            private int regionsCycle = Integer.MIN_VALUE, regionsMap = Integer.MIN_VALUE;
            /**
             * The current map's region grid with its icons (game.cpp world_map_icons; outer index =
             * the game's row array). World Map reads it every client tick while loaded, so it is
             * re-read when the map changes and otherwise every 50 cycles. Tiles aren't read.
             */
            @Override public net.runelite.api.worldmap.WorldMapRegion[][] getMapRegions() {
                int[] s = state();
                int map = s == null ? -1 : s[5];
                int cycle = Native.gameCycle();
                if (map == regionsMap && regionsCycle != Integer.MIN_VALUE && cycle >= regionsCycle && cycle - regionsCycle < 50) return regions;
                int[] v = Native.worldMapIcons();
                if (v == null) return regions;  // off the client thread or not loaded: last result
                regionsCycle = cycle;
                regionsMap = map;
                int rows = v[0], cols = v[1];
                @SuppressWarnings("unchecked")
                java.util.List<net.runelite.api.worldmap.WorldMapIcon>[][] icons = new java.util.List[rows][cols];
                for (int i = 2; i + 5 < v.length; i += 6) {
                    int r = v[i], c = v[i + 1];
                    if (r < 0 || r >= rows || c < 0 || c >= cols) continue;
                    if (icons[r][c] == null) icons[r][c] = new java.util.ArrayList<>();
                    final int type = v[i + 2];
                    final net.runelite.api.coords.WorldPoint at = new net.runelite.api.coords.WorldPoint(v[i + 4], v[i + 5], v[i + 3]);
                    icons[r][c].add(new net.runelite.api.worldmap.WorldMapIcon() {
                        @Override public int getType() { return type; }
                        @Override public net.runelite.api.coords.WorldPoint getCoordinate() { return at; }
                        @Override public String toString() { return "WorldMapIcon(" + type + " @ " + at + ")"; }
                    });
                }
                net.runelite.api.worldmap.WorldMapRegion[][] out = new net.runelite.api.worldmap.WorldMapRegion[rows][cols];
                for (int r = 0; r < rows; r++) {
                    for (int c = 0; c < cols; c++) {
                        final java.util.Collection<net.runelite.api.worldmap.WorldMapIcon> list = icons[r][c] == null
                            ? java.util.Collections.<net.runelite.api.worldmap.WorldMapIcon>emptyList()
                            : java.util.Collections.unmodifiableList(icons[r][c]);
                        out[r][c] = () -> list;
                    }
                }
                regions = out;
                return out;
            }
        };
        @Override public net.runelite.api.worldmap.WorldMapRenderer getWorldMapRenderer() { return renderer; }
    };
    @Override public net.runelite.api.worldmap.WorldMap getWorldMap() { return worldMap; }

    /** Deprecated pre-WorldMap API (still used by hub plugins, e.g. herbi-afk): a view of {@link #worldMap}. */
    private final net.runelite.api.RenderOverview renderOverview = new net.runelite.api.RenderOverview() {
        private final net.runelite.api.WorldMapData data = (x, y) -> worldMap.getWorldMapData().surfaceContainsPosition(x, y);
        @Override public net.runelite.api.WorldMapData getWorldMapData() { return data; }
        @Override public net.runelite.api.Point getWorldMapPosition() { return worldMap.getWorldMapPosition(); }
        @Override public float getWorldMapZoom() { return worldMap.getWorldMapZoom(); }
        @Override public void setWorldMapPositionTarget(net.runelite.api.coords.WorldPoint p) { worldMap.setWorldMapPositionTarget(p); }
        @Override public net.runelite.api.worldmap.WorldMapRenderer getWorldMapRenderer() { return worldMap.getWorldMapRenderer(); }
        @Override public void initializeWorldMap(net.runelite.api.worldmap.WorldMapData d) { worldMap.initializeWorldMap(d); }
    };
    @Override public net.runelite.api.RenderOverview getRenderOverview() { return renderOverview; }
    /**
     * Map element (mel) config: category from the game's table (game.cpp map_element). An id the table
     * doesn't hold gets category -1 rather than null: World Map dereferences it for every map icon.
     */
    private final java.util.Map<Integer, net.runelite.api.worldmap.MapElementConfig> mapElements = new java.util.HashMap<>();
    @Override public net.runelite.api.worldmap.MapElementConfig getMapElementConfig(int id) {
        synchronized (mapElements) {
            net.runelite.api.worldmap.MapElementConfig cached = mapElements.get(id);
            if (cached != null) return cached;
        }
        int[] m = Native.mapElement(id);
        final int category = m == null ? -1 : m[0];  // unverified: default for unknown ids
        net.runelite.api.worldmap.MapElementConfig config = new net.runelite.api.worldmap.MapElementConfig() {
            @Override public int getCategory() { return category; }
            @Override public net.runelite.api.SpritePixels getMapIcon(boolean large) { return null; }  // unverified: default (sprite id m[1] not decoded)
        };
        if (m != null) synchronized (mapElements) { if (mapElements.size() < 65536) mapElements.put(id, config); }
        return config;
    }
    /** Minimap zoom isn't read from the game; RuneLite's default. */
    /** The game's minimap zoom in pixels per tile (whole numbers 2..8, default 4); set through its setter (CS2 7252). */
    @Override public double getMinimapZoom() { int z = Native.minimapZoom(); return z > 0 ? z : 4.0; }
    @Override public void setMinimapZoom(double zoom) {
        int z = (int) Math.round(zoom);
        if (isClientThread()) Native.setMinimapZoom(z);
        else Host.invoke(() -> Native.setMinimapZoom(z));
    }
    @Override public void setMinimapZoom(boolean enabled) { if (isClientThread()) Native.setMinimapZoomInput(enabled); }
    /**
     * Desktop's minimap dot sprites (item, NPC, player, friend, team, friends chat, clan). The
     * mobile client styles its dots per category instead (dot colour, outline colour, thickness;
     * its `setminimapdotstyle` console command, game `minimap_dot_style`). Plugins get placeholder
     * sprites; when one replaces an entry (Minimap's recolouring), `applyMapDots` sets that
     * category's dot colour to the sprite's main colour, and putting the placeholder back
     * restores the original style. // unverified: not run on the device yet
     */
    private final net.runelite.api.SpritePixels[] mapDotPlaceholders = newMapDots();
    private final net.runelite.api.SpritePixels[] mapDots = mapDotPlaceholders.clone();
    private final net.runelite.api.SpritePixels[] mapDotsSeen = mapDotPlaceholders.clone();
    private final int[][] mapDotOriginal = new int[7][];
    /**
     * Desktop map dot index (item, NPC, player, friend, team, friends chat, clan) -> the mobile
     * minimap's category. The dot draw 0x58cab4 takes the category in w6: items 5 and NPCs 6
     * (constants at its callers; verified on device, gen 22), players by precedence in 0x58c6f0..
     * 0x58c79c: 0 (0x461564), 1 (team, player +0x790), 2 (0x4615ec), 3 (0x4774d0), else 4.
     * // unverified: 0 = friend, 2 = friends chat, 3 = clan are inferred from desktop's precedence
     */
    private static final int[] MOBILE_DOT_CATEGORY = {5, 6, 4, 0, 1, 2, 3};
    /** Desktop's default dot colours, for restoring a category the game hadn't styled. */
    private static final int[] MAP_DOT_DEFAULTS = {0xff0000, 0xffff00, 0xffffff, 0x00ff00, 0x0000ff, 0x8000ff, 0x00ffff};
    private static net.runelite.api.SpritePixels[] newMapDots() {
        net.runelite.api.SpritePixels[] a = new net.runelite.api.SpritePixels[7];
        for (int i = 0; i < a.length; i++) a[i] = new HostSpritePixels(new int[16], 4, 4);
        return a;
    }
    @Override public net.runelite.api.SpritePixels[] getMapDots() { return mapDots; }

    /** Game thread, every few cycles: pushes replaced map dot sprites into the game's dot styles. */
    void applyMapDots() {
        if (getGameState() != GameState.LOGGED_IN) return;  // the minimap's base dot isn't set up before
        for (int i = 0; i < 7; i++) {
            net.runelite.api.SpritePixels sp = mapDots[i];
            if (sp == mapDotsSeen[i]) continue;
            int cat = MOBILE_DOT_CATEGORY[i];
            int[] cur = Native.minimapDot(cat, 0, 0, -1);
            android.util.Log.i(Host.TAG, "map dot " + i + " (mobile category " + cat + ") replaced; game style " + java.util.Arrays.toString(cur) + (sp != null ? " colour " + Integer.toHexString(mainColor(sp)) : ""));
            if (cur == null) return;  // minimap not built yet: retry later
            if (mapDotOriginal[i] == null) mapDotOriginal[i] = cur;
            int[] o = mapDotOriginal[i];
            if (sp == null || sp == mapDotPlaceholders[i]) {
                if (o[0] != 0) Native.minimapDot(cat, o[1], o[2], o[3]);
                else Native.minimapDot(cat, MAP_DOT_DEFAULTS[i], 0, 1);
            } else {
                Native.minimapDot(cat, mainColor(sp), o[0] != 0 ? o[2] : 0, o[0] != 0 ? o[3] : 1);
            }
            mapDotsSeen[i] = sp;
        }
    }

    /** The most common opaque, non-black colour of a sprite (a dot's fill), RGB. */
    private static int mainColor(net.runelite.api.SpritePixels sp) {
        java.util.Map<Integer, Integer> counts = new java.util.HashMap<>();
        int best = 0, bestN = 0;
        for (int p : sp.getPixels()) {
            int rgb = p & 0xffffff;
            if (p == 0 || rgb == 0) continue;
            int n = counts.merge(rgb, 1, Integer::sum);
            if (n > bestN) { bestN = n; best = rgb; }
        }
        return best;
    }

    /** The long-press menu (game::read_menu); plugin-created entries stay host-side for now. */
    static final AndroidMenu MENU = new AndroidMenu();
    @Override public boolean isMenuOpen() { return !AndroidMenu.presorting && Native.menuOpen(); }
    @Override public int getMenuX() { return MENU.getMenuX(); }
    @Override public int getMenuY() { return MENU.getMenuY(); }
    @Override public int getMenuWidth() { return MENU.getMenuWidth(); }
    @Override public int getMenuHeight() { return MENU.getMenuHeight(); }
    @Override public boolean isMenuScrollable() { return false; }
    /** The long-press menu's scroll offset in pixels (the game clamps it at its next layout). */
    @Override public int getMenuScroll() { return Native.menuScroll(); }
    @Override public void setMenuScroll(int v) { if (isClientThread()) Native.setMenuScroll(v); }
    @Override public net.runelite.api.Menu getMenu() { return MENU; }
    @Override public net.runelite.api.MenuEntry[] getMenuEntries() { return MENU.getMenuEntries(); }
    @Override public void setMenuEntries(net.runelite.api.MenuEntry[] e) { MENU.setMenuEntries(e); }
    @Override public net.runelite.api.MenuEntry createMenuEntry(int idx) { return MENU.createMenuEntry(idx); }

    /** Only the top-level world view exists (no boats / world entities yet). */
    @Override
    public WorldView findWorldViewFromWorldPoint(net.runelite.api.coords.WorldPoint p) {
        return getTopLevelWorldView();
    }

    /** CS1 text macros aren't expanded; widget text set by the server/scripts is returned as is. */
    @Override
    public String macroExpand(String text) {
        return text;
    }

    @Override
    public Player getLocalPlayer() {
        // RuneLite keeps the local player through LOADING (teleports, region changes); QH's
        // NpcStep.scanForNpcs calls getLocalPlayer().getWorldView() on GameStateChanged(LOADING).
        GameState s = getGameState();
        return s == GameState.LOGGED_IN || s == GameState.LOADING ? Host.localPlayer() : null;
    }

    // --- camera and viewport (the viewport is the whole logical overlay canvas) ---

    /**
     * Projection scale fitted to on-screen NPC positions at the default zoom: 285 for a 720-high
     * canvas (docs/offsets.md), scaled to the canvas height. The zoom field isn't mapped yet.
     */
    static volatile int scaleOverride = -1;

    @Override public int getCameraX() { return Math.round(getCameraFpX()); }
    @Override public int getCameraY() { return Math.round(getCameraFpY()); }
    @Override public int getCameraZ() { return Math.round(getCameraFpZ()); }

    /**
     * Experimental (`setprop debug.osrsrl.eyeback D`): moves the reported camera back by D units
     * along the view axis. Perspective's depth then gains D while the lateral terms stay, which tests
     * whether the game's eye sits behind its camera position (the fitted scale only matches at one depth).
     */
    private float eyeBack;
    private long eyeCheckedAt;
    private float eyeBack() {
        long now = android.os.SystemClock.uptimeMillis();
        if (now - eyeCheckedAt > 1000) {
            eyeCheckedAt = now;
            try { String v = Native.property("debug.osrsrl.eyeback"); eyeBack = v.isEmpty() ? 0 : Float.parseFloat(v); }
            catch (NumberFormatException ignored) {}
        }
        return eyeBack;
    }
    private float eyeOffset(int axis) {
        float d = eyeBack();
        if (d == 0) return 0;
        int[] cam = cam();
        double pitch = cam[3] * Math.PI * 2 / 16384, yaw = cam[4] * Math.PI * 2 / 16384;
        double[] f = {-Math.sin(yaw) * Math.cos(pitch), Math.cos(yaw) * Math.cos(pitch), Math.sin(pitch)};
        return (float) (-d * f[axis]);
    }
    @Override public int getCameraPitch() { return cam()[3]; }
    @Override public int getCameraYaw() { return cam()[4]; }
    // Targets (+0x8949c/+0x894a0): the game eases pitch/yaw towards them (a swipe moves the targets).
    @Override public int getCameraYawTarget() { return cam()[12]; }
    @Override public int getCameraPitchTarget() { return cam()[11]; }
    @Override public void setCameraYawTarget(int yaw) { setCameraTarget(1, yaw); }
    @Override public void setCameraPitchTarget(int pitch) { setCameraTarget(0, pitch); }
    private int cameraTargetWrites;
    private void setCameraTarget(int which, int value) {
        if (!isClientThread()) return;
        int before = cam()[11 + which];
        boolean ok = Native.setCameraTarget(which, value);
        if (ok) cameraCache = null;
        // camlog: every 30th write, for checking plugins that steer the camera (Camera Smoothing)
        if ((cameraTargetWrites++ % 30) == 0 && "1".equals(Native.property("debug.osrsrl.camlog")))
            android.util.Log.i(Host.TAG, "camera target " + (which == 0 ? "pitch" : "yaw") + " " + before + " -> " + value + ": " + ok
                + " (now " + java.util.Arrays.toString(cam()) + ")");
    }
    @Override public net.runelite.api.CameraFocusableEntity getCameraFocusEntity() { return getLocalPlayer(); }
    @Override public float getCameraFpX() { return cam()[0] + eyeOffset(0); }
    @Override public float getCameraFpY() { return cam()[1] + eyeOffset(1); }
    @Override public float getCameraFpZ() { return cam()[2] + eyeOffset(2); }
    @Override public float getCameraFpPitch() { return (float) (getCameraPitch() * Math.PI * 2 / 16384); }
    @Override public float getCameraFpYaw() { return (float) (getCameraYaw() * Math.PI * 2 / 16384); }
    private long scaleCheckedAt;

    @Override
    public int getScale() {
        long now = System.currentTimeMillis();
        if (now - scaleCheckedAt > 1000) {  // `setprop debug.osrsrl.scale N` overrides, for calibration
            scaleCheckedAt = now;
            try {
                String v = Native.property("debug.osrsrl.scale");
                scaleOverride = v.isEmpty() ? -1 : Integer.parseInt(v);
            } catch (NumberFormatException ignored) {
            }
        }
        if (scaleOverride > 0) return scaleOverride;
        int[] z = gameViewport();
        // As the desktop client's viewportZoom: zoom * viewport height / 334.
        return z != null ? z[0] * z[4] / 334 : Math.round(285f * getViewportHeight() / 720f);
    }

    /**
     * The game's 3D viewport in UI canvas pixels: {zoom, x, y, width, height} from Client +0x46d4 and
     * +0x4720..+0x472c (0x5561e8, RuneLite's viewportZoom/viewport bounds; follows the zoom settings
     * and pinch zoom). Null while unset; `setprop debug.osrsrl.scale N` still wins for getScale.
     */
    private int[] gameViewport() {
        int stamp = Host.cycleStamp;
        if (viewportStamp != stamp || viewportCache == null) {
            int[] z = Native.viewportZoom();
            viewportCache = new Object[] {z != null && z[0] > 0 && z[3] > 0 && z[4] > 0 ? z : null};
            viewportStamp = stamp;
        }
        return (int[]) viewportCache[0];
    }
    // Camera and viewport are read once per client cycle: Perspective calls the getters per vertex
    // (NPC model hulls), and a JNI read each time took the game to ~0 fps with many NPCs highlighted.
    private int viewportStamp = -1, cameraStamp = -1;
    private Object[] viewportCache;
    private int[] cameraCache;
    private int[] cam() {
        int stamp = Host.cycleStamp;
        if (cameraStamp != stamp || cameraCache == null) {
            cameraCache = Native.camera();
            cameraStamp = stamp;
        }
        return cameraCache;
    }
    @Override public boolean isGpu() { return (gpuFlags & 1) != 0; }
    @Override public boolean isResized() { return true; }
    /** AWT events dispatched here become Android input (DevInput). */
    @Override public java.awt.Canvas getCanvas() { return DevInput.CANVAS; }
    /** Runs an entry through the game's menu (DevMenu). Game thread. */
    @Override public void menuAction(int p0, int p1, net.runelite.api.MenuAction action, int id, int itemId, String option, String target) {
        DevMenu.menuAction(p0, p1, action, id, itemId, option, target);
    }
    /** RuneLite's 3D zoom is the viewport zoom (same value as getScale). */
    @Override public int get3dZoom() { return getScale(); }
    @Override public int getViewportXOffset() { int[] z = gameViewport(); return z != null ? z[1] : 0; }
    @Override public int getViewportYOffset() { int[] z = gameViewport(); return z != null ? z[2] : 0; }
    @Override public int getViewportWidth() { int[] z = gameViewport(); return z != null ? z[3] : OverlayRenderer.canvasSize().width; }
    @Override public int getViewportHeight() { int[] z = gameViewport(); return z != null ? z[4] : OverlayRenderer.canvasSize().height; }
    /** Centre of the 3D viewport on the canvas. */
    @Override public int getCenterX() { return getViewportXOffset() + getViewportWidth() / 2; }
    @Override public int getCenterY() { return getViewportYOffset() + getViewportHeight() / 2; }
    /** Frames per second the game draws (BeforeRender counted per second; the ImGui overlay shows the same rate). */
    @Override public int getFPS() { return Host.fps; }
    // No Jagex launcher on mobile (null display name, as RuneLite started without it).
    // Stretched mode is the game's own UiScaling (Native.uiScaling): it draws the UI canvas at the surface size
    // divided by percent/100, uniformly, as desktop's resizable stretched mode draws at 1 + scalingFactor/100.
    // While enabled the setting is 100 + scalingFactor (floored to whole multiples of 100 with integer scaling);
    // disabled, the game's own value is put back. Not saved. Keep-aspect-ratio is a fixed-mode option on desktop:
    // the game's scale is always uniform, so it is kept as given. Fast (nearest-neighbour) changes nothing here.
    private boolean stretched, stretchedFast, integerScaling, keepAspectRatio;
    private int scalingFactor, uiOwn = -1, uiWritten = -1;
    @Override public boolean isStretchedEnabled() { return stretched; }
    @Override public void setStretchedEnabled(boolean on) { stretched = on; applyStretch(); }
    @Override public boolean isStretchedFast() { return stretchedFast; }
    @Override public void setStretchedFast(boolean on) { stretchedFast = on; }
    @Override public void setStretchedIntegerScaling(boolean on) { integerScaling = on; applyStretch(); }
    @Override public void setStretchedKeepAspectRatio(boolean on) { keepAspectRatio = on; }
    public boolean isStretchedKeepAspectRatio() { return keepAspectRatio; }
    @Override public void setScalingFactor(int factor) { scalingFactor = factor; applyStretch(); }
    @Override public void invalidateStretching(boolean resize) { }  // nothing is cached: the canvas is read per cycle
    /** The UiScaling percent stretched mode asks for, or the game's own while disabled. */
    private void applyStretch() {
        if (!isClientThread()) { Host.invoke(this::applyStretch); return; }
        int cur = Native.uiScaling();
        if (cur < 0) return;
        if (cur != uiWritten) uiOwn = cur;  // the game's (or the player's) value since the last write
        int want = uiOwn;
        if (stretched) {
            want = 100 + scalingFactor;
            if (integerScaling) want = Math.max(100, want / 100 * 100);
            want = Math.max(25, Math.min(400, want));
        }
        if (want != cur && Native.setUiScaling(want)) uiWritten = want;
    }
    /** As desktop: the canvas is laid out again for the current surface size (the game's own layout, 0x448588). */
    @Override public void resizeCanvas() {
        if (isClientThread()) Native.relayout();
        else Host.invoke(Native::relayout);
    }
    // The GPU flags a GPU plugin sets; the game always renders with its own GL pipeline, so only isGpu reads them.
    private int gpuFlags;
    @Override public void setGpuFlags(int flags) { gpuFlags = flags; }
    // Unlocked fps is the game's frame limit (its FpsLimit setting applied, Native.fpsLimit): the target while
    // unlocked (0 = none), the game's own back when locked. The setting itself is left as it is.
    private boolean unlockedFps;
    private int unlockedFpsTarget, fpsOwn = -1, fpsWritten = -1;
    @Override public void setUnlockedFps(boolean on) { unlockedFps = on; applyFps(); }
    @Override public void setUnlockedFpsTarget(int fps) { unlockedFpsTarget = fps; applyFps(); }
    private void applyFps() {
        if (!isClientThread()) { Host.invoke(this::applyFps); return; }
        int cur = Native.fpsLimit();
        if (cur < 0) return;
        if (cur != fpsWritten) fpsOwn = cur;
        int want = unlockedFps ? Math.max(0, unlockedFpsTarget) : fpsOwn;
        if (want != cur && Native.setFpsLimit(want)) fpsWritten = Native.fpsLimit();
    }
    private static final int COMPASS_SPRITE = 169;  // the minimap compass graphic
    /** The minimap compass is the game's sprite 169: an override of it; null puts the game's back. */
    @Override public void setCompass(net.runelite.api.SpritePixels sp) {
        if (sp == null) spriteOverrides.remove(COMPASS_SPRITE);
        else spriteOverrides.put(COMPASS_SPRITE, sp);
    }
    /**
     * Desktop calls the drawer for every tile while it builds the minimap image, the Rasterizer drawing into it.
     * Here the game's own build (0x549634) runs first; after each one (seen at the end of the cycle) the drawer is
     * called for the same tiles with the Rasterizer on a copy of the game's minimap raster, written back after.
     */
    private net.runelite.api.TileFunction minimapTileDrawer;
    @Override public void setMinimapTileDrawer(net.runelite.api.TileFunction f) {
        minimapTileDrawer = f;
        Native.setMinimapWatch(f != null);
    }
    /** Game thread, each cycle. */
    void drawMinimapTiles() {
        net.runelite.api.TileFunction f = minimapTileDrawer;
        int plane = f != null ? Native.minimapDrawn() : -1;
        if (plane < 0) return;
        int[] r = Native.minimapRaster();
        net.runelite.api.Tile[][][] tiles = getTopLevelWorldView().getScene().getTiles();
        if (r == null || tiles == null || plane >= tiles.length || tiles[plane].length < 103 || tiles[plane][0].length < 103) return;
        int size = r[0], zoom = r[1];
        int[] px = java.util.Arrays.copyOfRange(r, 2, r.length);
        AndroidRender.RASTERIZER.target(px, size, size);
        try {
            for (int y = 1; y < 103; y++)
                for (int x = 1; x < 103; x++) {
                    net.runelite.api.Tile t = tiles[plane][x][y];
                    int px0 = 48 + x * zoom, py0 = 48 + (103 - y) * zoom;
                    if (t != null) f.drawTile(t, x, y, px0, py0, px0 + zoom, py0 + zoom);
                }
        } finally {
            AndroidRender.RASTERIZER.target(null, 0, 0);
        }
        Native.setMinimapRaster(px);
    }
    // The Android app starts its engine and is never held at startup: the host loads once the game runs, so
    // initialize and unblockStartup find the state they produce on desktop already reached.
    @Override public void initialize() { }
    @Override public void unblockStartup() { }
    /** RuneLite's loader hands the jav_config here; the app loads its own (patched to the local server) before the host. */
    private net.runelite.api.ClientConfiguration configuration;
    @Override public void setConfiguration(net.runelite.api.ClientConfiguration c) { configuration = c; }
    // Low memory: the Android game has no low-detail mode (static RE run 302: no flag in its loc loading or its
    // settings). Desktop's low detail loads no ground decorations a player can't interact with; here they are taken
    // out of the game's scene as they load (SceneObjects) and the scene is reloaded to bring them back.
    volatile boolean lowMemory;
    @Override public void changeMemoryMode(boolean low) {
        if (!isClientThread()) { Host.invoke(() -> changeMemoryMode(low)); return; }
        boolean was = lowMemory;
        lowMemory = low;
        if (low && !was) {
            for (net.runelite.api.Tile[][] plane : getTopLevelWorldView().getScene().getTiles())
                for (net.runelite.api.Tile[] col : plane)
                    for (net.runelite.api.Tile t : col) {
                        net.runelite.api.GroundObject g = t != null ? t.getGroundObject() : null;
                        if (g != null && lowDetailDrops(g.getId())) t.setGroundObject(null);
                    }
        } else if (!low && was) Native.reloadScene();
    }
    // shortcut: "interactive" is any action or an impostor (desktop also keeps by the loc's interact type and a
    // flag the API doesn't carry); upgrade when AndroidObjectComposition reads those fields.
    boolean lowDetailDrops(int id) {
        if (!lowMemory) return false;
        net.runelite.api.ObjectComposition d = getObjectDefinition(id);
        if (d == null || d.getImpostorIds() != null) return false;
        for (String a : d.getActions()) if (a != null) return false;
        return true;
    }

    // --- rendering surface (AndroidRender says what each means on Android) ---
    @Override public net.runelite.api.Rasterizer getRasterizer() { return AndroidRender.RASTERIZER; }
    @Override public net.runelite.api.TextureProvider getTextureProvider() { return AndroidRender.TEXTURES; }
    // The game's own loaded sprite arrays (Native.spriteArray), copied on the game thread; null before
    // they are loaded or off the game thread. Copies: writing them doesn't change what the game draws.
    @Override public net.runelite.api.SpritePixels[] getCrossSprites() {
        int[] a = Native.isClientThread() ? Native.spriteArray(0) : null;
        if (a == null) return null;
        net.runelite.api.SpritePixels[] out = new net.runelite.api.SpritePixels[a[0]];
        for (int i = 0, o = 1; i < out.length; i++) {
            int w = a[o], h = a[o + 1], n = a[o + 6];
            HostSpritePixels s = new HostSpritePixels(java.util.Arrays.copyOfRange(a, o + 7, o + 7 + w * h), w, h);
            s.setOffsetX(a[o + 2]);
            s.setOffsetY(a[o + 3]);
            s.setMaxWidth(a[o + 4]);
            s.setMaxHeight(a[o + 5]);
            out[i] = s;
            o += 7 + w * h + n;
        }
        return out;
    }
    @Override public net.runelite.api.IndexedSprite[] getMapScene() {
        int[] a = Native.isClientThread() ? Native.spriteArray(1) : null;
        if (a == null) return null;
        net.runelite.api.IndexedSprite[] out = new net.runelite.api.IndexedSprite[a[0]];
        for (int i = 0, o = 1; i < out.length; i++) {
            int w = a[o], h = a[o + 1], n = a[o + 6];
            HostIndexedSprite s = HostIndexedSprite.blank(w, h);
            byte[] px = s.getPixels();
            for (int k = 0; k < w * h; k++) px[k] = (byte) a[o + 7 + k];
            s.setPalette(java.util.Arrays.copyOfRange(a, o + 7 + w * h, o + 7 + w * h + n));
            s.setOffsetX(a[o + 2]);
            s.setOffsetY(a[o + 3]);
            s.setOriginalWidth(a[o + 4]);
            s.setOriginalHeight(a[o + 5]);
            out[i] = s;
            o += 7 + w * h + n;
        }
        return out;
    }
    /** The map function icons (desktop's mapIcons: gameval SpriteID.Mapfunction._0.._n), frame 0 of each from the game's sprite cache. */
    private net.runelite.api.SpritePixels[] mapIcons;
    @Override public net.runelite.api.SpritePixels[] getMapIcons() {
        if (mapIcons != null) return mapIcons;
        if (!isClientThread()) return null;
        java.util.List<net.runelite.api.SpritePixels> out = new java.util.ArrayList<>();
        for (int i = 0; ; i++) {
            int id;
            try {
                id = net.runelite.api.gameval.SpriteID.Mapfunction.class.getField("_" + i).getInt(null);
            } catch (ReflectiveOperationException e) {
                break;
            }
            int[] r = Native.sprite(id);
            if (r == null && i == 0) return null;  // the sprites archive isn't loaded yet: try again later
            out.add(r == null ? null : new HostSpritePixels(java.util.Arrays.copyOfRange(r, 2, 2 + r[0] * r[1]), r[0], r[1]));
        }
        return mapIcons = out.toArray(new net.runelite.api.SpritePixels[0]);
    }
    @Override public net.runelite.api.BufferProvider getBufferProvider() { AndroidRender.BUFFER.request(); return AndroidRender.BUFFER; }
    /**
     * Desktop's Rasterizer3D clip, relative to the 3D centre: the scene renderer's viewport
     * (Native.viewport: centre x/y, clip x0, y0, x1, y1) on the game thread, else the 3D viewport's size.
     */
    private int[] clip3d() {
        // renderer +0x630c60 +0x24..+0x30 (setter 0x730cbc): -cx, w - cx, -cy, h - cy in screen pixels,
        // scaled here to the canvas the viewport getters use.
        int[] v = isClientThread() ? Native.viewport() : null;
        int w = getViewportWidth(), h = getViewportHeight();
        if (v != null && v[0] != 0 && v[5] > v[4] && v[7] > v[6]) {
            float sx = (float) w / (v[5] - v[4]), sy = (float) h / (v[7] - v[6]);
            return new int[] {Math.round(v[4] * sx), Math.round(v[5] * sx), Math.round(v[6] * sy), Math.round(v[7] * sy)};
        }
        return new int[] {-(w / 2), w - w / 2, -(h / 2), h - h / 2};
    }
    @Override public int getRasterizer3D_clipNegativeMidX() { return clip3d()[0]; }
    @Override public int getRasterizer3D_clipMidX2() { return clip3d()[1]; }
    @Override public int getRasterizer3D_clipNegativeMidY() { return clip3d()[2]; }
    @Override public int getRasterizer3D_clipMidY2() { return clip3d()[3]; }
    /**
     * The scene's skybox colour (Native.skyboxColour, the renderer settings' RGBA that feeds the
     * shaders' skyboxColour). 0 means none set (the game's own sky); setting 0 clears it.
     */
    private int skybox;
    @Override public int getSkyboxColor() {
        if (isClientThread()) { int v = Native.skyboxColour(); skybox = (v >>> 24) == 0 ? 0 : v & 0xffffff; }
        return skybox;
    }
    @Override public void setSkyboxColor(int rgb) {
        if (isClientThread() && Native.setSkyboxColour(rgb == 0 ? 0 : 0xff000000 | rgb)) skybox = rgb;
    }
    // The camera focal point (Client camera block +0x78..+0x80, the smoothed player position the camera
    // orbits): X local x, Y local y, Z height, as getCameraX/Y/Z. The game moves it towards the player each frame.
    @Override public float getCameraFocalPointX() { return cam()[5]; }
    @Override public float getCameraFocalPointY() { return cam()[6]; }
    @Override public float getCameraFocalPointZ() { return cam()[7]; }
    @Override public void setCameraFocalPointX(float v) { setFocal(0, v); }
    @Override public void setCameraFocalPointY(float v) { setFocal(1, v); }
    @Override public void setCameraFocalPointZ(float v) { setFocal(2, v); }
    private void setFocal(int axis, float v) {
        if (isClientThread() && Native.setCameraFocal(axis, Math.round(v))) cameraCache = null;
    }
    // The mobile renderer draws every 2D layer itself; the mask is kept for plugins that read it back.
    private int draw2DMask = -1;
    @Override public int getDraw2DMask() { return draw2DMask; }
    @Override public void setDraw2DMask(int mask) { draw2DMask = mask; }
    // No GPU plugin hook on Android: the game renders with its own GL pipeline and never calls these.
    private net.runelite.api.hooks.DrawCallbacks drawCallbacks;
    @Override public net.runelite.api.hooks.DrawCallbacks getDrawCallbacks() { return drawCallbacks; }
    @Override public void setDrawCallbacks(net.runelite.api.hooks.DrawCallbacks cb) { drawCallbacks = cb; }
    /** The mobile game picks entities under a tap itself (Native.pickAt); a GPU plugin's clickbox has nothing to feed. */
    @Override public void checkClickbox(net.runelite.api.Projection p, net.runelite.api.Model m, int orientation, int x, int y, int z, long hash) { }
    /** The minimap's zoom-input flag (mm +0x64b4: the game's own input zooms the minimap while it is set). */
    @Override public boolean isMinimapZoom() { return Native.minimapZoomInput() > 0; }
    // Oculus orb (Client +0x894bc, CAM_MODE's field; the game's cycle moves the camera focal point
    // freely while it is 1 or 2) and its focal point (+0x89500/+0x89508, local units). The mobile game
    // has one field for both camera mode and orb state. Writes on the client thread only.
    @Override public int getCameraMode() { return Native.clientInt(Native.ORB_STATE); }
    @Override public void setCameraMode(int mode) { Native.setClientInt(Native.ORB_STATE, mode); }
    @Override public int getOculusOrbState() { return Native.clientInt(Native.ORB_STATE); }
    @Override public void setOculusOrbState(int state) { Native.setClientInt(Native.ORB_STATE, state); }
    @Override public int getOculusOrbFocalPointX() { return Native.clientInt(Native.ORB_X); }
    @Override public int getOculusOrbFocalPointY() { return Native.clientInt(Native.ORB_Y); }
    /** Camera shakes (CAM_SHAKE) are dropped after each cycle while disabled, before the frame applies them. */
    @Override public boolean isCameraShakeDisabled() { return Native.cameraShakeDisabled(); }
    @Override public void setCameraShakeDisabled(boolean off) { Native.setCameraShakeDisabled(off); }
    /** Desktop inverts the mouse camera drag; here the touch camera drag (and its glide), arrow keys unchanged. */
    @Override public void setInvertPitch(boolean on) { Native.setCameraInvert(0, on); }
    @Override public void setInvertYaw(boolean on) { Native.setCameraInvert(1, on); }
    /** No button selects the camera drag here: the game starts it from a touch only (a mouse's right/middle buttons go to their own listeners). */
    @Override public void setCameraMouseButtonMask(int mask) { }
    /** Desktop's pitch relaxer is the game's own flag here (Client +0x8951c, which the server can also set): pitch up to 520 instead of 383. */
    @Override public void setCameraPitchRelaxerEnabled(boolean on) { Native.setClientInt(Native.PITCH_RELAXER, on ? 1 : 0); }
    /** The touch camera's drag sensitivity, scaled from the game's own (its settings keep working under it). */
    @Override public void setCameraSpeed(float speed) { Native.setCameraSpeed(speed); }
    // The mobile game moves the focal point at one speed (Client +0x894c4, 12) in both free camera and orb modes.
    @Override public void setOculusOrbNormalSpeed(int speed) { Native.setClientInt(Native.ORB_SPEED, speed); }
    @Override public void setFreeCameraSpeed(int speed) { Native.setClientInt(Native.ORB_SPEED, speed); }
    /** The game's music volume (0-255); a set also changes the playing songs' volume, as desktop. */
    @Override public int getMusicVolume() { return Native.musicVolume(); }
    @Override public void setMusicVolume(int v) {
        if (isClientThread()) Native.setMusicVolume(v);
        else Host.invoke(() -> Native.setMusicVolume(v));
    }
    /** The songs the game's music player is playing (client thread; elsewhere empty). */
    @Override public java.util.List<net.runelite.api.MidiRequest> getActiveMidiRequests() {
        int[] r = Native.midiRequests();
        java.util.List<net.runelite.api.MidiRequest> out = new java.util.ArrayList<>();
        for (int i = 0; r != null && i + 1 < r.length; i += 2) {
            final int id = r[i];
            final boolean jingle = r[i + 1] != 0;
            out.add(new net.runelite.api.MidiRequest() {
                @Override public int getArchiveId() { return id; }
                @Override public boolean isJingle() { return jingle; }
                @Override public String toString() { return (jingle ? "jingle " : "song ") + id; }
            });
        }
        return out;
    }
    @Override public String getLauncherDisplayName() { return null; }

    // ---- Session and settings state (client-api-client-state) ----

    /** As desktop: the ACCOUNT_TYPE varbit (1777) the server sets at login. */
    @Override public net.runelite.api.vars.AccountType getAccountType() {
        switch (getVarbitValue(net.runelite.api.Varbits.ACCOUNT_TYPE)) {
            case 1: return net.runelite.api.vars.AccountType.IRONMAN;
            case 2: return net.runelite.api.vars.AccountType.ULTIMATE_IRONMAN;
            case 3: return net.runelite.api.vars.AccountType.HARDCORE_IRONMAN;
            case 4: return net.runelite.api.vars.AccountType.GROUP_IRONMAN;
            case 5: return net.runelite.api.vars.AccountType.HARDCORE_GROUP_IRONMAN;
            default: return net.runelite.api.vars.AccountType.NORMAL;
        }
    }

    private String buildId;
    /** Desktop logs it as the injected client's build; here the hook and host build (host.jar's osrsrl-build.txt). */
    @Override public String getBuildID() {
        if (buildId == null) {
            try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(
                    AndroidClient.class.getClassLoader().getResourceAsStream("osrsrl-build.txt"), java.nio.charset.StandardCharsets.UTF_8))) {
                buildId = r.readLine();
            } catch (Exception e) {
                buildId = "unknown";
            }
        }
        return buildId;
    }

    /** RuneLite's Hooks: post/postDeferred reach the host's EventBus. */
    @Override public net.runelite.api.hooks.Callbacks getCallbacks() { return Host.hooks(); }

    /** The connected world's host and ports as of the last read on the client thread. */
    private volatile Object[] connection;
    private Object[] connection() {
        Object[] c = isClientThread() ? Native.connection() : null;
        if (c != null) connection = c;
        return connection;
    }
    /** The game's connect address (Client +0x3098): the test server's, as repack.sh points the app there. */
    @Override public String getWorldHost() {
        Object[] c = connection();
        return c != null ? (String) c[0] : null;
    }

    private int socketFd = -1;
    /**
     * The game's socket to its world: the open TCP socket whose peer is the connected world's host
     * (getWorldHost, compared as a literal address, so no lookup) on one of its game ports. Nothing
     * else is returned: an fd connected anywhere else is never handed out.
     */
    @Override public java.io.FileDescriptor getSocketFD() {
        Object[] c = connection();
        if (c == null) return null;
        String host = (String) c[0];
        int[] ports = (int[]) c[1];
        if (socketFd >= 0 && gameSocket(socketFd, host, ports)) return fileDescriptor(socketFd);
        for (int fd = 0; fd < 4096; fd++)
            if (gameSocket(fd, host, ports)) { socketFd = fd; return fileDescriptor(fd); }
        socketFd = -1;
        return null;
    }
    private static boolean gameSocket(int fd, String host, int[] ports) {
        java.io.FileDescriptor d = fileDescriptor(fd);
        if (d == null) return false;
        try {
            if (!android.system.OsConstants.S_ISSOCK(android.system.Os.fstat(d).st_mode)) return false;
            java.net.SocketAddress a = android.system.Os.getpeername(d);
            if (!(a instanceof java.net.InetSocketAddress)) return false;
            java.net.InetSocketAddress i = (java.net.InetSocketAddress) a;
            return i.getAddress() != null && i.getAddress().getHostAddress().equals(host) && (i.getPort() == ports[0] || i.getPort() == ports[1]);
        } catch (android.system.ErrnoException e) {
            return false;
        }
    }
    /** A FileDescriptor naming fd `n` without owning it (closing or collecting it never closes the game's socket). */
    private static java.io.FileDescriptor fileDescriptor(int n) {
        try {
            java.io.FileDescriptor d = new java.io.FileDescriptor();
            java.io.FileDescriptor.class.getMethod("setInt$", int.class).invoke(d, n);
            return d;
        } catch (ReflectiveOperationException e) {
            return null;
        }
    }

    /** The game's world list (slr.ws, list order), null before the game has loaded one, as desktop. */
    @Override public net.runelite.api.World[] getWorldList() {
        Object[] all = Native.worldList();
        if (all == null || all.length == 0) return null;
        net.runelite.api.World[] out = new net.runelite.api.World[all.length];
        for (int i = 0; i < all.length; i++) {
            Object[] e = (Object[]) all[i];
            int[] info = (int[]) e[2];
            AndroidWorld w = new AndroidWorld();
            w.setId(info[0]);
            w.setTypes(net.runelite.api.WorldType.fromMask(info[1]));
            w.setPlayerCount(info[2]);
            w.setLocation(info[3]);
            w.setIndex(info[5]);
            w.setAddress((String) e[0]);
            w.setActivity((String) e[1]);
            out[i] = w;
        }
        return out;
    }

    /** Writes the game's tick counter (Client +0x3058) that getTickCount reads; on the client thread only. */
    @Override public void setTickCount(int tick) {
        if (isClientThread()) Native.setServerTick(tick);
        else Host.invoke(() -> Native.setServerTick(tick));
    }

    // RuneLite's own client flags (desktop keeps them in injected fields the game never reads):
    // stored here the same way. The mobile game has no mouseover text, interpolates frames itself
    // and has no world map loading percentage to report.
    // 64-bit varps: the game's long tables beside the int ones (VARP_LONG); every varp read as a long.
    @Override public long[] getVarpsLong() { long[] v = Native.varpsLong(false); return v != null ? v : new long[0]; }
    @Override public long[] getServerVarpsLong() { long[] v = Native.varpsLong(true); return v != null ? v : new long[0]; }
    @Override public long getVarpLongValue(int id) { long[] v = getVarpsLong(); return id >= 0 && id < v.length ? v[id] : 0; }
    @Override public long getServerVarpLongValue(int id) { long[] v = getServerVarpsLong(); return id >= 0 && id < v.length ? v[id] : 0; }
    /** As desktop, the client's value only (no VarbitChanged until queueChangedVarp); client thread only. */
    @Override public void setVarpLongValue(int id, long value) {
        if (isClientThread()) Native.setVarpLong(id, value);
        else Host.invoke(() -> Native.setVarpLong(id, value));
    }
    /** The game's ring of the last 100 private/friends/clan chat message ids (duplicates are dropped). */
    @Override public long[] getCrossWorldMessageIds() {
        long[] r = Native.crossWorldIds();
        return r != null ? java.util.Arrays.copyOf(r, 100) : new long[100];
    }
    @Override public int getCrossWorldMessageIdsIndex() { long[] r = Native.crossWorldIds(); return r != null ? (int) r[100] : 0; }
    /** The login screen's page (desktop's loginIndex numbering for 0-14) and input field (0 username, 1 password). */
    @Override public int getLoginIndex() { return Native.loginIndex(); }
    @Override public int getCurrentLoginField() { return Native.loginField(); }
    @Override public void drawOriginalMenu(int alpha) { AndroidRender.drawMenu(this, alpha, false); }
    @Override public void draw2010Menu(int alpha) { AndroidRender.drawMenu(this, alpha, true); }
    private java.util.function.IntPredicate animationInterpolationFilter;
    @Override public java.util.function.IntPredicate getAnimationInterpolationFilter() { return animationInterpolationFilter; }
    @Override public void setAnimationInterpolationFilter(java.util.function.IntPredicate f) { animationInterpolationFilter = f; }
    private boolean mouseoverTextEnabled = true;
    @Override public boolean isMouseoverTextEnabled() { return mouseoverTextEnabled; }
    @Override public void setMouseoverTextEnabled(boolean on) { mouseoverTextEnabled = on; }
    /** A varbit's value within a given varp array (as getVarbitValue reads the live one). */
    @Override public int getVarbitValue(int[] varps, int varbit) {
        int[] d = Native.varbitDef(varbit);
        if (d == null || varps == null || d[0] < 0 || d[0] >= varps.length) return 0;
        int bits = d[2] - d[1] + 1;
        int mask = bits >= 32 ? -1 : (1 << bits) - 1;
        return (varps[d[0]] >>> d[1]) & mask;
    }
    /**
     * The server's IF_SETEVENTS settings covering a widget (Menu Entry Swapper's UI swaps read its click
     * mask), or null when none covers it, as desktop's widget flags table. // unverified: on the device.
     */
    @Override public net.runelite.api.widgets.WidgetConfigNode getWidgetConfig(net.runelite.api.widgets.Widget w) {
        if (w == null) return null;
        int index = w.getIndex();
        int[] e = Native.widgetEvents(w.getId(), index);
        if (e == null) return null;
        long hash = ((long) w.getId() << 32) | (index & 0xffffffffL);
        return new net.runelite.api.widgets.WidgetConfigNode() {
            @Override public int getClickMask() { return e[0]; }
            @Override public int getOpMask() { return e[1]; }
            @Override public long getHash() { return hash; }
            @Override public net.runelite.api.Node getNext() { return null; }
            @Override public net.runelite.api.Node getPrevious() { return null; }
        };
    }
    @Override public int getCanvasWidth() { return OverlayRenderer.canvasSize().width; }
    @Override public int getCanvasHeight() { return OverlayRenderer.canvasSize().height; }

    // --- skills (Skill.ordinal() is the game's stat index) ---

    private static final int SKILLS = 25;

    private static int[] slice(int part) {
        return java.util.Arrays.copyOfRange(Native.skills(), part * SKILLS, (part + 1) * SKILLS);
    }

    @Override public int getBoostedSkillLevel(net.runelite.api.Skill s) { return Native.skills()[s.ordinal()]; }
    @Override public int getRealSkillLevel(net.runelite.api.Skill s) { return Native.skills()[SKILLS + s.ordinal()]; }
    @Override public int getSkillExperience(net.runelite.api.Skill s) { return Native.skills()[2 * SKILLS + s.ordinal()]; }
    @Override public int[] getBoostedSkillLevels() { return slice(0); }
    @Override public int[] getRealSkillLevels() { return slice(1); }
    @Override public int[] getSkillExperiences() { return slice(2); }

    @Override
    public int getTotalLevel() {
        int total = 0;
        for (int l : slice(1)) total += l;
        return total;
    }

    @Override
    public long getOverallExperience() {
        long total = 0;
        for (int x : slice(2)) total += x;
        return total;
    }

    // --- regions (no instances yet) ---

    @Override public boolean isInInstancedRegion() { return AndroidWorldView.instanced(); }

    @Override
    public java.util.EnumSet<net.runelite.api.WorldType> getWorldType() {
        return net.runelite.api.WorldType.fromMask(Native.worldFlags());
    }

    // Client +0x3174 / +0x2f64 (the CS2 map_world / worldflags handlers).
    @Override public int getWorld() { return Native.world(); }
    @Override public net.runelite.api.World createWorld() { return new AndroidWorld(); }
    /** GE item search results that OC_FINDNEXT returns (game Client +0x417be8; unverified on device). */
    @Override public void setGeSearchResultIds(short[] ids) { if (isClientThread()) Native.setGeSearch(ids, Integer.MIN_VALUE, Integer.MIN_VALUE); }
    @Override public void setGeSearchResultCount(int count) { if (isClientThread()) Native.setGeSearch(null, count, Integer.MIN_VALUE); }
    @Override public void setGeSearchResultIndex(int index) { if (isClientThread()) Native.setGeSearch(null, Integer.MIN_VALUE, index); }
    /**
     * As desktop: sets the world the next login connects to (id, flags, host, ports) through the game's
     * setWorld, the login screen's world pick. Client thread only, as the game's world list is.
     */
    @Override public void changeWorld(net.runelite.api.World world) {
        if (world == null || !isClientThread()) return;
        Native.changeWorld(world.getId(), net.runelite.api.WorldType.toMask(world.getTypes()), world.getAddress());
    }
    /** As desktop: op 1 "World Switcher" on the logout tab's button (182:3); the server opens the world switcher (69). */
    @Override public void openWorldHopper() {
        if (isClientThread()) Native.widgetOp(182 << 16 | 3, -1, 1);
    }
    /** As desktop: op 1 "Switch" on the world switcher's list (69:18, sub = world id); the server answers LOGOUT_TRANSFER. */
    @Override public void hopToWorld(net.runelite.api.World world) {
        if (world != null && isClientThread()) Native.widgetOp(69 << 16 | 18, world.getId(), 1);
    }
    /** Marks a skill changed (as a stat update does) so the skill tab's onStatTransmit scripts redraw it (Virtual Levels). */
    @Override public void queueChangedSkill(net.runelite.api.Skill skill) {
        if (skill != null && isClientThread()) Native.queueChangedSkill(skill.ordinal());
    }

    /**
     * As on desktop, the `tupleIndex`-th field of every tuple in the column (a column of type
     * (int, string) with three tuples gives three ints for 0, three strings for 1), through the
     * game's db_getfieldcount and db_getfield. Null off the client thread.
     */
    @Override
    public Object[] getDBTableField(int rowID, int column, int tupleIndex) {
        if (!isClientThread()) return null;
        // The lookups run the game's db_getfield handler on the script VM's stacks, which can't be
        // used while a script runs (script events, runelite_callback emulation such as
        // questFilter). Database rows are static cache data, so results are kept; one asked for
        // mid-script that isn't known yet reads as empty and is fetched on the next cycle.
        long key = ((long) rowID << 32) | ((long) (column & 0xffff) << 16) | (tupleIndex & 0xffff);
        Object[] cached = dbCache.get(key);
        if (cached != null) return cached.clone();
        if (liveStacks) {
            dbWanted.add(key);
            return new Object[0];
        }
        Object[] r = readDBTableField(rowID, column, tupleIndex);
        // Empty results are cached too: World Map asks for absent fields of every quest row each client tick.
        if (dbCache.size() < 65536) dbCache.put(key, r.clone());
        return r;
    }

    private final java.util.Map<Long, Object[]> dbCache = new java.util.HashMap<>();
    private final java.util.Set<Long> dbWanted = new java.util.LinkedHashSet<>();

    /** Game thread, each cycle outside scripts: fetches the db fields asked for mid-script. */
    void fetchWantedDbFields() {
        if (dbWanted.isEmpty() || liveStacks) return;
        java.util.List<Long> keys = new java.util.ArrayList<>(dbWanted);
        dbWanted.clear();
        for (long k : keys) {
            Object[] r = readDBTableField((int) (k >>> 32), (int) (k >>> 16) & 0xffff, (int) k & 0xffff);
            if (r.length > 0 && dbCache.size() < 65536) dbCache.put(k, r);
        }
    }

    /** Remembers a field so it is known before a script asks for it (see getDBTableField). */
    void wantDbField(int rowID, int column, int tupleIndex) {
        long key = ((long) rowID << 32) | ((long) (column & 0xffff) << 16) | (tupleIndex & 0xffff);
        if (!dbCache.containsKey(key)) dbWanted.add(key);
    }

    private Object[] readDBTableField(int rowID, int column, int tupleIndex) {
        int n = Native.dbFieldCount(rowID, column);
        // A row without values in the column: db_getfield on tuple 0 pushes the column entry's
        // value, which is the table's default or else the type's generic one (-1 / ""). The table
        // default is kept; the generic one reads as no value (empty), as on desktop: World Map skips
        // quest rows whose column 21 has a value, and quest rows read -1 there (gen 32).
        // unverified: a table default equal to -1 / "" would read as empty too
        boolean empty = n == 0;
        if (empty) n = 1;
        java.util.List<Object> out = new java.util.ArrayList<>();
        for (int t = 0; t < n && t < 4096; t++) {
            Object[] r = Native.dbField(rowID, column, t);
            if (r != null && tupleIndex >= 0 && tupleIndex < r.length) out.add(r[tupleIndex]);
        }
        if (empty && out.size() == 1) {
            Object v = out.get(0);
            if ((v instanceof Number && ((Number) v).longValue() == -1) || "".equals(v)) return new Object[0];
        }
        return out.toArray();
    }

    // Friends chat / clans: not in a channel (RuneLite returns null then).
    @Override public net.runelite.api.FriendsChatManager getFriendsChatManager() { return ChatChannels.friendsChat(); }
    private final AndroidNameables.Friends friends = new AndroidNameables.Friends();
    private final AndroidNameables.Ignores ignores = new AndroidNameables.Ignores();
    @Override public net.runelite.api.FriendContainer getFriendContainer() { return friends; }
    /** Open sub-interfaces keyed by the component they're mounted in (RuneLite's componentTable). */
    @Override public net.runelite.api.HashTable<net.runelite.api.WidgetNode> getComponentTable() {
        int[] t = Native.subInterfaces();
        final java.util.Map<Long, net.runelite.api.WidgetNode> m = new java.util.LinkedHashMap<>();
        for (int i = 0; i + 2 < t.length; i += 3) {
            final int comp = t[i], group = t[i + 1], mode = t[i + 2];
            m.put((long) comp, new net.runelite.api.WidgetNode() {
                @Override public int getId() { return group; }
                @Override public int getModalMode() { return mode; }
                @Override public net.runelite.api.Node getNext() { return null; }
                @Override public net.runelite.api.Node getPrevious() { return null; }
                @Override public long getHash() { return comp; }
            });
        }
        return new net.runelite.api.HashTable<net.runelite.api.WidgetNode>() {
            @Override public net.runelite.api.WidgetNode get(long key) { return m.get(key); }
            @Override public java.util.Iterator<net.runelite.api.WidgetNode> iterator() { return m.values().iterator(); }
        };
    }
    /**
     * As desktop: closes the sub-interface (the node from getComponentTable: hash = component,
     * id = group) through the game's own IF_CLOSESUB path. Client thread only, as on desktop.
     * Verified 2026-10-17 gen 29 (probe `wclose 12 0` closed the bank; client-only, as desktop).
     */
    @Override public void closeInterface(net.runelite.api.WidgetNode node, boolean unload) {
        if (node == null) return;
        Native.closeSubInterface((int) node.getHash(), node.getId(), unload);
    }
    /**
     * As desktop: opens `interfaceId` inside `componentId` through IF_OPENSUB's own helper (the
     * component-table node, the group's load and onLoad, the layout) and returns its component-table
     * node; null for ids the game doesn't have. Client-only, as desktop; client thread.
     */
    @Override public net.runelite.api.WidgetNode openInterface(int componentId, int interfaceId, int modalMode) {
        if (!isClientThread() || !Native.openSubInterface(componentId, interfaceId, modalMode)) return null;
        return getComponentTable().get(componentId);
    }
    /**
     * As desktop: the scene's minimap image of a plane (736x736 = desktop's 184-tile extended scene
     * at 4 px per tile; the mobile 104-tile scene is the middle 416 px, as Instance Map crops it with
     * getExpandedMapLoading() 0), drawn by the game's own minimap renderer (`Native.drawMinimap`).
     * Client thread.
     */
    @Override public net.runelite.api.SpritePixels drawInstanceMap(int plane) {
        int[] px = Native.drawMinimap(plane);
        return px == null ? null : new HostSpritePixels(px, 736, 736);
    }
    /** Only LOADING is honoured (plugins use it to rebuild the scene), via the game's own reload. */
    @Override public void setGameState(GameState state) {
        if (state != GameState.LOADING) return;
        if (isClientThread()) Native.reloadScene();
        else Host.invoke(Native::reloadScene);
    }
    @Override public net.runelite.api.NameableContainer<net.runelite.api.Ignore> getIgnoreContainer() { return ignores; }
    private AndroidNameables.Entry[] friendsCycle = new AndroidNameables.Entry[0];
    private int friendsCycleStamp = -1;
    /**
     * Whether a player is on the friends list (Player.isFriend: desktop sets the flag per player
     * from the same list). The list is read once per cycle; draw listeners ask for every player.
     */
    boolean isFriendCached(String name) {
        if (name == null) return false;
        if (friendsCycleStamp != Host.cycleStamp) {
            friendsCycle = AndroidNameables.read(false);
            friendsCycleStamp = Host.cycleStamp;
        }
        return AndroidNameables.find(friendsCycle, name) != null;
    }
    @Override public boolean isFriended(String name, boolean mustBeLoggedIn) {
        net.runelite.api.Friend f = friends.findByName(name);
        return f != null && (!mustBeLoggedIn || f.getWorld() > 0);
    }
    // Player menu options (MenuManager.addPlayerMenuItem: Hiscore's "Lookup"). Kept host-side;
    // the game's player menus are built from its own option list.
    private final String[] playerOptions = new String[8];
    private final boolean[] playerOptionsPriorities = new boolean[8];
    private final int[] playerMenuTypes = new int[8];
    @Override public String[] getPlayerOptions() { return playerOptions; }
    @Override public boolean[] getPlayerOptionsPriorities() { return playerOptionsPriorities; }
    @Override public int[] getPlayerMenuTypes() { return playerMenuTypes; }
    @Override public net.runelite.api.clan.ClanChannel getClanChannel() { return ChatChannels.clan(net.runelite.api.clan.ClanID.CLAN); }
    @Override public net.runelite.api.clan.ClanChannel getGuestClanChannel() { return ChatChannels.clan(-1); }
    @Override public net.runelite.api.clan.ClanSettings getClanSettings() { return ChatChannels.settings(net.runelite.api.clan.ClanID.CLAN); }
    @Override public net.runelite.api.clan.ClanSettings getGuestClanSettings() { return ChatChannels.settings(-1); }
    @Override public net.runelite.api.clan.ClanChannel getClanChannel(int clanId) { return clanId >= 0 ? ChatChannels.clan(clanId) : null; }
    @Override public net.runelite.api.clan.ClanSettings getClanSettings(int clanId) { return clanId >= 0 ? ChatChannels.settings(clanId) : null; }

    // Sprite overrides are written into the game's cached sprite of the same id and size
    // (HostSprites.Overrides); widget sprite overrides are kept but not applied.
    private final java.util.Map<Integer, net.runelite.api.SpritePixels> spriteOverrides = new HostSprites.Overrides();
    private final java.util.Map<Integer, net.runelite.api.SpritePixels> widgetSpriteOverrides = new java.util.HashMap<>();
    @Override public java.util.Map<Integer, net.runelite.api.SpritePixels> getSpriteOverrides() { return spriteOverrides; }
    @Override public java.util.Map<Integer, net.runelite.api.SpritePixels> getWidgetSpriteOverrides() { return widgetSpriteOverrides; }
    /** Overrides are written into the game's sprite cache directly, so there is nothing to reset. */
    @Override public net.runelite.api.NodeCache getWidgetSpriteCache() { return () -> {}; }
    /** Struct compositions are read from the game per call (nothing cached host-side), so reset is a no-op. PostStructComposition is not posted. */
    @Override public net.runelite.api.NodeCache getStructCompositionCache() { return () -> {}; }
    @Override public net.runelite.api.dbtable.DBRowConfig getDBRowConfig(int rowID) { return CacheConfig.dbRow(rowID); }
    /** A copy of the game's varc store (int and string varcs), read on the client thread. */
    @Override public java.util.Map<Integer, Object> getVarcMap() { return CacheConfig.varcs(); }
    /** IF_SETEVENTS flags keyed component << 32 | index (CacheConfig.widgetFlags). */
    @Override public net.runelite.api.HashTable<net.runelite.api.widgets.WidgetConfigNode> getWidgetFlags() { return CacheConfig.widgetFlags(); }
    /**
     * The game's own item icon renderer (createItemSprite, docs/offsets.md): 36x32, border 0/1/2,
     * shadow colour, quantity text per `stackable` (0 never, 1 always, 2 if the item stacks).
     * Game thread only; null elsewhere or while the item's model is still loading. `scale` other
     * than 512 isn't applied.
     */
    @Override
    public net.runelite.api.SpritePixels createItemSprite(int itemId, int quantity, int border, int shadowColor,
                                                          int stackable, boolean noted, int scale) {
        if (!Native.isClientThread()) return null;
        int textMode = 0;
        if (stackable == 1) textMode = 1;
        else if (stackable == 2 && quantity != 1) {
            net.runelite.api.ItemComposition c = getItemDefinition(itemId);
            textMode = c != null && c.isStackable() ? 1 : 0;
        }
        int[] r = Native.itemSprite(itemId, quantity, border, shadowColor, textMode, noted);
        if (r == null) return null;
        int w = r[0], h = r[1];
        return new HostSpritePixels(java.util.Arrays.copyOfRange(r, 2, 2 + w * h), w, h);
    }

    @Override public net.runelite.api.SpritePixels createSpritePixels(int[] pixels, int width, int height) {
        return new HostSpritePixels(pixels, width, height);
    }
    // Chat (mod) icons: the font's <img=N> table. The array starts with placeholders for the game's
    // icons (sized from the game); plugins (ChatIconManager: Emojis, chat rank icons) append or replace
    // entries, and syncModIcons hands the result to the native side, which installs it in the game.
    private net.runelite.api.IndexedSprite[] modIcons, syncedIcons;
    private Object[] syncedPixels;
    @Override public synchronized net.runelite.api.IndexedSprite[] getModIcons() {
        if (modIcons == null) {
            int[] sizes = Native.modIconSizes();
            if (sizes == null) return null;  // not loaded yet (desktop: null too)
            modIcons = new net.runelite.api.IndexedSprite[sizes.length / 2];
            for (int i = 0; i < modIcons.length; i++) modIcons[i] = HostIndexedSprite.game(i, sizes[2 * i], sizes[2 * i + 1]);
        }
        return modIcons;
    }
    @Override public synchronized void setModIcons(net.runelite.api.IndexedSprite[] icons) {
        modIcons = icons;
        syncModIcons();
    }

    /** Game thread, every cycle and on setModIcons: pushes the table when an entry changed. */
    synchronized void syncModIcons() {
        net.runelite.api.IndexedSprite[] icons = modIcons;
        if (icons == null) return;
        int n = icons.length;
        boolean same = syncedIcons != null && syncedIcons.length == n;
        for (int i = 0; same && i < n; i++)
            same = syncedIcons[i] == icons[i] && (icons[i] == null || syncedPixels[i] == icons[i].getPixels());
        if (same) return;
        syncedIcons = icons.clone();
        syncedPixels = new Object[n];
        boolean custom = false;
        int[] meta = new int[n * 7];
        byte[][] px = new byte[n][];
        int[][] pal = new int[n][];
        for (int i = 0; i < n; i++) {
            net.runelite.api.IndexedSprite s = icons[i];
            syncedPixels[i] = s != null ? s.getPixels() : null;
            int at = i * 7;
            if (s instanceof HostIndexedSprite && ((HostIndexedSprite) s).gameIndex >= 0) {
                meta[at] = ((HostIndexedSprite) s).gameIndex;
                continue;
            }
            meta[at] = -1;
            if (s == null) continue;
            custom = true;
            meta[at + 1] = s.getWidth(); meta[at + 2] = s.getHeight();
            meta[at + 3] = s.getOffsetX(); meta[at + 4] = s.getOffsetY();
            meta[at + 5] = s.getOriginalWidth(); meta[at + 6] = s.getOriginalHeight();
            px[i] = s.getPixels();
            pal[i] = s.getPalette();
        }
        // Nothing to install while the table is only the game's own icons.
        if (custom || syncedOnce) { Native.setModIcons(meta, px, pal); syncedOnce = true; }
    }
    private boolean syncedOnce;

    // Preferences (Metronome reads the sound effect volume): the game's own audio settings aren't
    // read; values a plugin sets are kept.
    private final net.runelite.api.Preferences preferences = new net.runelite.api.Preferences() {
        private String remembered = "";
        private int sfx = 127, areaSfx = 127;
        @Override public String getRememberedUsername() { return remembered; }
        @Override public void setRememberedUsername(String u) { remembered = u != null ? u : ""; }
        @Override public int getSoundEffectVolume() { return sfx; }
        @Override public void setSoundEffectVolume(int v) { sfx = v; }
        @Override public int getAreaSoundEffectVolume() { return areaSfx; }
        @Override public void setAreaSoundEffectVolume(int v) { areaSfx = v; }
        @Override public boolean getHideUsername() { return false; }
    };
    @Override public net.runelite.api.Preferences getPreferences() { return preferences; }
    /**
     * The login name (KPH Tracker): the login screen's username field (desktop's Login.username); a token
     * login leaves it empty, and then the local player's name, as the test accounts log in by name.
     */
    @Override public String getUsername() {
        String u = Native.loginText(0);
        if (u != null && !u.isEmpty()) return u;
        net.runelite.api.Player p = Host.localPlayer();
        return p != null && p.getName() != null ? p.getName() : "";
    }
    /** The login screen's fields (desktop's Login.username/password/otp); desktop posts UsernameChanged on a new name. */
    @Override public void setUsername(String name) {
        if (!isClientThread() || !Native.setLoginText(0, name != null ? name : "")) return;
        net.runelite.client.eventbus.EventBus bus = Host.eventBus();
        if (bus != null) bus.post(new net.runelite.api.events.UsernameChanged());
    }
    @Override public void setPassword(String password) { if (isClientThread()) Native.setLoginText(1, password != null ? password : ""); }
    @Override public void setOtp(String otp) { if (isClientThread()) Native.setLoginText(2, otp != null ? otp : ""); }
    /** The title background, stretched over the game's two title sprites (null: the game's own). */
    @Override public void setLoginScreen(net.runelite.api.SpritePixels s) {
        if (s == null) Native.setLoginBackground(null, 0, 0);
        else Native.setLoginBackground(s.getPixels(), s.getWidth(), s.getHeight());
    }
    @Override public void setShouldRenderLoginScreenFire(boolean on) { Native.setLoginFire(on); }

    @Override public net.runelite.api.IndexedSprite createIndexedSprite() { return new HostIndexedSprite(); }

    /**
     * OAuthApi.getAccountHash (XP Tracker, GE, profiles): -1 when logged out, as RuneLite; otherwise a
     * stable per-name value (the game's real account hash isn't read).
     */
    @Override public long getAccountHash() {
        // The raw state, not getGameState(): that reads LOADING until GameStateChanged(LOGGED_IN) is
        // posted, and the login's RuneScapeProfileChanged/UsernameChanged are posted just before it
        // (Mahogany Homes read group "MahoganyHomes.-1" there). Desktop's hash is valid while LOADING.
        net.runelite.api.GameState s = net.runelite.api.GameState.of(Native.gameState());
        if (s != net.runelite.api.GameState.LOGGED_IN && s != net.runelite.api.GameState.LOADING) return -1;
        net.runelite.api.Player p = getLocalPlayer();
        String n = p != null ? p.getName() : null;
        return n == null ? -1 : (n.toLowerCase().hashCode() & 0xffffffffL);
    }
    // No instances yet: an empty template table.
    @Override public int[][][] getInstanceTemplateChunks() { return AndroidWorldView.templateChunks(); }

    // --- inventories ---

    @Override
    public net.runelite.api.ItemContainer getItemContainer(int id) {
        return AndroidItemContainer.of(id);
    }

    @Override
    public net.runelite.api.ItemContainer getItemContainer(net.runelite.api.InventoryID id) {
        return AndroidItemContainer.of(id.getId());
    }

    /** Every inventory the client holds, keyed by inventory id (snapshots, as getItemContainer). */
    @Override
    public net.runelite.api.HashTable<net.runelite.api.ItemContainer> getItemContainers() {
        final java.util.LinkedHashMap<Long, net.runelite.api.ItemContainer> m = new java.util.LinkedHashMap<>();
        int[] ids = Native.inventoryIds();
        if (ids != null) for (int id : ids) {
            net.runelite.api.ItemContainer c = AndroidItemContainer.of(id);
            if (c != null) m.put((long) id, c);
        }
        return new net.runelite.api.HashTable<net.runelite.api.ItemContainer>() {
            @Override public net.runelite.api.ItemContainer get(long key) { return m.get(key); }
            @Override public java.util.Iterator<net.runelite.api.ItemContainer> iterator() { return m.values().iterator(); }
        };
    }

    // --- widgets ---

    @Override
    public net.runelite.api.widgets.Widget getWidget(int componentId) {
        componentId = mapDesktopComponent(componentId);
        return Native.widgetExists(componentId) ? AndroidWidget.of(componentId) : null;
    }

    /** Android's root interface (601) has no desktop group ids; route the ones the API needs. */
    private static final int ANDROID_ROOT = 601, ANDROID_MINIMAP_DRAW_AREA = ANDROID_ROOT << 16 | 34,
        ANDROID_SIDE_PANEL = ANDROID_ROOT << 16 | 111, RESIZABLE_INVENTORY_CONTAINER = 161 << 16 | 73;

    static int mapDesktopComponent(int id) {
        switch (id) {
            case net.runelite.api.widgets.ComponentID.RESIZABLE_VIEWPORT_MINIMAP_DRAW_AREA:
            case net.runelite.api.widgets.ComponentID.RESIZABLE_VIEWPORT_BOTTOM_LINE_MINIMAP_DRAW_AREA:
            case net.runelite.api.widgets.ComponentID.FIXED_VIEWPORT_MINIMAP_DRAW_AREA:
                return Native.rootInterface() == ANDROID_ROOT ? ANDROID_MINIMAP_DRAW_AREA : id;
            case RESIZABLE_INVENTORY_CONTAINER:
                // The side panel (inventory, skills, ...): Status Bars draws its bars beside it.
                return Native.rootInterface() == ANDROID_ROOT ? ANDROID_SIDE_PANEL : id;
            default:
                return id;
        }
    }

    /** Deprecated, but RuneLite's own Perspective.localToMinimap still calls it. */
    @Override
    @SuppressWarnings("deprecation")
    public net.runelite.api.widgets.Widget getWidget(net.runelite.api.widgets.WidgetInfo info) {
        return getWidget(info.getId());
    }

    @Override
    public net.runelite.api.widgets.Widget getWidget(int group, int child) {
        return getWidget(group << 16 | child);
    }

    @Override
    public int getTopLevelInterfaceId() {
        return Native.rootInterface();
    }

    /** The toplevel interface's root components (layout parent -1), as desktop's getWidgetRoots. */
    @Override
    public net.runelite.api.widgets.Widget[] getWidgetRoots() {
        int group = Native.rootInterface();
        if (group < 0) return new net.runelite.api.widgets.Widget[0];
        java.util.List<net.runelite.api.widgets.Widget> out = new java.util.ArrayList<>();
        for (int c = 0, n = Native.widgetChildCount(group); c < n; c++) {
            int[] g = Native.widgetGeometry(group << 16 | c);
            if (g != null && g.length > 4 && g[4] == -1) out.add(getWidget(group << 16 | c));
        }
        return out.toArray(new net.runelite.api.widgets.Widget[0]);
    }

    /** The mobile client's revision (build 242.3). */
    @Override public int getRevision() { return 242; }

    /**
     * Desktop's IF1 inventory drag delay (5 cycles by default). The mobile drag block
     * (`0x41d0a4`) only compares each widget's own dead time `+0x324` (all mobile inventories are
     * IF3 children), so the delay is written into the inventory items' (149:0's children) dead time,
     * at once and again each cycle, since the inventory's scripts recreate them with their own.
     * -1 until a plugin sets it: the game's value stays.
     */
    private volatile int inventoryDragDelay = -1;
    @Override public void setInventoryDragDelay(int delay) {
        inventoryDragDelay = delay;
        if (isClientThread()) applyInventoryDragDelay();
    }
    /** Client tick (Host.dispatch) and setInventoryDragDelay: the delay into each inventory item's dead time. */
    void applyInventoryDragDelay() {
        int d = inventoryDragDelay;
        if (d < 0) return;
        net.runelite.api.widgets.Widget inv = getWidget(149, 0);
        net.runelite.api.widgets.Widget[] items = inv == null ? null : inv.getDynamicChildren();
        if (items == null) return;
        for (net.runelite.api.widgets.Widget w : items) if (w != null && w.getDragDeadTime() != d) w.setDragDeadTime(d);
    }

    @Override
    public boolean isClientThread() {
        return Native.isClientThread();
    }

    // ---- Plugin Hub needs (2026-10-16) ----

    private volatile Thread clientThread;
    private final java.util.Map<Integer, java.util.List<Integer>> dbTableRows = new java.util.HashMap<>();
    private int itemCount = -1;

    /** The game thread (Thread objects compare equal by identity, as plugins use it). */
    @Override public Thread getClientThread() {
        if (isClientThread() && !Edt.isEdt()) clientThread = Thread.currentThread();  // the EDT only stands in for it
        return clientThread;
    }

    /**
     * Rows of a DB table, found by scanning row ids through the game's DBRowType cache (rows are
     * numbered densely across tables); scanned once, then cached. Client thread only (empty
     * otherwise, and not cached).
     */
    @Override public java.util.List<Integer> getDBTableRows(int table) {
        synchronized (dbTableRows) {
            java.util.List<Integer> cached = dbTableRows.get(table);
            if (cached != null) return cached;
        }
        if (!isClientThread()) return java.util.Collections.emptyList();
        java.util.Map<Integer, java.util.List<Integer>> all = new java.util.HashMap<>();
        int misses = 0;
        for (int row = 0; row <= 0xffff && misses < 4096; row++) {
            int t = Native.dbRowTable(row);
            if (t < 0) { misses++; continue; }
            misses = 0;
            all.computeIfAbsent(t, k -> new java.util.ArrayList<>()).add(row);
        }
        synchronized (dbTableRows) {
            for (java.util.Map.Entry<Integer, java.util.List<Integer>> e : all.entrySet())
                dbTableRows.put(e.getKey(), java.util.Collections.unmodifiableList(e.getValue()));
            java.util.List<Integer> r = dbTableRows.get(table);
            if (r == null) dbTableRows.put(table, r = java.util.Collections.emptyList());
            return r;
        }
    }

    /**
     * Rows of `table` whose `column` holds `value` in field `tupleIndex` of any tuple (desktop
     * uses the cache's DB index; this scans the table's rows). Client thread only.
     */
    /** getDBRowsByValue results (static cache data; World Map asks for every quest icon each client tick). */
    private final java.util.Map<String, java.util.List<Integer>> dbRowsByValue = new java.util.HashMap<>();
    @Override public java.util.List<Integer> getDBRowsByValue(int table, int column, int tupleIndex, Object value) {
        if (!isClientThread() || value == null) return java.util.Collections.emptyList();
        String key = table + ":" + column + ":" + tupleIndex + ":" + value.getClass().getName() + ":" + value;
        java.util.List<Integer> hit = dbRowsByValue.get(key);
        if (hit != null) return new java.util.ArrayList<>(hit);
        java.util.List<Integer> out = lookupDBRowsByValue(table, column, tupleIndex, value);
        // Not while a script runs: getDBTableField then answers empty for fields it hasn't read yet.
        if (!liveStacks && dbRowsByValue.size() < 16384) dbRowsByValue.put(key, new java.util.ArrayList<>(out));
        return out;
    }
    private java.util.List<Integer> lookupDBRowsByValue(int table, int column, int tupleIndex, Object value) {
        java.util.List<Integer> out = new java.util.ArrayList<>();
        for (int row : getDBTableRows(table)) {
            Object[] vals = getDBTableField(row, column, tupleIndex);
            if (vals == null) continue;
            for (Object v : vals) {
                if (value.equals(v) || (value instanceof Number && v instanceof Number
                        && ((Number) value).longValue() == ((Number) v).longValue())) {
                    out.add(row);
                    break;
                }
            }
        }
        return out;
    }

    /** Number of item definitions: one past the highest id the game's obj cache resolves. */
    @Override public int getItemCount() {
        if (itemCount >= 0 || !isClientThread()) return Math.max(itemCount, 0);
        int lo = 0, hi = 0x10000;  // lookup(lo) exists, lookup(hi) assumed not
        if (getItemDefinition(hi - 1) != null) return itemCount = hi;
        while (hi - lo > 1) {
            int mid = (lo + hi) >>> 1;
            if (getItemDefinition(mid) != null) lo = mid; else hi = mid;
        }
        return itemCount = hi;
    }

    /** Followers (pets) aren't tracked yet. */
    @Override public net.runelite.api.NPC getFollower() { return null; }

    /** The top-level WorldView's map spotanims (GraphicsObjects, refreshed per client tick). */
    final GraphicsObjects graphicsObjects = new GraphicsObjects();
    @Override public net.runelite.api.Deque<net.runelite.api.GraphicsObject> getGraphicsObjects() { return graphicsObjects; }

    /** The game's js5 archives (CacheConfig.Index); getSprites reads frame 0 of the sprites archive's groups from the game's sprite cache. */
    @Override public net.runelite.api.IndexDataBase getIndex(int id) { return CacheConfig.index(id); }
    @Override public net.runelite.api.IndexDataBase getIndexConfig() { return CacheConfig.index(2); }
    @Override public net.runelite.api.IndexDataBase getIndexSprites() { return CacheConfig.index(8); }
    @Override public net.runelite.api.IndexDataBase getIndexScripts() { return CacheConfig.index(12); }
    @Override public net.runelite.api.NodeCache getItemCompositionCache() { return CacheConfig.CACHES[CacheConfig.OBJ]; }
    @Override public net.runelite.api.NodeCache getObjectCompositionCache() { return CacheConfig.CACHES[CacheConfig.LOC]; }
    @Override public net.runelite.api.NodeCache getAnimationCache() { return CacheConfig.CACHES[CacheConfig.SEQ]; }
    @Override public net.runelite.api.NodeCache getItemModelCache() { return CacheConfig.CACHES[CacheConfig.ITEM_MODEL]; }
    @Override public net.runelite.api.NodeCache getItemSpriteCache() { return CacheConfig.CACHES[CacheConfig.ITEM_SPRITE]; }
    /** The game's HealthBarType and health bar sprite caches, cleared by its own clear (0x66e060). */
    @Override public void resetHealthBarCaches() { CacheConfig.CACHES[CacheConfig.HEALTH_BAR].reset(); AndroidHealthBarConfig.reset(); }

    @Override public net.runelite.api.SpritePixels[] getSprites(net.runelite.api.IndexDataBase source, int archiveId, int fileId) {
        if (!(source instanceof CacheConfig.Index) || ((CacheConfig.Index) source).id != 8 || fileId != 0 || !isClientThread()) return null;
        int[] r = Native.sprite(archiveId);
        if (r == null) return null;
        int w = r[0], h = r[1];
        return new net.runelite.api.SpritePixels[] {new HostSpritePixels(java.util.Arrays.copyOfRange(r, 2, 2 + w * h), w, h)};
    }
}
