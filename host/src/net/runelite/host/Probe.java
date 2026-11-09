package net.runelite.host;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Collection;
import java.util.List;
import java.util.Map;

import net.runelite.client.plugins.Plugin;

/**
 * Debug hook: `setprop debug.osrsrl.probe "QuestHelperPlugin.getSelectedQuest().getCurrentStep().getActiveStep()"`
 * evaluates the path every few game ticks on the game thread and logs the result's class and
 * fields (tag osrsrl-probe). The first segment is a running plugin's simple class name; then
 * `name()` calls a no-arg method and `name` reads a field (private members included). Empty = off.
 */
final class Probe {
    private static final String TAG = "osrsrl-probe";
    private static List<Plugin> plugins;
    private static int tick;
    private static String lastSay = "";

    private static String lastSet = "";
    private static String lastWdump = "";

    private static void dumpGroup(net.runelite.api.Client c, String arg) {
        if (arg.equals("all")) {  // every open sub-interface group (find a mobile group by its text)
            int[] subs = Native.subInterfaces();
            java.util.Set<Integer> groups = new java.util.TreeSet<>();
            for (int i = 0; i + 2 < subs.length; i += 3) groups.add(subs[i + 1]);
            Log.i(TAG, "wdump all: groups " + groups);
            for (int g : groups) dumpGroup(c, Integer.toString(g));
            return;
        }
        try {
            int group = Integer.parseInt(arg);
            for (int child = 0; child < 1024; child++) {
                net.runelite.api.widgets.Widget w = c.getWidget(group, child);
                if (w == null) { if (child > 0) break; else continue; }
                Log.i(TAG, "w " + group + ":" + child + " type " + w.getType() + " text '" + w.getText() + "' name '" + w.getName()
                    + "' verb '" + w.getTargetVerb() + "' ops " + java.util.Arrays.toString(w.getActions()) + " sprite " + w.getSpriteId()
                    + " tiling " + w.getSpriteTiling() + " border " + w.getBorderType() + " flip " + w.isFlippedHorizontally() + "/" + w.isFlippedVertically()
                    + " filled " + w.isFilled() + " font " + w.getFontId() + " align " + w.getXTextAlignment() + "/" + w.getYTextAlignment()
                    + " lineh " + w.getLineHeight() + " shadow " + w.getTextShadowed() + " colour " + Integer.toHexString(w.getTextColor())
                    + " item " + w.getItemId() + " orig " + w.getOriginalX() + "," + w.getOriginalY() + " " + w.getOriginalWidth() + "x" + w.getOriginalHeight()
                    + " modes " + w.getXPositionMode() + w.getYPositionMode() + w.getWidthMode() + w.getHeightMode() + " geo " + w.getRelativeX() + "," + w.getRelativeY() + " " + w.getWidth() + "x" + w.getHeight()
                    + " dyn " + w.getDynamicChildren().length);
            }
        } catch (Throwable t) {
            Log.w(TAG, "wdump " + arg + " failed: " + t);
        }
    }

    private static void testSetter(net.runelite.api.Client c, String[] a) {
        if (a.length < 3) return;
        try {
            if (a[0].equals("load") && Host.loader != null) {  // "load <jar> <n>": load plugins/<jar> now (n makes each load a new value)
                Log.i(TAG, "load " + a[1]);
                Host.loader.accept(a[1]);
                return;
            }
            if (a[0].equals("afk")) {  // "afk <minutes> <n>": the game's idle logout timer (tools/api-probe.sh: no idle logout during its set-up)
                c.setIdleTimeout(Integer.parseInt(a[1]) * 3000);
                return;
            }
            if (a[0].equals("db")) {  // "db <row> <column> <tuple> [rows]": getDBTableField over rows row..row+rows-1
                a = String.join(" ", a).split(" +");
                int row = Integer.parseInt(a[1]), col = Integer.parseInt(a[2]), tup = Integer.parseInt(a[3]);
                int rows = a.length > 4 ? Integer.parseInt(a[4]) : 1;
                for (int r = row; r < row + rows; r++)
                    Log.i(TAG, "db row " + r + " col " + col + " tuple " + tup + " -> " + java.util.Arrays.toString(c.getDBTableField(r, col, tup)));
                return;
            }
            if (a[0].equals("inst")) {  // "inst log 1": instance state, regions, used template chunks, the player's template point
                Log.i(TAG, "inst " + c.isInInstancedRegion() + " regions " + java.util.Arrays.toString(c.getMapRegions()));
                int[][][] t = c.getInstanceTemplateChunks();
                int shown = 0;
                for (int l = 0; l < 4; l++)
                    for (int x = 0; x < 13; x++)
                        for (int z = 0; z < 13; z++) {
                            int v = t[l][x][z];
                            if (v == -1 || shown++ >= 40) continue;
                            Log.i(TAG, "inst chunk " + l + "/" + x + "/" + z + " -> zone " + (v >> 14 & 1023) + "," + (v >> 3 & 2047)
                                + " level " + (v >> 24 & 3) + " rot " + (v >> 1 & 3));
                        }
                net.runelite.api.Player me = c.getLocalPlayer();
                if (me != null) Log.i(TAG, "inst me " + me.getWorldLocation() + " template "
                    + net.runelite.api.coords.WorldPoint.fromLocalInstance(c, me.getLocalLocation()) + " used " + shown);
                return;
            }
            if (a[0].equals("net")) {  // "net get|post|xp": one request through the injected OkHttpClient (NetGuard test; post/xp go to the local test server's HTTP sidecar, never to RuneLite)
                final String kind = a[1];
                okhttp3.OkHttpClient http = net.runelite.client.RuneLite.getInjector().getInstance(okhttp3.OkHttpClient.class);
                okhttp3.Request.Builder rb = new okhttp3.Request.Builder();
                if (a[1].equals("get")) rb.url("https://static.runelite.net/npcs/npcs.min.json").head();
                else if (a[1].equals("post")) rb.url("http://10.0.2.2:8080/netguard-test/runelite-1.13.1/loottracker")
                        .post(okhttp3.RequestBody.create(okhttp3.MediaType.parse("application/json"), "[]"));
                else rb.url("http://10.0.2.2:8080/netguard-test/runelite-1.13.1/xp/update?username=netguard-test");
                http.newCall(rb.build()).enqueue(new okhttp3.Callback() {
                    @Override public void onFailure(okhttp3.Call call, java.io.IOException e) { Log.i(TAG, "net " + kind + " failed: " + e.getMessage()); }
                    @Override public void onResponse(okhttp3.Call call, okhttp3.Response r) { Log.i(TAG, "net " + kind + " -> HTTP " + r.code()); r.close(); }
                });
                return;
            }
            if (a[0].equals("twidth")) {  // "twidth <font id> <text>": GameFont.getTextWidth (the game's PARAWIDTH measure)
                int font = Integer.parseInt(a[1]);
                Log.i(TAG, "twidth font " + font + " '" + a[2] + "' -> " + GameFont.of(font).getTextWidth(a[2]));
                return;
            }
            if (a[0].equals("config") && Host.configManager != null) {  // "config <group> <key>=<value>": setConfiguration (fires ConfigChanged)
                int eq = a[2].indexOf('=');
                if (eq > 0) {
                    Host.configManager.setConfiguration(a[1], a[2].substring(0, eq), a[2].substring(eq + 1));
                    Log.i(TAG, "config " + a[1] + "." + a[2]);
                } else {  // "config <group> <key>": unsetConfiguration (test cleanup)
                    Host.configManager.unsetConfiguration(a[1], a[2]);
                    Log.i(TAG, "config unset " + a[1] + "." + a[2]);
                }
                return;
            }
            if (a[0].equals("ohead")) {  // "ohead <npc id> <text>": setOverheadText + setOverheadCycle(100) on the first such NPC
                int id = Integer.parseInt(a[1]);
                for (net.runelite.api.NPC n : c.getTopLevelWorldView().npcs()) {
                    if (n.getId() != id) continue;
                    n.setOverheadText(a[2]);
                    n.setOverheadCycle(100);
                    Log.i(TAG, "ohead " + n.getName() + " #" + n.getIndex() + " -> " + a[2]);
                    break;
                }
                return;
            }
            if (a[0].equals("tiles")) {  // "tiles <range> 0|1": corner heights and canvas points around the player (1 = every tile, 0 = odd ones)
                tilesDump(c, Integer.parseInt(a[1]), "1".equals(a[2]));
                return;
            }
            if (a[0].equals("gfin")) {  // "gfin <spotanim id> 0": setFinished(true) on each graphics object with that id
                for (net.runelite.api.GraphicsObject g : c.getTopLevelWorldView().getGraphicsObjects()) {
                    if (g.getId() != Integer.parseInt(a[1])) continue;
                    Log.i(TAG, "gfin " + g + " frame " + g.getAnimationFrame() + " anim " + (g.getAnimation() != null ? g.getAnimation().getId() : -1));
                    g.setFinished(true);
                    Log.i(TAG, "gfin -> finished " + g.finished());
                }
                return;
            }
            if (a[0].equals("reload")) {  // "reload 0 0": setGameState(LOADING)
                c.setGameState(net.runelite.api.GameState.LOADING);
                Log.i(TAG, "reload requested, state now " + c.getGameState());
                return;
            }
            if (a[0].equals("friends")) {  // "friends 0 0": friend/ignore containers and the component table
                Log.i(TAG, "friends state " + Native.friendListState() + " " + java.util.Arrays.toString(c.getFriendContainer().getMembers())
                        + " ignores " + java.util.Arrays.toString(c.getIgnoreContainer().getMembers()));
                for (net.runelite.api.WidgetNode n : c.getComponentTable())
                    Log.i(TAG, "component " + (n.getHash() >>> 16) + ":" + (n.getHash() & 0xffff) + " -> group " + n.getId() + " mode " + n.getModalMode());
                return;
            }
            if (a[0].equals("ge")) {  // "ge raw <slot>": the GE slot object's address and raw bytes, plus GeOffers' decode
                int slot = Integer.parseInt(a[2]);
                byte[] b = Native.geSlotRaw(slot);
                if (b == null) { Log.i(TAG, "ge slot " + slot + ": no object"); return; }
                long p = 0;
                for (int i = 7; i >= 0; i--) p = p << 8 | (b[i] & 0xff);
                StringBuilder sb = new StringBuilder();
                for (int i = 8; i < b.length; i++) { if (i > 8 && (i - 8) % 4 == 0) sb.append(' '); sb.append(String.format("%02x", b[i] & 0xff)); }
                net.runelite.api.GrandExchangeOffer o = c.getGrandExchangeOffers()[slot];
                Log.i(TAG, "ge slot " + slot + " @" + Long.toHexString(p) + ": " + sb + " -> " + o.getState() + " item " + o.getItemId()
                        + " price " + o.getPrice() + " qty " + o.getQuantitySold() + "/" + o.getTotalQuantity() + " spent " + o.getSpent());
                return;
            }
            if (a[0].equals("sprite")) {  // "sprite <id> 0": HostSprites.load result (size or null)
                java.awt.image.BufferedImage im = HostSprites.load(Integer.parseInt(a[1]));
                Log.i(TAG, "sprite " + a[1] + " " + (im == null ? "null" : im.getWidth() + "x" + im.getHeight()));
                return;
            }
            if (a[0].equals("players")) {  // "players 0 0": names and friends chat / clan membership
                for (net.runelite.api.Player p : c.getTopLevelWorldView().players())
                    Log.i(TAG, "player '" + p.getName() + "' fc " + p.isFriendsChatMember() + " clan " + p.isClanMember()
                        + " friend " + p.isFriend() + (p == c.getLocalPlayer() ? " (local)" : ""));
                return;
            }
            if (a[0].equals("pfield")) {  // "pfield <plugin class simple name> <field>": a plugin field's value (reflection)
                for (Plugin pl : plugins) {
                    if (!pl.getClass().getSimpleName().equals(a[1])) continue;
                    java.lang.reflect.Field f = pl.getClass().getDeclaredField(a[2]);
                    f.setAccessible(true);
                    Log.i(TAG, "pfield " + a[1] + "." + a[2] + " = " + f.get(pl));
                }
                return;
            }
            if (a[0].equals("chat")) {  // "chat log 0": friends chat and clan channels as the API returns them
                net.runelite.api.FriendsChatManager fc = c.getFriendsChatManager();
                Log.i(TAG, "friends chat " + (fc == null ? "null" : "'" + fc.getName() + "' owner '" + fc.getOwner() + "' my " + fc.getMyRank()
                        + " kick " + fc.getKickRank() + " " + java.util.Arrays.toString(fc.getMembers())));
                for (int id = -1; id <= 1; id++) {
                    net.runelite.api.clan.ClanChannel ch = id < 0 ? c.getGuestClanChannel() : c.getClanChannel(id);
                    Log.i(TAG, "clan channel " + id + " " + (ch == null ? "null" : "'" + ch.getName() + "' " + ch.getMembers()));
                }
                return;
            }
            if (a[0].equals("script")) {  // "script <id> <int args, comma-separated>": runScript, then the int/object stacks
                java.util.List<Object> args = new java.util.ArrayList<>();
                args.add(Integer.parseInt(a[1]));
                for (String s : a[2].split(",")) if (!s.isEmpty()) args.add(Integer.parseInt(s));
                c.runScript(args.toArray());
                int n = c.getIntStackSize(), m = c.getObjectStackSize();
                Log.i(TAG, "script " + a[1] + " " + a[2] + " -> ints " + java.util.Arrays.toString(java.util.Arrays.copyOf(c.getIntStack(), Math.min(n, 16)))
                        + " (" + n + ") objs " + java.util.Arrays.toString(java.util.Arrays.copyOf(c.getObjectStack(), Math.min(m, 8))) + " (" + m + ")");
                return;
            }
            if (a[0].equals("quest")) {  // "quest <Quest enum name> <varp>": QUEST_STATUS_GET result and a varp
                net.runelite.api.Quest q = net.runelite.api.Quest.valueOf(a[1]);
                c.runScript(net.runelite.api.ScriptID.QUEST_STATUS_GET, q.getId());
                int raw = c.getIntStack()[0];
                Log.i(TAG, "quest " + q + " id " + q.getId() + " script 4029 -> " + raw + " getState " + q.getState(c)
                        + " varp " + a[2] + " = " + c.getVarpValue(Integer.parseInt(a[2])));
                return;
            }
            if (a[0].equals("npcmodel")) {  // "npcmodel <npc id> 0": model bounds and hull of each NPC with that id
                int want = Integer.parseInt(a[1]);
                for (net.runelite.api.NPC n : c.getTopLevelWorldView().npcs()) {
                    net.runelite.api.NPCComposition base = n.getComposition();
                    if (n.getId() != want && (base == null || base.getId() != want)) continue;
                    int[] m = Native.actorModel(false, n.getIndex());
                    String b = "no model";
                    if (m != null) {
                        int[] lo = {Integer.MAX_VALUE, Integer.MAX_VALUE, Integer.MAX_VALUE}, hi = {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
                        for (int i = 1; i + 2 < m.length; i += 3)
                            for (int k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], m[i + k]); hi[k] = Math.max(hi[k], m[i + k]); }
                        b = "height " + m[0] + " verts " + (m.length - 1) / 3 + " x " + lo[0] + ".." + hi[0] + " y " + lo[1] + ".." + hi[1] + " z " + lo[2] + ".." + hi[2];
                    }
                    java.awt.Shape h = n.getConvexHull();
                    net.runelite.api.Model gm = n.getModel();
                    if (gm == null) Log.i(TAG, "npcmodel " + want + " index " + n.getIndex() + " getModel null");
                    else {
                        int hidden = 0, flat = 0;
                        for (int v : gm.getFaceColors3()) { if (v == -2) hidden++; else if (v == -1) flat++; }
                        Log.i(TAG, "npcmodel " + want + " index " + n.getIndex() + " getModel verts " + gm.getVerticesCount() + " faces " + gm.getFaceCount()
                                + " hidden " + hidden + " flat " + flat + " height " + gm.getModelHeight()
                                + " colours1[0..3] " + java.util.Arrays.toString(java.util.Arrays.copyOf(gm.getFaceColors1(), Math.min(4, gm.getFaceCount()))));
                    }
                    net.runelite.api.NPCComposition comp = n.getComposition();
                    Log.i(TAG, "npcmodel " + want + " getId " + n.getId() + " base " + (base != null ? base.getId() + " configs " + java.util.Arrays.toString(base.getConfigs()) : "?") + " index " + n.getIndex() + " name " + n.getName() + " world " + n.getWorldLocation()
                            + " local " + n.getLocalLocation() + " orient " + n.getCurrentOrientation() + " size " + (comp != null ? comp.getSize() : -1)
                            + " scale " + (comp != null ? comp.getWidthScale() + "/" + comp.getHeightScale() : "?") + "; " + b
                            + "; hull " + (h != null ? h.getBounds() : null) + " feet " + net.runelite.api.Perspective.localToCanvas(c, n.getLocalLocation(), c.getTopLevelWorldView().getPlane()));
                }
                return;
            }
            int colon = a[1].indexOf(':');
            int id = colon < 0 ? Integer.parseInt(a[1])
                : Integer.parseInt(a[1].substring(0, colon)) << 16 | Integer.parseInt(a[1].substring(colon + 1));
            switch (a[0]) {
                case "whide": {  // "whide <group:child> 0|1"
                    net.runelite.api.widgets.Widget w = c.getWidget(id);
                    if (w == null) { Log.w(TAG, "whide: no widget " + a[1]); break; }
                    boolean before = w.isSelfHidden();
                    w.setHidden(!"0".equals(a[2]));
                    Log.i(TAG, "set hidden " + a[1] + " " + before + " -> " + w.isSelfHidden());
                    break;
                }
                case "wqty": {  // "wqty <group:child> <slot> <mode>": child's setItemQuantityMode (API 0 never, 1 always, 2 stackable)
                    String[] q = a[2].trim().split(" +");
                    net.runelite.api.widgets.Widget p = c.getWidget(id);
                    net.runelite.api.widgets.Widget w = p == null ? null : p.getChild(Integer.parseInt(q[0]));
                    if (w == null) { Log.w(TAG, "wqty: no widget " + a[1] + "[" + q[0] + "]"); break; }
                    w.setItemQuantityMode(Integer.parseInt(q[1]));
                    Log.i(TAG, "set quantity mode " + a[1] + "[" + q[0] + "] item " + w.getItemId() + " x" + w.getItemQuantity()
                        + " -> " + q[1]);
                    break;
                }
                case "wtext": {  // "wtext <group:child> text"
                    net.runelite.api.widgets.Widget w = c.getWidget(id);
                    if (w == null) { Log.w(TAG, "wtext: no widget " + a[1]); break; }
                    String before = w.getText();
                    w.setText(a[2]);
                    Log.i(TAG, "set text " + a[1] + " '" + before + "' -> '" + w.getText() + "' type " + w.getType());
                    break;
                }
                case "spotanim": {  // "spotanim <id> <delay>": the local player's slot-0 spot anim (-1 removes it, -2 only lists)
                    net.runelite.api.Player me = c.getLocalPlayer();
                    if (id != -2) me.createSpotAnim(0, id, 0, Integer.parseInt(a[2].trim()));
                    StringBuilder sb = new StringBuilder();
                    for (net.runelite.api.ActorSpotAnim s : me.getSpotAnims()) sb.append(' ').append(s);
                    Log.i(TAG, "spotanim " + id + ": getGraphic " + me.getGraphic() + " spot anims" + sb + " cycle " + c.getGameCycle());
                    break;
                }
                case "anim": {  // "anim <id> <n>": the local player's animation (-1 clears it)
                    net.runelite.api.Player me = c.getLocalPlayer();
                    int before = me.getAnimation();
                    me.setAnimation(id);
                    Log.i(TAG, "anim " + before + " -> " + me.getAnimation() + " frame " + me.getAnimationFrame());
                    break;
                }
                case "terrain": {  // "terrain <scene x> <scene y>": that tile's terrain on the current plane, and the scene's
                    net.runelite.api.Scene s = c.getTopLevelWorldView().getScene();
                    int z = c.getTopLevelWorldView().getPlane(), y = Integer.parseInt(a[2].trim());
                    short[][][] u = s.getUnderlayIds(), o = s.getOverlayIds();
                    byte[][][] sh = s.getTileShapes();
                    net.runelite.api.Tile t = s.getTiles()[z][id][y];
                    net.runelite.api.SceneTilePaint p = t.getSceneTilePaint();
                    net.runelite.api.SceneTileModel m = t.getSceneTileModel();
                    int paints = 0, models = 0;
                    for (net.runelite.api.Tile[] col : s.getTiles()[z]) for (net.runelite.api.Tile k : col) {
                        if (k.getSceneTilePaint() != null) paints++;
                        if (k.getSceneTileModel() != null) models++;
                    }
                    Log.i(TAG, "terrain " + t.getWorldLocation() + " underlay " + (u == null ? "?" : u[z][id][y]) + " overlay " + (o == null ? "?" : o[z][id][y])
                            + " shape " + (sh == null ? "?" : sh[z][id][y]) + " paint " + (p == null ? null : "sw/se/ne/nw " + p.getSwColor() + "/" + p.getSeColor() + "/" + p.getNeColor() + "/" + p.getNwColor()
                            + " texture " + p.getTexture() + " rgb " + Integer.toHexString(p.getRBG()) + " flat " + p.isFlat())
                            + " model " + (m == null ? null : "shape " + m.getShape() + " rotation " + m.getRotation() + " underlay/overlay rgb " + Integer.toHexString(m.getModelUnderlay()) + "/" + Integer.toHexString(m.getModelOverlay())
                            + " vertices x " + java.util.Arrays.toString(m.getVertexX()) + " y " + java.util.Arrays.toString(m.getVertexY()) + " z " + java.util.Arrays.toString(m.getVertexZ())
                            + " faces " + java.util.Arrays.toString(m.getFaceX()) + java.util.Arrays.toString(m.getFaceY()) + java.util.Arrays.toString(m.getFaceZ())
                            + " textures " + java.util.Arrays.toString(m.getTriangleTextureId()))
                            + "; plane " + z + ": " + paints + " paints, " + models + " models; draw distance " + s.getDrawDistance() + " min level " + s.getMinLevel()
                            + " tint " + s.getOverrideHue() + "/" + s.getOverrideSaturation() + "/" + s.getOverrideLuminance() + "/" + s.getOverrideAmount() + " skybox " + s.getSkybox());
                    break;
                }
                case "iparam": {  // "iparam <item id> <param>": item param as int and string
                    int key = Integer.parseInt(a[2]);
                    if (key < 0) {  // -1: list every param 0..4095 the item has
                        StringBuilder sb = new StringBuilder();
                        for (int k = 0; k < 4096; k++) {
                            Object v = Native.typeParam(Params.OBJ, id, k);
                            if (v != null) sb.append(' ').append(k).append('=').append(v);
                        }
                        Log.i(TAG, "item " + id + " params:" + sb);
                        break;
                    }
                    net.runelite.api.ItemComposition ic = c.getItemDefinition(id);
                    Log.i(TAG, "item " + id + " param " + key + " int " + ic.getIntValue(key) + " string '" + ic.getStringValue(key) + "'");
                    break;
                }
                case "idef": {  // "idef <item id> 0": the ItemComposition, model fields included
                    net.runelite.api.ItemComposition ic = c.getItemDefinition(id);
                    Log.i(TAG, "item " + id + " " + ic + " model " + ic.getInventoryModel()
                            + " recolor " + java.util.Arrays.toString(ic.getColorToReplace()) + "->" + java.util.Arrays.toString(ic.getColorToReplaceWith())
                            + " retexture " + java.util.Arrays.toString(ic.getTextureToReplace()) + "->" + java.util.Arrays.toString(ic.getTextureToReplaceWith())
                            + " ambient " + ic.getAmbient() + " contrast " + ic.getContrast());
                    break;
                }
                case "npcdef": {  // "npcdef <npc id> 0": the NPCComposition, models and recolours included
                    net.runelite.api.NPCComposition nc = c.getNpcDefinition(id);
                    Log.i(TAG, "npc " + id + " " + nc + (nc != null ? " stats " + java.util.Arrays.toString(nc.getStats()) : ""));
                    break;
                }
                case "wlisten": {  // "wlisten <group:child> <index|-1>": the game's onDrag/onDragComplete/onOp/onTimer listeners, drag dead time/zone
                    int idx = Integer.parseInt(a[2]);
                    int[] slots = {0x4d0, 0x500, 0x698, 0x668};
                    String[] names = {"onDrag", "onDragComplete", "onOp", "onTimer"};
                    StringBuilder sb = new StringBuilder();
                    for (int k = 0; k < slots.length; k++)
                        sb.append(' ').append(names[k]).append('=').append(java.util.Arrays.toString(Native.widgetListener(id, idx, slots[k])));
                    Log.i(TAG, "wlisten " + a[1] + "[" + idx + "] flags 0x" + Integer.toHexString(Native.widgetFlags(id, idx)) + sb
                            + " java onDragComplete " + (WidgetListeners.onDragComplete(id, idx) != null)
                            + " IF_SETEVENTS " + java.util.Arrays.toString(Native.widgetEvents(id, idx))
                            + " deadTime " + Native.widgetGetField(id, idx, 0x324) + " deadZone " + Native.widgetGetField(id, idx, 0x320));
                    break;
                }
                case "wmove": {  // "wmove <group:child> <dx>": setOriginalX(+dx) + revalidate()
                    net.runelite.api.widgets.Widget w = c.getWidget(id);
                    if (w == null) { Log.w(TAG, "wmove: no widget " + a[1]); break; }
                    int x0 = w.getRelativeX(), ox = w.getOriginalX();
                    w.setOriginalX(ox + Integer.parseInt(a[2]));
                    w.revalidate();
                    Log.i(TAG, "moved " + a[1] + " original x " + ox + " -> " + w.getOriginalX() + ", laid-out x " + x0 + " -> " + w.getRelativeX());
                    break;
                }
                case "wclose": {  // "wclose <group> 0": closeInterface(node, true) on the component-table node holding that group
                    net.runelite.api.WidgetNode n = null;
                    for (net.runelite.api.WidgetNode w : c.getComponentTable()) if (w.getId() == id) n = w;
                    if (n == null) { Log.w(TAG, "wclose: group " + id + " is not open"); break; }
                    c.closeInterface(n, true);
                    Log.i(TAG, "closeInterface group " + id + " in " + (n.getHash() >>> 16) + ":" + (n.getHash() & 0xffff));
                    break;
                }
                case "wmap": {  // "wmap 0 0": the world map API as plugins see it, and surfaceContainsPosition on/off the surface
                    net.runelite.api.worldmap.WorldMap wm = c.getWorldMap();
                    Log.i(TAG, "wmap raw " + java.util.Arrays.toString(Native.worldMap()) + " loaded " + wm.getWorldMapRenderer().isLoaded()
                        + " pos " + wm.getWorldMapPosition() + " zoom " + wm.getWorldMapZoom()
                        + " contains(Lumbridge 3222,3218) " + Native.worldMapContains(0, 0, 3222, 3218)
                        + " contains(tower basement 3104,9571) " + Native.worldMapContains(0, 0, 3104, 9571)
                        + " surface " + wm.getWorldMapData().surfaceContainsPosition(3222, 3218) + "/" + wm.getWorldMapData().surfaceContainsPosition(3104, 9571));
                    // What WorldMapOverlay sees: its points and the 595 children it reads (map, bottom
                    // graphic with its onTimer, clip children, tooltip).
                    try {
                        Object mgr = net.runelite.client.RuneLite.getInjector()
                            .getInstance(net.runelite.client.ui.overlay.worldmap.WorldMapPointManager.class);
                        java.lang.reflect.Method gp = mgr.getClass().getDeclaredMethod("getWorldMapPoints");  // package-private
                        gp.setAccessible(true);
                        @SuppressWarnings("unchecked")
                        java.util.List<net.runelite.client.ui.overlay.worldmap.WorldMapPoint> pts =
                            (java.util.List<net.runelite.client.ui.overlay.worldmap.WorldMapPoint>) gp.invoke(mgr);
                        StringBuilder sb = new StringBuilder("wmap points " + pts.size());
                        for (int i = 0; i < Math.min(3, pts.size()); i++) sb.append(' ').append(pts.get(i).getName()).append('@').append(pts.get(i).getWorldPoint());
                        for (int child : new int[] {7, 10, 23, 34, 41}) {
                            net.runelite.api.widgets.Widget w = c.getWidget(595, child);
                            sb.append(" | 595:").append(child).append(w == null ? " null" : " " + w.getBounds() + " hidden " + w.isHidden() + " type " + w.getType());
                        }
                        Log.i(TAG, sb.toString());
                        // Region icons, and those whose map element category is 1056 (quest start, as World Map tests).
                        net.runelite.api.worldmap.WorldMapRegion[][] regs = wm.getWorldMapRenderer().getMapRegions();
                        int n = 0, quests = 0;
                        StringBuilder q = new StringBuilder();
                        java.util.Map<Integer, Integer> cats = new java.util.TreeMap<>();
                        for (net.runelite.api.worldmap.WorldMapRegion[] row : regs) for (net.runelite.api.worldmap.WorldMapRegion r : row) {
                            if (r == null) continue;
                            for (net.runelite.api.worldmap.WorldMapIcon ic : r.getMapIcons()) {
                                n++;
                                int cat = Native.mapElement(ic.getType()) == null ? -2 : c.getMapElementConfig(ic.getType()).getCategory();
                                cats.merge(cat, 1, Integer::sum);
                                if (cat == 1056 && quests++ < 4) q.append(' ').append(ic);
                            }
                        }
                        Log.i(TAG, "wmap regions " + regs.length + "x" + (regs.length > 0 ? regs[0].length : 0) + " icons " + n
                            + " quest starts " + quests + q + " categories " + cats);
                    } catch (Throwable t) {
                        Log.e(TAG, "wmap points", t);
                    }
                    break;
                }
                case "wjump": {  // "wjump <x> <y>": setWorldMapPositionTarget on plane 0, then the position plugins read
                    net.runelite.api.worldmap.WorldMap wm = c.getWorldMap();
                    net.runelite.api.Point before = wm.getWorldMapPosition();
                    wm.setWorldMapPositionTarget(new net.runelite.api.coords.WorldPoint(id, Integer.parseInt(a[2]), 0));
                    Log.i(TAG, "wjump " + id + "," + a[2] + " from " + before + " now " + wm.getWorldMapPosition());
                    break;
                }
                case "infoboxes": {  // "infoboxes <n> 0": log every InfoBox (class, text, tooltip); n distinguishes repeat dumps
                    java.util.List<net.runelite.client.ui.overlay.infobox.InfoBox> boxes = net.runelite.client.RuneLite.getInjector()
                        .getInstance(net.runelite.client.ui.overlay.infobox.InfoBoxManager.class).getInfoBoxes();
                    Log.i(TAG, "infoboxes #" + id + ": " + boxes.size() + " at tick " + c.getTickCount());
                    for (net.runelite.client.ui.overlay.infobox.InfoBox b : boxes)
                        Log.i(TAG, "infobox " + b.getClass().getName() + " text '" + b.getText() + "' tooltip '" + b.getTooltip() + "'");
                    break;
                }
                case "examine": {  // "examine <obj> <count>": the server's examine_item clientscript 6003, run client-side
                    c.runScript(6003, id, Integer.parseInt(a[2]), "Probe examine text.", 1, 100, 1, 60, 40);
                    Log.i(TAG, "runScript 6003 obj " + id + " count " + a[2]);
                    break;
                }
                case "sound": {  // "sound <id> <volume>"
                    c.playSoundEffect(id, Integer.parseInt(a[2]));
                    Log.i(TAG, "playSoundEffect " + id);
                    break;
                }
                case "varbit": {
                    int before = c.getVarbitValue(id);
                    c.setVarbit(id, Integer.parseInt(a[2]));
                    net.runelite.api.VarbitComposition d = c.getVarbit(id);
                    Log.i(TAG, "set varbit " + id + " " + before + " -> " + c.getVarbitValue(id)
                        + (d != null ? " (varp " + d.getIndex() + " bits " + d.getLeastSignificantBit() + ".." + d.getMostSignificantBit() + ")" : ""));
                    break;
                }
                case "varp": {
                    int[] v = c.getVarps();
                    int before = v[id];
                    v[id] = Integer.parseInt(a[2]);
                    c.queueChangedVarp(id);
                    Log.i(TAG, "set varp " + id + " " + before + " -> " + c.getVarpValue(id));
                    break;
                }
                case "varc": {
                    int before = c.getVarcIntValue(id);
                    c.setVarcIntValue(id, Integer.parseInt(a[2]));
                    Log.i(TAG, "set varc " + id + " " + before + " -> " + c.getVarcIntValue(id));
                    break;
                }
                case "varcstr": {
                    String before = c.getVarcStrValue(id);
                    c.setVarcStrValue(id, a[2]);
                    Log.i(TAG, "set varcstr " + id + " '" + before + "' -> '" + c.getVarcStrValue(id) + "'");
                    break;
                }
                default:
                    Log.w(TAG, "set: unknown kind " + a[0]);
            }
        } catch (Throwable t) {
            Log.w(TAG, "set " + String.join(" ", a) + " failed: " + t);
        }
    }

    static void init(List<Plugin> running) {
        plugins = running;
    }

    private static byte[] lastTargetState;

    /** `setprop debug.osrsrl.tmodelog 1`: hex of the game's widget target-mode state (0xc0 bytes) when it changes. */
    private static void logTargetState() {
        byte[] b = Native.targetState(0xc0);
        if (b == null || java.util.Arrays.equals(b, lastTargetState)) return;
        StringBuilder sb = new StringBuilder("tmode");
        for (int i = 0; i < b.length; i++) {
            if (i % 16 == 0) sb.append(String.format(" | %02x:", i));
            boolean diff = lastTargetState != null && lastTargetState[i] != b[i];
            sb.append(String.format(diff ? " *%02x" : " %02x", b[i] & 0xff));
        }
        lastTargetState = b;
        Log.i(TAG, sb.toString());
    }

    static void onGameTick() {
        if ("1".equals(Native.property("debug.osrsrl.camlog"))) logCamera();
        if ("1".equals(Native.property("debug.osrsrl.selflog")) && (tick % 5) == 0) logSelf();
        if ("1".equals(Native.property("debug.osrsrl.tmodelog"))) logTargetState();
        String say = Native.property("debug.osrsrl.say");  // test hook: Client.addChatMessage (GAMEMESSAGE), once per value
        if (!say.isEmpty() && !say.equals(lastSay) && Host.client() != null) {
            lastSay = say;
            Host.client().addChatMessage(net.runelite.api.ChatMessageType.GAMEMESSAGE, "", say, null);
        }
        String set = Native.property("debug.osrsrl.set");  // test hook: "varbit|varp|varc|varcstr <id> <value>", "whide|wtext <group:child> <v>", "sound <id> <vol>"; once per value
        if (!set.isEmpty() && !set.equals(lastSet) && Host.client() != null) {
            lastSet = set;
            testSetter(Host.client(), set.split(" ", 3));
        }
        String wdump = Native.property("debug.osrsrl.wdump");  // test hook: dump a widget group's getters, once per value
        if (!wdump.isEmpty() && !wdump.equals(lastWdump) && Host.client() != null) {
            lastWdump = wdump;
            dumpGroup(Host.client(), wdump.trim());
        }
        String where = Native.property("debug.osrsrl.where");
        if (!where.isEmpty() && (tick % 5) == 0) logWhere(where);
        if (plugins == null || (tick++ % 5) != 0) return;
        String path = Native.property("debug.osrsrl.probe");
        if (path.isEmpty()) return;
        try {
            Object o = eval(path);
            Log.i(TAG, path + " = " + describe(o));
        } catch (Throwable t) {
            Log.w(TAG, path + " failed: " + t);
        }
    }

    /**
     * Debug hook: `setprop debug.osrsrl.where <loc id>` logs every scene object with that id: world
     * position, plane and the centre of its click box in UI-canvas and screen pixels (for adb taps).
     */
    /** Debug hook: `setprop debug.osrsrl.selflog 1` logs the local player's and first NPC's API state. */
    private static void logSelf() {
        net.runelite.api.Client c = Host.client();
        if (c == null) return;
        net.runelite.api.Player me = c.getLocalPlayer();
        if (me != null)
            Log.i(TAG, "self " + me.getName() + " weight " + c.getWeight() + " energy " + c.getEnergy() + " cb " + me.getCombatLevel() + " skull " + me.getSkullIcon() + " prayer " + me.getOverheadIcon()
                    + " team " + me.getTeam() + " pose " + me.getPoseAnimation() + " idle/walk/run " + me.getIdlePoseAnimation() + "/"
                    + me.getWalkAnimation() + "/" + me.getRunAnimation() + " " + me.getPlayerComposition()
                    + " regions " + java.util.Arrays.toString(c.getMapRegions()) + " dest " + c.getLocalDestinationLocation() + " raw " + java.util.Arrays.toString(Native.walkDestination())
                    + " health " + c.getLocalPlayer().getHealthRatio() + "/" + c.getLocalPlayer().getHealthScale() + " idle " + c.getMouseIdleTicks() + " protect-melee " + c.isPrayerActive(net.runelite.api.Prayer.PROTECT_FROM_MELEE)
                    + " messages " + com.google.common.collect.Iterables.size(c.getMessages()));
        for (net.runelite.api.NPC n : c.getTopLevelWorldView().npcs()) {
            net.runelite.api.NPCComposition comp = n.getComposition();
            Log.i(TAG, "npc " + n.getName() + " id " + n.getId() + " pose " + n.getPoseAnimation() + " idle/walk " + n.getIdlePoseAnimation()
                    + "/" + n.getWalkAnimation() + " configs " + (comp == null ? null : java.util.Arrays.toString(comp.getConfigs())));
            break;
        }
    }

    private static void logWhere(String arg) {
        // "deco" / "ground": the decorative or ground objects within 5 tiles of the player instead.
        final String kind = arg.trim();
        int id = -1;
        if (!kind.equals("deco") && !kind.equals("ground"))
            try { id = Integer.parseInt(kind); } catch (NumberFormatException e) { return; }
        net.runelite.api.Client c = Host.client();
        net.runelite.api.coords.WorldPoint me = c != null && c.getLocalPlayer() != null ? c.getLocalPlayer().getWorldLocation() : null;
        AndroidWorldView wv = Host.worldView();
        if (c == null || wv == null) return;
        net.runelite.api.Tile[][][] tiles = wv.getScene().getTiles();
        double toScreen = 1080.0 / Math.max(1, c.getCanvasHeight());
        int found = 0;
        for (int p = 0; p < tiles.length; p++)
            for (net.runelite.api.Tile[] col : tiles[p])
                for (net.runelite.api.Tile t : col) {
                    if (t == null) continue;
                    List<net.runelite.api.TileObject> objs = new java.util.ArrayList<>();
                    if (t.getWallObject() != null) objs.add(t.getWallObject());
                    if (t.getDecorativeObject() != null) objs.add(t.getDecorativeObject());
                    if (t.getGroundObject() != null) objs.add(t.getGroundObject());
                    if (t.getGameObjects() != null) for (net.runelite.api.GameObject g : t.getGameObjects()) if (g != null) objs.add(g);
                    for (net.runelite.api.TileObject o : objs) {
                        if (!o.getWorldLocation().equals(t.getWorldLocation())) continue;
                        if (id >= 0 ? o.getId() != id
                                : !(kind.equals("deco") ? o instanceof net.runelite.api.DecorativeObject : o instanceof net.runelite.api.GroundObject)
                                  || me == null || me.distanceTo(o.getWorldLocation()) > 5 || found >= 8) continue;
                        java.awt.Shape box = o.getClickbox();
                        String at = "offscreen";
                        if (box != null) {
                            java.awt.geom.Rectangle2D b = box.getBounds2D();
                            at = String.format("canvas %d,%d screen %d,%d", (int) b.getCenterX(), (int) b.getCenterY(),
                                    (int) (b.getCenterX() * toScreen), (int) (b.getCenterY() * toScreen));
                        }
                        String model = "no model";
                        if (o instanceof AndroidTileObject) {
                            AndroidTileObject a = (AndroidTileObject) o;
                            int[] m = Native.objectModel(a.plane, a.sceneX(), a.sceneY(), a.layer(), a.tag);
                            if (m != null) model = "model at " + m[0] + "," + m[1] + " h " + m[2] + " orient " + m[3]
                                    + " height " + m[4] + " verts " + (m.length - 5) / 3 + " (local " + o.getX() + "," + o.getY() + " z " + o.getZ() + ")";
                        }
                        Log.i(TAG, "where " + (id >= 0 ? "" + id : kind + " " + o.getId()) + " " + o.getClass().getSimpleName() + " at " + o.getWorldLocation() + ": " + at + "; " + model);
                        found++;
                    }
                }
        if (found == 0) Log.i(TAG, "where " + kind + ": not in scene");
    }

    private static Object eval(String path) throws Exception {
        String[] seg = path.split("\\.");
        Object cur = null;
        for (Plugin p : plugins) if (p.getClass().getSimpleName().equals(seg[0])) cur = p;
        if (cur == null) throw new IllegalArgumentException("no plugin " + seg[0]);
        for (int i = 1; i < seg.length && cur != null; i++) {
            String s = seg[i];
            if (s.endsWith("()")) {
                Method m = findMethod(cur.getClass(), s.substring(0, s.length() - 2));
                m.setAccessible(true);
                cur = m.invoke(cur);
            } else {
                Field f = findField(cur.getClass(), s);
                f.setAccessible(true);
                cur = f.get(cur);
            }
        }
        return cur;
    }

    private static Method findMethod(Class<?> c, String name) throws NoSuchMethodException {
        for (Class<?> k = c; k != null; k = k.getSuperclass())
            for (Method m : k.getDeclaredMethods())
                if (m.getName().equals(name) && m.getParameterTypes().length == 0) return m;
        throw new NoSuchMethodException(c.getName() + "." + name + "()");
    }

    private static Field findField(Class<?> c, String name) throws NoSuchFieldException {
        for (Class<?> k = c; k != null; k = k.getSuperclass())
            for (Field f : k.getDeclaredFields()) if (f.getName().equals(name)) return f;
        throw new NoSuchFieldException(c.getName() + "." + name);
    }

    private static String describe(Object o) {
        if (o == null) return "null";
        StringBuilder sb = new StringBuilder(o.getClass().getName()).append(" {");
        for (Class<?> k = o.getClass(); k != null && k != Object.class; k = k.getSuperclass()) {
            for (Field f : k.getDeclaredFields()) {
                if (Modifier.isStatic(f.getModifiers())) continue;
                try {
                    f.setAccessible(true);
                    sb.append("\n  ").append(f.getName()).append('=').append(brief(f.get(o)));
                } catch (Throwable t) {
                    sb.append("\n  ").append(f.getName()).append("=<").append(t).append('>');
                }
            }
        }
        return sb.append("\n}").toString();
    }

    private static String brief(Object v) {
        if (v == null) return "null";
        if (v instanceof Collection) {
            Collection<?> c = (Collection<?>) v;
            StringBuilder sb = new StringBuilder("size " + c.size() + " [");
            int n = 0;
            for (Object e : c) {
                if (n++ == 3) { sb.append(", ..."); break; }
                sb.append(n > 1 ? ", " : "").append(oneLine(e));
            }
            return sb.append(']').toString();
        }
        if (v instanceof Map) return "map size " + ((Map<?, ?>) v).size();
        return oneLine(v);
    }

    private static String oneLine(Object v) {
        String s;
        try {
            s = String.valueOf(v);
        } catch (Throwable t) {
            s = v.getClass().getName() + "<toString threw>";
        }
        return s.length() > 160 ? s.substring(0, 160) + "..." : s;
    }

    /**
     * Probe "tiles": for each tile within range of the player, the settings and bridge flag, the four
     * corner heights Perspective uses, the canvas tile polygon and Line of Sight's corner points
     * (localToCanvas at +-64). Logs every tile, or only odd ones (a null or far-off point, a corner
     * height far from the player's, a bridge flag).
     */
    private static void tilesDump(net.runelite.api.Client c, int range, boolean all) {
        net.runelite.api.WorldView wv = c.getTopLevelWorldView();
        net.runelite.api.Player me = c.getLocalPlayer();
        if (wv == null || me == null) return;
        net.runelite.api.coords.LocalPoint pl = me.getLocalLocation();
        int plane = wv.getPlane(), px = pl.getSceneX(), py = pl.getSceneY();
        int[][][] h = wv.getTileHeights();
        byte[][][] st = wv.getTileSettings();
        int ph = net.runelite.api.Perspective.getTileHeight(c, pl, plane);
        Log.i(TAG, "tiles player " + me.getWorldLocation() + " scene " + px + "," + py + " plane " + plane + " h " + ph
                + " cam " + c.getCameraFpX() + "," + c.getCameraFpY() + "," + c.getCameraFpZ() + " pitch " + c.getCameraPitch()
                + " yaw " + c.getCameraYaw() + " scale " + c.getScale() + " vp " + c.getViewportWidth() + "x" + c.getViewportHeight()
                + "+" + c.getViewportXOffset() + "," + c.getViewportYOffset() + " gpu " + c.isGpu() + " dims " + h.length + "x" + h[0].length + "x" + h[0][0].length);
        int odd = 0, n = 0;
        for (int dy = range; dy >= -range; dy--)
            for (int dx = -range; dx <= range; dx++) {
                int sx = px + dx, sy = py + dy;
                if (sx < 0 || sy < 0 || sx >= 104 || sy >= 104) continue;
                n++;
                int bridge = st[1][sx][sy] & 2, z = plane < 3 && bridge != 0 ? plane + 1 : plane;
                int[] corners = {h[z][sx][sy], h[z][sx + 1][sy], h[z][sx][sy + 1], h[z][sx + 1][sy + 1]};
                net.runelite.api.coords.LocalPoint lp = new net.runelite.api.coords.LocalPoint((sx << 7) + 64, (sy << 7) + 64, wv);
                java.awt.Polygon poly = net.runelite.api.Perspective.getCanvasTilePoly(c, lp);
                StringBuilder pts = new StringBuilder();
                boolean bad = poly == null || bridge != 0;
                for (int k = 0; k < 4; k++) {
                    int cx = lp.getX() + ((k & 1) == 0 ? -64 : 64), cy = lp.getY() + ((k & 2) == 0 ? -64 : 64);
                    net.runelite.api.Point p = net.runelite.api.Perspective.localToCanvas(c, new net.runelite.api.coords.LocalPoint(cx, cy, wv), plane);
                    pts.append(p == null ? " null" : " " + p.getX() + "," + p.getY());
                    if (p == null || Math.abs(p.getX()) > 5000 || Math.abs(p.getY()) > 5000) bad = true;
                    if (Math.abs(corners[k] - ph) > 1000) bad = true;
                }
                if (poly != null)
                    for (int k = 0; k < poly.npoints; k++)
                        if (Math.abs(poly.xpoints[k]) > 5000 || Math.abs(poly.ypoints[k]) > 5000) bad = true;
                if (bad) odd++;
                if (!all && !bad) continue;
                StringBuilder pp = new StringBuilder();
                if (poly == null) pp.append("null");
                else for (int k = 0; k < poly.npoints; k++) pp.append(k > 0 ? " " : "").append(poly.xpoints[k]).append(',').append(poly.ypoints[k]);
                Log.i(TAG, "tile " + (wv.getBaseX() + sx) + "," + (wv.getBaseY() + sy) + " d " + dx + "," + dy + " set " + st[plane][sx][sy]
                        + " s1 " + st[1][sx][sy] + " z " + z + " h sw/se/nw/ne " + java.util.Arrays.toString(corners)
                        + " los" + pts + " poly " + pp + (bad ? " ODD" : ""));
            }
        Log.i(TAG, "tiles done: " + n + " tiles, " + odd + " odd");
    }

    /** `setprop debug.osrsrl.camlog 1`: camera vs player frames, for projection offsets. */
    private static void logCamera() {
        try {
            int[] cam = Native.camera(), pos = Native.playerPosition();
            if (cam == null || pos == null) return;
            Log.i(TAG, "viewport " + java.util.Arrays.toString(Native.viewport()) + " scale " + Host.client().getScale()
                    + " viewport size " + Host.client().getViewportWidth() + "x" + Host.client().getViewportHeight()
                    + " game zoom " + java.util.Arrays.toString(Native.viewportZoom()));
            net.runelite.api.Client c = Host.client();
            AndroidWorldView wv = Host.worldView();
            net.runelite.api.coords.LocalPoint lp = new net.runelite.api.coords.LocalPoint(pos[4], pos[5], net.runelite.api.WorldView.TOPLEVEL);
            net.runelite.api.Point p = net.runelite.api.Perspective.localToCanvas(c, lp, wv.getPlane());
            Log.i(TAG, "cam " + java.util.Arrays.toString(cam) + " player world " + pos[0] + "," + pos[1]
                + " tile " + pos[2] + "," + pos[3] + " fine " + pos[4] + "," + pos[5]
                + " focal-fine " + (cam[5] - pos[4]) + "," + (cam[6] - pos[5])
                + " base " + wv.getBaseX() + "," + wv.getBaseY()
                + " h " + wv.getTileHeights()[wv.getPlane()][pos[2]][pos[3]]
                + " hgame " + c.getTopLevelWorldView().getTileHeight(pos[4], pos[5], wv.getPlane())
                + " canvas " + p + " of " + c.getViewportWidth() + "x" + c.getViewportHeight());
            if ((tick % 10) == 0) {
                int root = Native.rootInterface(), n = Native.widgetChildCount(root);
                StringBuilder sb = new StringBuilder("root " + root + ":");
                for (int i = 0; i < n; i++) {
                    AndroidWidget w = new AndroidWidget(root << 16 | i);
                    if (w.rawGeo() == null || w.isHidden() || w.getWidth() <= 0) continue;
                    java.awt.Rectangle r = w.getBounds();
                    sb.append(' ').append(i).append('=').append(r.x).append(',').append(r.y).append(' ').append(r.width).append('x').append(r.height);
                }
                Log.i(TAG, sb.toString());
            }
        } catch (Throwable t) {
            Log.w(TAG, "camlog: " + t);
        }
    }
}
