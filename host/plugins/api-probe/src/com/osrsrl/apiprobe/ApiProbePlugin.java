package com.osrsrl.apiprobe;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.lang.reflect.Array;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import javax.inject.Inject;
import javax.inject.Named;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.Player;
import net.runelite.api.Rasterizer;
import net.runelite.api.Scene;
import net.runelite.api.Tile;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.widgets.Widget;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Calls every abstract method of every net.runelite.api interface on a live instance, by reflection,
 * and writes one row per method to <runeLiteDir>/api-probe.tsv (tools/api-probe.sh). Results:
 * pass (a value of the declared type, no exception; setters: set, read back, restored),
 * throw (exception), inert (resolves to a generated *Stub/*Bag or an Inert path), mismatch (a setter
 * read back something else), unchecked (a native-hook action whose effect the API cannot read),
 * unreached (no live instance) and skipped (the state it needs is missing; the reason is the evidence).
 * Runs on the game thread, ROWS_PER_TICK rows per GameTick, once per load; the LAST rows after all others.
 */
@PluginDescriptor(name = "API Probe", description = "Probes every net.runelite.api method (tools/api-probe.sh)", enabledByDefault = true)
public class ApiProbePlugin extends Plugin {
    private static final Logger log = LoggerFactory.getLogger(ApiProbePlugin.class);
    private static final int ROWS_PER_TICK = 40;
    private static final String API = "net.runelite.api.";

    /** Interfaces the client calls rather than implements: plugins or RuneLite provide them. */
    static final Map<String, String> EXCLUDED = new LinkedHashMap<>();
    static {
        EXCLUDED.put(API + "hooks.Callbacks", "RuneLite's callbacks: the game calls them, plugins never do");
        EXCLUDED.put(API + "hooks.DrawCallbacks", "a GPU plugin implements it and the client calls it");
        EXCLUDED.put(API + "widgets.JavaScriptCallback", "a plugin's widget listener: the client calls it");
        EXCLUDED.put(API + "TileFunction", "a plugin's minimap tile drawer: the client calls it");
        EXCLUDED.put(API + "ClientConfiguration", "RuneLite's loader implements it (RSAppletStub) and hands it to GameEngine.setConfiguration");
        // One method: interface + "." + name.
        EXCLUDED.put(API + "Client.setCameraMouseButtonMask", "the game starts its camera drag (touch state 8) from a touch only: its input dispatcher sends a mouse's right and middle buttons to their own listeners, never to the touch state machine, so no button can be selected (the host keeps it a no-op)");
    }

    /**
     * Rows that change what the server or the session sees, run one at a time after every other row, each once
     * the previous one's check is done: a client-side close of a modal the run set-up opened, then a walk onto the
     * player's own tile (the server closes its modal too, and the fight stops), then camera drags on the scene (real
     * touch input, with no modal over the scene), then a scene reload.
     */
    static final List<String> LAST = Arrays.asList("Client.closeInterface", "Client.menuAction", "Client.setInvertPitch", "Client.setInvertYaw", "Client.setGameState");

    interface Turned { void run(int yaw, int pitch) throws Exception; }

    /**
     * A drag on the scene through the host canvas, as the devtools drag (press, 8 moves 60 ms apart, release, on a thread
     * of its own: the game reads a move per cycle), started at the viewport's centre with the pitch target mid-range so it
     * is not clamped; a tick after the release (the glide is over in a few cycles) the targets' changes go to `then` (yaw
     * wrapped to -8192..8191).
     */
    void cameraDrag(Row row, Turned then) {
        java.awt.Canvas cv = client.getCanvas();
        int x = client.getCenterX(), y = client.getCenterY();
        client.setCameraPitchTarget(0x7fc);
        int yaw0 = client.getCameraYawTarget(), pitch0 = client.getCameraPitchTarget();
        Thread t = new Thread(() -> {
            try {
                mouse(cv, java.awt.event.MouseEvent.MOUSE_PRESSED, x, y);
                for (int i = 1; i <= 8; i++) {
                    Thread.sleep(60);
                    mouse(cv, java.awt.event.MouseEvent.MOUSE_DRAGGED, x + 20 * i, y + 8 * i);
                }
                Thread.sleep(60);
                mouse(cv, java.awt.event.MouseEvent.MOUSE_RELEASED, x + 160, y + 64);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }, "api-probe-drag");
        t.start();
        later(row, afterDrag(row, t, () -> then.run(((client.getCameraYawTarget() - yaw0 + 8192) & 0x3fff) - 8192, client.getCameraPitchTarget() - pitch0)));
    }

    /** `then` on the first tick after the drag thread has sent its release. */
    private Check afterDrag(Row row, Thread t, Check then) {
        return () -> later(row, t.isAlive() ? afterDrag(row, t, then) : then);
    }

    private static void mouse(java.awt.Canvas cv, int id, int x, int y) {
        cv.dispatchEvent(new java.awt.event.MouseEvent(cv, id, System.currentTimeMillis(), 0, x, y, 1, false, java.awt.event.MouseEvent.BUTTON1));
    }

    @Inject private Client client;
    @Inject private EventBus eventBus;
    @Inject @Named("runeLiteDir") private File dir;

    private final Map<Class<?>, Object> live = new HashMap<>();
    private final Set<String> nativeImpl = new HashSet<>();
    private final Map<String, List<Row>> byIface = new TreeMap<>();
    private final List<Row> rows = new ArrayList<>();
    private final List<Row> deferred = new ArrayList<>();
    private String apiJar = "?", build = "?";
    private Iterator<Row> current;
    private Object currentInstance;
    private boolean seeded, retried, done, lastPhase;
    private final List<Object[]> lastRows = new ArrayList<>();  // {Row, instance}

    static final class Row {
        String iface, name, desc, result, evidence = "";
        Method m;
        boolean nat;
    }

    @Override
    protected void startUp() throws Exception {
        ClassLoader api = Client.class.getClassLoader();
        try (BufferedReader r = reader(getClass().getClassLoader().getResourceAsStream("api-methods.txt"))) {
            for (String line; (line = r.readLine()) != null; ) {
                if (line.startsWith("# ")) { apiJar = line.substring(2); continue; }
                String[] f = line.split("\t");
                if (EXCLUDED.containsKey(f[0]) || EXCLUDED.containsKey(f[0] + "." + f[1])) continue;
                Row row = new Row();
                row.iface = f[0];
                row.name = f[1];
                row.desc = f[2];
                for (Method m : Class.forName(f[0], false, api).getDeclaredMethods())
                    if (m.getName().equals(f[1]) && desc(m).equals(f[2])) row.m = m;
                if (row.m == null) { row.result = "throw"; row.evidence = "method not found in the client's API classes"; }
                rows.add(row);
                byIface.computeIfAbsent(f[0], k -> new ArrayList<>()).add(row);
            }
        }
        try (BufferedReader r = reader(api.getResourceAsStream("api-native.txt"))) {
            for (String line; (line = r.readLine()) != null; ) nativeImpl.add(line);
        } catch (Exception e) {
            log.warn("api-probe: no api-native.txt in host.jar: {}", e.toString());
        }
        try (BufferedReader r = reader(api.getResourceAsStream("osrsrl-build.txt"))) {
            build = r.readLine();
        } catch (Exception e) {
            log.warn("api-probe: no osrsrl-build.txt in host.jar");
        }
        log.info("api-probe: {} rows, build {}", rows.size(), build);
    }

    private static BufferedReader reader(InputStream in) {
        return new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
    }

    @Subscribe
    public void onGameTick(GameTick t) {
        if (done || client.getGameState() != GameState.LOGGED_IN || client.getLocalPlayer() == null) return;
        try {
            if (!seeded) { seed(); seeded = true; return; }
            if (byIface.containsKey(API + "PendingLogin") && client.getFriendContainer() != null)  // notices live ~5 s
                for (net.runelite.api.PendingLogin l : client.getFriendContainer().getPendingLogins()) { live.putIfAbsent(net.runelite.api.PendingLogin.class, l); break; }
            runLater();
            for (int n = 0; n < ROWS_PER_TICK; n++) {
                if (current == null || !current.hasNext()) {
                    current = null;
                    if (!next()) {
                        if (!later.isEmpty() || awaitingHitsplat() || awaited()) return;
                        if (lastRows.isEmpty()) { finish(); return; }
                        // one LAST row per tick, in LAST order, each once the previous one's checks are done
                        lastPhase = true;
                        lastRows.sort(java.util.Comparator.comparingInt(l -> LAST.indexOf(key((Row) l[0]))));
                        Object[] l = lastRows.remove(0);
                        log.info("api-probe: last: {}", key((Row) l[0]));
                        probe((Row) l[0], l[1]);
                        return;
                    }
                    continue;
                }
                Row row = current.next();
                if (row.result != null) continue;
                log.info("api-probe: {}.{}{}", row.iface, row.name, row.desc);
                probe(row, currentInstance);
            }
        } catch (Throwable e) {
            log.error("api-probe failed", e);
            done = true;
            try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(dir, "api-probe.tsv")), StandardCharsets.UTF_8)) {
                w.write("# error: " + e + "\n");  // ends tools/api-probe.sh's wait; it reports the missing rows
            } catch (Exception ignored) {
                // logcat has the failure
            }
        }
    }

    // Combat state comes from events: the first hitsplat and graphics object (a map spotanim) as they appear. Only
    // as their own interface: a graphics object's Node/Renderable methods would take those interfaces' rows.
    @Subscribe
    public void onHitsplatApplied(HitsplatApplied e) {
        if (!done && e.getHitsplat() != null) live.putIfAbsent(net.runelite.api.Hitsplat.class, e.getHitsplat());
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated e) {
        if (!done && e.getGraphicsObject() != null) live.putIfAbsent(net.runelite.api.GraphicsObject.class, e.getGraphicsObject());
    }

    // A health bar type as the game first uses it, and the script a callback runs in (only as their own interface).
    @Subscribe
    public void onPostHealthBarConfig(net.runelite.api.events.PostHealthBarConfig e) {
        if (!done && e.getHealthBarConfig() != null) live.putIfAbsent(net.runelite.api.HealthBarConfig.class, e.getHealthBarConfig());
    }

    @Subscribe
    public void onScriptCallbackEvent(net.runelite.api.events.ScriptCallbackEvent e) {
        if (!done && e.getScript() != null) live.putIfAbsent(net.runelite.api.Script.class, e.getScript());
    }

    /**
     * Interfaces the run set-up makes live while the probe runs, named in `debug.osrsrl.probewait` (comma-separated
     * simple names, e.g. "PendingLogin,Script": a friend logs in, a tab with a callback script opens): the probe ends
     * only once each has an instance.
     */
    private boolean awaited() {
        for (String t : probewait()) if (byIface.containsKey(API + t)) return true;
        return false;
    }

    /** The names in `debug.osrsrl.probewait`; "modal" has closeInterface wait for a modal interface to close. */
    static List<String> probewait() {
        List<String> out = new ArrayList<>();
        try {
            String w = (String) host("Native", "property", new Class<?>[] {String.class}, "debug.osrsrl.probewait");
            if (w != null) for (String t : w.split(",")) if (!t.trim().isEmpty()) out.add(t.trim());
        } catch (Exception e) {
            // no property: nothing to wait for
        }
        return out;
    }

    /** In combat (the local player interacting with someone) and no hitsplat seen yet: the next one is on its way. */
    private boolean awaitingHitsplat() {
        return live.get(net.runelite.api.Hitsplat.class) == null && byIface.containsKey(API + "Hitsplat")
            && client.getLocalPlayer().getInteracting() != null;
    }

    /** A row's check that reads its effect on the next game tick (client cycles ran in between). */
    interface Check {
        void run() throws Exception;
    }

    private final List<Object[]> later = new ArrayList<>();  // {Row, Check}

    void later(Row row, Check c) {
        row.result = "pending";
        later.add(new Object[] {row, c});
    }

    private void runLater() {
        List<Object[]> now = new ArrayList<>(later);
        later.clear();
        for (Object[] l : now) {
            Row row = (Row) l[0];
            try {
                ((Check) l[1]).run();
            } catch (Throwable e) {
                Throwable c = e instanceof InvocationTargetException ? e.getCause() : e;
                set(row, "throw", c.getClass().getName() + ": " + c.getMessage());
            }
        }
    }

    // Events the hook rows raise, recorded for their later checks.
    final List<net.runelite.api.events.SoundEffectPlayed> sounds = new ArrayList<>();
    final List<net.runelite.api.events.AreaSoundEffectPlayed> areaSounds = new ArrayList<>();

    @Subscribe
    public void onSoundEffectPlayed(net.runelite.api.events.SoundEffectPlayed e) {
        if (sounds.size() < 256) sounds.add(e);
    }

    @Subscribe
    public void onAreaSoundEffectPlayed(net.runelite.api.events.AreaSoundEffectPlayed e) {
        if (areaSounds.size() < 256) areaSounds.add(e);
    }

    final List<net.runelite.api.MenuEntry> clicks = new ArrayList<>();  // menu clicks in the LAST phase (menuAction's walk)

    @Subscribe
    public void onMenuOptionClicked(net.runelite.api.events.MenuOptionClicked e) {
        if (lastPhase && clicks.size() < 256) clicks.add(e.getMenuEntry());
    }

    /** The next interface with a live instance and unprobed rows; then one retry of deferred rows. */
    private boolean next() {
        for (Map.Entry<String, List<Row>> e : byIface.entrySet()) {
            Object inst = instance(e.getKey());
            if (inst == null) continue;
            current = byIface.remove(e.getKey()).iterator();
            currentInstance = inst;
            return true;  // the loop ends here, so removing the entry is safe
        }
        if (!retried && !deferred.isEmpty()) {
            retried = true;
            for (Row r : deferred) {
                r.result = null;
                byIface.computeIfAbsent(r.iface, k -> new ArrayList<>()).add(r);
            }
            deferred.clear();
            return next();
        }
        return false;
    }

    private Object instance(String iface) {
        try {
            return live.get(Class.forName(iface, false, Client.class.getClassLoader()));
        } catch (ClassNotFoundException e) {
            return null;
        }
    }

    // ---- live instances ----

    /** Instances the safe-argument getters don't reach: the local player first (it is the Actor), a root widget, the scene's objects. */
    private void seed() {
        register(client, 0);
        try {
            seedLive();
        } catch (Throwable e) {
            log.warn("api-probe: seeding stopped: {}", e.toString());
        }
        log.info("api-probe: seeded {} live types", live.size());
    }

    private void seedLive() {
        Player me = client.getLocalPlayer();
        register(me, 0);
        register(me.getWorldLocation(), 0);
        register(me.getLocalLocation(), 0);
        register(new net.runelite.api.Point(0, 0), 0);
        // createChild/deleteAllChildren run on this widget, so it must hold no dynamic children of the game's.
        Widget[] roots = client.getWidgetRoots();
        if (roots != null)
            for (Widget w : roots)
                if (w != null && (w.getDynamicChildren() == null || w.getDynamicChildren().length == 0)) { register(w, 0); break; }
        if (roots != null) register(font(roots, 0), 0);  // the first widget with a game font
        register(client.getItemContainer(93), 0);  // inventory
        register(client.getMenu(), 0);
        register(client.getTopLevelWorldView(), 0);
        // The NPC whose first model the game's model store holds and that has an idle sequence: the Model rows and
        // applyTransformations run on it (an NPC whose model is missing gives loadModel null).
        for (net.runelite.api.NPC n : client.getTopLevelWorldView().npcs()) {
            int[] m = n.getComposition() != null ? n.getComposition().getModels() : null;
            if (m != null && m.length > 0 && n.getIdlePoseAnimation() != -1 && client.loadModelData(m[0]) != null) { register(n, 0); break; }
        }
        for (Object n : client.getTopLevelWorldView().npcs()) { register(n, 0); break; }  // any NPC when none qualifies
        for (net.runelite.api.NPC n : client.getTopLevelWorldView().npcs()) {  // overrides are rare: any NPC's
            register(n.getModelOverrides(), 0);
            register(n.getChatheadOverrides(), 0);
        }
        Scene scene = client.getTopLevelWorldView().getScene();
        register(scene, 0);
        LocalPoint lp = me.getLocalLocation();
        int plane = client.getTopLevelWorldView().getPlane();
        Tile[][][] tiles = scene.getTiles();
        register(tiles[plane][lp.getSceneX()][lp.getSceneY()], 0);
        List<java.util.function.Function<Tile, Object>> getters = new ArrayList<>(Arrays.asList(Tile::getGameObjects, Tile::getWallObject,
            Tile::getDecorativeObject, Tile::getGroundObject, Tile::getItemLayer, Tile::getGroundItems, Tile::getSceneTilePaint, Tile::getSceneTileModel));
        for (Tile[] col : tiles[plane]) {
            for (Tile tile : col) {
                if (tile == null) continue;
                for (Iterator<java.util.function.Function<Tile, Object>> it = getters.iterator(); it.hasNext(); ) {
                    try {
                        register(it.next().apply(tile), 0);
                    } catch (RuntimeException e) {
                        it.remove();  // a getter the host lacks: its rows say so
                    }
                }
            }
        }
        // An animated loc (DynamicObject, only as its own interface): the first scene object renderable that is one.
        for (Tile[] col : tiles[plane])
            for (Tile tile : col) {
                if (tile == null || live.containsKey(net.runelite.api.DynamicObject.class)) continue;
                List<Object> rs = new ArrayList<>();
                for (net.runelite.api.GameObject g : tile.getGameObjects()) if (g != null) rs.add(g.getRenderable());
                if (tile.getWallObject() != null) { rs.add(tile.getWallObject().getRenderable1()); rs.add(tile.getWallObject().getRenderable2()); }
                if (tile.getDecorativeObject() != null) { rs.add(tile.getDecorativeObject().getRenderable()); rs.add(tile.getDecorativeObject().getRenderable2()); }
                if (tile.getGroundObject() != null) rs.add(tile.getGroundObject().getRenderable());
                for (Object r : rs) if (r instanceof net.runelite.api.DynamicObject) { live.put(net.runelite.api.DynamicObject.class, r); break; }
            }
    }

    private static Object font(Widget[] ws, int depth) {
        if (ws == null || depth > 8) return null;
        for (Widget w : ws) {
            if (w == null) continue;
            if (w.getFontId() >= 0 && w.getFont() != null) return w.getFont();
            for (Widget[] kids : new Widget[][] {w.getStaticChildren(), w.getNestedChildren(), w.getDynamicChildren()}) {
                Object f = font(kids, depth + 1);
                if (f != null) return f;
            }
        }
        return null;
    }

    /** Records `v` (and the elements of arrays, iterables and maps) as the live instance of each of its types not yet held. */
    private void register(Object v, int depth) {
        if (v == null || depth > 3) return;
        Class<?> c = v.getClass();
        if (c.isArray()) {
            if (c.getComponentType().isPrimitive()) return;
            for (int i = 0, n = Array.getLength(v); i < n; i++) register(Array.get(v, i), depth + 1);
            return;
        }
        if (v instanceof Map) {
            int n = 0;
            for (Object o : ((Map<?, ?>) v).values()) if (n++ < 64) register(o, depth + 1);
            return;
        }
        boolean inert = inert(v);
        for (Class<?> t : types(c)) {
            Object held = live.get(t);
            if (held == null || (inert(held) && !inert)) live.put(t, v);
        }
        if (v instanceof Iterable && !(v instanceof Collection && ((Collection<?>) v).size() > 4096)) {
            int n = 0;
            try {
                for (Object o : (Iterable<?>) v) if (n++ < 64) register(o, depth + 1); else break;
            } catch (Throwable e) {
                // a lazily backed iterable can fail; the instance itself is registered
            }
        }
    }

    private static Set<Class<?>> types(Class<?> c) {
        Set<Class<?>> out = new HashSet<>();
        for (Class<?> k = c; k != null && k != Object.class; k = k.getSuperclass()) {
            out.add(k);
            addInterfaces(k, out);
        }
        return out;
    }

    private static void addInterfaces(Class<?> c, Set<Class<?>> out) {
        for (Class<?> i : c.getInterfaces()) if (out.add(i)) addInterfaces(i, out);
    }

    static boolean inert(Object v) {
        if (v == null || !Proxy.isProxyClass(v.getClass())) return false;
        try {
            return String.valueOf(v).startsWith("Inert ");
        } catch (Throwable e) {
            return false;
        }
    }

    // ---- one row ----

    private void probe(Row row, Object inst) {
        String key = key(row);
        Method m = row.m;
        Method impl;
        try {
            impl = inst.getClass().getMethod(m.getName(), m.getParameterTypes());
        } catch (NoSuchMethodException e) {
            impl = m;
        }
        String decl = impl.getDeclaringClass().getName();
        row.nat = nativeImpl.contains(decl + "\t" + impl.getName() + "\t" + desc(impl));
        String inertBy = inert(inst) ? "Inert proxy" : isGenerated(impl.getDeclaringClass()) ? impl.getDeclaringClass().getSimpleName() : null;
        if (LAST.contains(key) && !lastPhase) { row.result = "last"; lastRows.add(new Object[] {row, inst}); return; }
        try {
            Special s = SPECIAL.get(key + m.getParameterCount());
            if (s != null) s.run(this, row, inst);
            else if (setter(m) != null) roundTrip(row, inst, setter(m));
            else if (indexedGetter(m) != null) indexedRoundTrip(row, inst, indexedGetter(m));
            else if (m.getName().startsWith("set") && m.getReturnType() == void.class) noGetter(row, inst);
            else call(row, inst);
        } catch (Missing e) {
            if (!retried) { row.result = "deferred"; deferred.add(row); return; }
            set(row, "unreached", "no live " + e.getMessage() + " for an argument");
            return;
        } catch (Throwable e) {
            Throwable c = e instanceof InvocationTargetException ? e.getCause() : e;
            set(row, "throw", c.getClass().getName() + ": " + c.getMessage());
        }
        if (inertBy != null && !"skipped".equals(row.result)) set(row, "inert", inertBy + " (" + row.result + ": " + row.evidence + ")");
    }

    private static boolean isGenerated(Class<?> c) {
        String n = c.getSimpleName();
        return n.endsWith("Stub") || n.endsWith("Bag") || n.contains("Inert");
    }

    private void call(Row row, Object inst) throws Exception {
        Object v = row.m.invoke(inst, args(row));
        Class<?> r = row.m.getReturnType();
        if (r == void.class) {
            if (row.nat) set(row, "unchecked", "native hook; its effect is not readable through the API");
            else set(row, "pass", "returned");
            return;
        }
        register(v, 0);
        if (inert(v)) { set(row, "inert", "returned an Inert " + r.getSimpleName()); return; }
        if (!r.isPrimitive() && v != null && !r.isInstance(v)) { set(row, "mismatch", "returned " + v.getClass().getName()); return; }
        set(row, "pass", describe(v));
    }

    /**
     * A setter with no getter and no effect check: its effect can only be host state, so the instance's
     * fields are compared around the call. No field changed and no native call: the host does nothing (inert).
     */
    private void noGetter(Row row, Object inst) throws Exception {
        Map<String, Object> before = fields(inst);
        row.m.invoke(inst, args(row));
        Map<String, Object> after = fields(inst);
        List<String> changed = new ArrayList<>();
        for (Map.Entry<String, Object> e : before.entrySet())
            if (!same(e.getValue(), after.get(e.getKey()))) changed.add(e.getKey() + " " + describe(e.getValue()) + " -> " + describe(after.get(e.getKey())));
        if (changed.isEmpty() && !row.nat) set(row, "inert", "no effect: none of " + before.size() + " fields of " + inst.getClass().getSimpleName() + " changed, no native call");
        else set(row, "mismatch", "no getter or effect check; changed: " + changed + (row.nat ? " (native)" : ""));
    }

    /** Shallow copy of every instance field of `o` (arrays cloned), by declaring class and name. */
    static Map<String, Object> fields(Object o) throws IllegalAccessException {
        Map<String, Object> out = new TreeMap<>();
        for (Class<?> k = o.getClass(); k != null && k != Object.class; k = k.getSuperclass())
            for (java.lang.reflect.Field f : k.getDeclaredFields()) {
                if (java.lang.reflect.Modifier.isStatic(f.getModifiers())) continue;
                f.setAccessible(true);
                Object v = f.get(o);
                if (v != null && v.getClass().isArray()) v = v instanceof Object[] ? ((Object[]) v).clone() : cloneArray(v);
                out.put(k.getSimpleName() + "." + f.getName(), v);
            }
        return out;
    }

    private static Object cloneArray(Object a) {
        int n = Array.getLength(a);
        Object c = Array.newInstance(a.getClass().getComponentType(), n);
        System.arraycopy(a, 0, c, 0, n);
        return c;
    }

    /** A static method of a host class (net.runelite.host), by reflection: the host's own state for effect checks. */
    static Object host(String cls, String name, Class<?>[] types, Object... args) throws Exception {
        Method m = Class.forName("net.runelite.host." + cls, false, Client.class.getClassLoader()).getDeclaredMethod(name, types);
        m.setAccessible(true);
        return m.invoke(null, args);
    }

    static Object hostField(String cls, String name) throws Exception {
        java.lang.reflect.Field f = Class.forName("net.runelite.host." + cls, false, Client.class.getClassLoader()).getDeclaredField(name);
        f.setAccessible(true);
        return f.get(null);
    }

    static int clientInt(int which) throws Exception {
        return (Integer) host("Native", "clientInt", new Class<?>[] {int.class}, which);
    }

    static final int CHAT_CYCLE = 3, CHANGED_SKILL_COUNT = 4, ORB_SPEED = 5, PITCH_RELAXER = 6;  // Native.clientInt indices

    /** setX(v) with a getter getX()/isX(): set an altered value, read it back, restore, read again. */
    private void roundTrip(Row row, Object inst, Method get) throws Exception {
        Object old = get.invoke(inst);
        Object alt = alter(old, row.m.getParameterTypes()[0]);
        if (alt == old && row.m.getParameterTypes()[0] == Widget.class && inst instanceof Widget) alt = old == inst ? null : inst;  // setDragParent: the widget itself
        row.m.invoke(inst, alt);
        Object read = get.invoke(inst);
        row.m.invoke(inst, old);
        Object back = get.invoke(inst);
        String ev = get.getName() + " " + describe(old) + " -> set " + describe(alt) + " -> " + describe(read) + " -> restored " + describe(back);
        set(row, same(read, alt) && same(back, old) ? "pass" : "mismatch", ev);
    }

    /** setX(k, v) with a getter getX(k)/getXValue(k). */
    private void indexedRoundTrip(Row row, Object inst, Method get) throws Exception {
        int k = KEYS.getOrDefault(simple(row.iface) + "." + row.name, 0);
        Object old = get.invoke(inst, k);
        Object alt = alter(old, row.m.getParameterTypes()[1]);
        row.m.invoke(inst, k, alt);
        Object read = get.invoke(inst, k);
        row.m.invoke(inst, k, old);
        Object back = get.invoke(inst, k);
        String ev = get.getName() + "(" + k + ") " + describe(old) + " -> set " + describe(alt) + " -> " + describe(read) + " -> restored " + describe(back);
        set(row, same(read, alt) && same(back, old) ? "pass" : "mismatch", ev);
    }

    /** Keys for indexed setters: ids the host's own set hook was verified with (docs/plugin-host.md). */
    static final Map<String, Integer> KEYS = new HashMap<>();
    static {
        KEYS.put("Client.setVarbit", 8119);
        KEYS.put("Client.setVarpValue", 1000);
        KEYS.put("Client.setVarpLongValue", 1000);
        KEYS.put("Client.setVarcIntValue", 1000);
        KEYS.put("Client.setVarcStrValue", 1001);
    }

    private static Method setter(Method m) {
        if (m.getParameterCount() != 1) return null;
        String n = m.getName();
        Class<?> p = m.getParameterTypes()[0];
        List<String> names = new ArrayList<>();
        if (ALIAS.containsKey(n)) {
            names.add(ALIAS.get(n));
        } else if (n.startsWith("set") && n.length() > 3) {
            String suf = n.substring(3);
            names.add("get" + suf);
            names.add("is" + suf);
            if (suf.startsWith("Is")) names.add("is" + suf.substring(2));
        } else if (m.getReturnType() == m.getDeclaringClass()) {
            names.add(n);  // builder style: MenuEntry.onClick(Consumer) / onClick()
        } else {
            return null;
        }
        for (String g : names) {
            try {
                Method get = m.getDeclaringClass().getMethod(g);
                if (box(get.getReturnType()) == box(p) || p.isAssignableFrom(get.getReturnType())) return get;
            } catch (NoSuchMethodException e) {
                // next name
            }
        }
        return null;
    }

    /** Setters whose getter has another name. */
    static final Map<String, String> ALIAS = new HashMap<>();
    static {
        ALIAS.put("setActionFrame", "getAnimationFrame");  // desktop's older name for the animation frame
    }

    private static Method indexedGetter(Method m) {
        if (!m.getName().startsWith("set") || m.getParameterCount() != 2 || m.getParameterTypes()[0] != int.class) return null;
        String suf = m.getName().substring(3);
        Class<?> p = m.getParameterTypes()[1];
        String type = p == int.class ? "Int" : p.isPrimitive() ? Character.toUpperCase(p.getName().charAt(0)) + p.getName().substring(1) : p.getSimpleName();
        // ParamHolder.setValue(int, int/long/String) reads back as getIntValue/getLongValue/getStringValue
        for (String g : new String[] {"get" + suf, "get" + suf + "Value", "get" + type + suf}) {
            try {
                Method get = m.getDeclaringClass().getMethod(g, int.class);
                if (box(get.getReturnType()) == box(p)) return get;
            } catch (NoSuchMethodException e) {
                // next name
            }
        }
        return null;
    }

    // ---- special effect checks ----

    interface Special {
        void run(ApiProbePlugin p, Row row, Object inst) throws Exception;
    }

    static final int PROBE_SLOT = 7, LEVELUP_SPOTANIM = 199, SPOTANIM_DELAY = 10000;  // 200 s at 50 cycles/s
    static final int PROJ_SPOTANIM = 100, PROJ_HEIGHT = 40, PROJ_CYCLES = 3000;

    /** The game moves a projectile when it draws a frame: checked each tick until it has moved, or it ended unmoved. */
    static Check moved(ApiProbePlugin p, Row row, net.runelite.api.Projectile pr, String ev) {
        return () -> {
            int cycle = p.client.getGameCycle();
            if (pr.getX() == 0 && pr.getY() == 0 && cycle < pr.getEndCycle()) { p.later(row, moved(p, row, pr, ev)); return; }
            p.set(row, pr.getX() != 0 || pr.getY() != 0 ? "pass" : "mismatch",
                ev + "; cycle " + cycle + " at x/y/z " + pr.getX() + "," + pr.getY() + "," + pr.getZ());
        };
    }
    static final Map<String, Special> SPECIAL = new HashMap<>();
    static {
        // Hint arrows: set, observe hasHintArrow and its target, clear, observe it gone.
        Special arrow = (p, row, inst) -> {
            Client c = (Client) inst;
            if (c.hasHintArrow()) { p.set(row, "skipped", "a hint arrow was already set; the probe would lose it"); return; }
            Object target = row.name.equals("clearHintArrow") ? c.getLocalPlayer().getWorldLocation() : p.arg(row.m.getParameterTypes()[0]);
            Method set = Client.class.getMethod("setHintArrow", target instanceof WorldPoint ? WorldPoint.class : row.m.getParameterTypes()[0]);
            set.invoke(c, target);
            boolean on = c.hasHintArrow();
            String ev = "set " + describe(target) + ": hasHintArrow " + on + " type " + c.getHintArrowType() + " point " + describe(c.getHintArrowPoint())
                + " npc " + describe(c.getHintArrowNpc()) + " player " + describe(c.getHintArrowPlayer());
            c.clearHintArrow();
            boolean off = !c.hasHintArrow();
            p.set(row, on && off ? "pass" : "mismatch", ev + "; cleared: hasHintArrow " + !off);
        };
        SPECIAL.put("Client.setHintArrow1", arrow);
        SPECIAL.put("Client.clearHintArrow0", arrow);
        // The game reads chat lines it owns; the probe's line says what it is.
        SPECIAL.put("Client.addChatMessage4", (p, row, inst) -> {
            Object v = row.m.invoke(inst, ChatMessageType.GAMEMESSAGE, "", "api-probe: addChatMessage", null);
            p.set(row, v != null ? "pass" : "mismatch", describe(v) + " " + (v != null ? ((net.runelite.api.MessageNode) v).getValue() : ""));
            p.register(v, 0);
        });
        SPECIAL.put("Client.addChatMessage5", (p, row, inst) -> {
            Object v = row.m.invoke(inst, ChatMessageType.GAMEMESSAGE, "", "api-probe: addChatMessage", null, false);
            p.set(row, v != null ? "pass" : "mismatch", describe(v) + " " + (v != null ? ((net.runelite.api.MessageNode) v).getValue() : ""));
        });
        // Menu drawing into the frame buffer plugins read: pixels changed inside the open menu's box (none
        // drawn when no menu is open, as desktop); the buffer is restored.
        Special menuDraw = (p, row, inst) -> {
            Client c = (Client) inst;
            net.runelite.api.BufferProvider b = c.getBufferProvider();
            boolean open = c.isMenuOpen();
            int[] before = b.getPixels().clone();
            row.m.invoke(c, 255);
            int[] after = b.getPixels();
            int changed = 0;
            for (int i = 0; i < Math.min(before.length, after.length); i++) if (before[i] != after[i]) changed++;
            System.arraycopy(before, 0, after, 0, Math.min(before.length, after.length));
            String where = open ? "menu open at " + c.getMenuX() + "," + c.getMenuY() + " " + c.getMenuWidth() + "x" + c.getMenuHeight() + " (" + c.getMenuEntries().length + " entries)" : "no menu open";
            p.set(row, open ? (changed > 0 ? "pass" : "mismatch") : (changed == 0 ? "pass" : "mismatch"), where + ": " + changed + " pixels drawn (restored)");
        };
        SPECIAL.put("Client.draw2010Menu1", menuDraw);
        SPECIAL.put("Client.drawOriginalMenu1", menuDraw);
        // The game's socket: its fd number and peer, which must be the world host the client reports.
        SPECIAL.put("Client.getSocketFD0", (p, row, inst) -> {
            Client c = (Client) inst;
            java.io.FileDescriptor fd = c.getSocketFD();
            if (fd == null) { p.set(row, "mismatch", "null (world host " + describe(c.getWorldHost()) + ")"); return; }
            Object n = java.io.FileDescriptor.class.getMethod("getInt$").invoke(fd);
            Object peer = Class.forName("android.system.Os").getMethod("getpeername", java.io.FileDescriptor.class).invoke(null, fd);
            String host = c.getWorldHost();
            // InetSocketAddress prints "/<address>:<port>" (no java.net refs here: the host's netscan refuses them)
            boolean ok = host != null && String.valueOf(peer).startsWith("/" + host + ":");
            p.set(row, ok ? "pass" : "mismatch", "fd " + n + " peer " + peer + ", world host " + describe(host));
        });
        // The world map's regions (loaded once the map has been opened): the first region with icons is the
        // WorldMapRegion rows' instance and its first icon the WorldMapIcon rows'.
        SPECIAL.put("WorldMapRenderer.getMapRegions0", (p, row, inst) -> {
            p.call(row, inst);
            for (net.runelite.api.worldmap.WorldMapRegion[] col : ((net.runelite.api.worldmap.WorldMapRenderer) inst).getMapRegions())
                for (net.runelite.api.worldmap.WorldMapRegion r : col)
                    if (r != null && !r.getMapIcons().isEmpty()) {
                        p.live.put(net.runelite.api.worldmap.WorldMapRegion.class, r);
                        p.live.put(net.runelite.api.worldmap.WorldMapIcon.class, r.getMapIcons().iterator().next());
                        return;
                    }
        });
        // A graphics object's finished flag (finished() is its getter); true ends the animation, harmless on a
        // combat swing's spotanim: the game drops it on its next pass.
        SPECIAL.put("GraphicsObject.setFinished1", (p, row, inst) -> {
            net.runelite.api.GraphicsObject g = (net.runelite.api.GraphicsObject) inst;
            boolean old = g.finished();
            g.setFinished(!old);
            boolean read = g.finished();
            g.setFinished(old);
            p.set(row, read == !old && g.finished() == old ? "pass" : "mismatch", "finished " + old + " -> set " + !old + " -> " + read + " -> restored " + g.finished());
        });
        // Posing: the result's vertices against the input model's, so the evidence shows the sequence moved them.
        SPECIAL.put("Client.applyTransformations5", (p, row, inst) -> {
            Object[] a = p.args(row);
            net.runelite.api.Model in = (net.runelite.api.Model) a[0];
            net.runelite.api.Model out = (net.runelite.api.Model) row.m.invoke(inst, a);
            p.register(out, 0);
            if (out == null) { p.set(row, "mismatch", "null for sequence " + describe(a[1] == null ? null : ((net.runelite.api.Animation) a[1]).getId())); return; }
            int moved = 0, n = Math.min(in.getVerticesCount(), out.getVerticesCount());
            for (int i = 0; i < n; i++)
                if (in.getVerticesX()[i] != out.getVerticesX()[i] || in.getVerticesY()[i] != out.getVerticesY()[i] || in.getVerticesZ()[i] != out.getVerticesZ()[i]) moved++;
            p.set(row, moved > 0 ? "pass" : "mismatch", describe(out) + ": sequence " + ((net.runelite.api.Animation) a[1]).getId() + " frame 0 moved " + moved + " of " + n + " vertices");
        });
        // Widget position, size and actions: set altered values, read back, restore.
        SPECIAL.put("Widget.setPos2", (p, row, inst) -> {
            Widget w = (Widget) inst;
            int x = w.getOriginalX(), y = w.getOriginalY();
            w.setPos(x + 1, y + 1);
            int rx = w.getOriginalX(), ry = w.getOriginalY();
            w.setPos(x, y);
            p.set(row, rx == x + 1 && ry == y + 1 && w.getOriginalX() == x ? "pass" : "mismatch", "original " + x + "," + y + " -> " + rx + "," + ry + " -> restored " + w.getOriginalX() + "," + w.getOriginalY());
        });
        SPECIAL.put("Widget.setPos4", (p, row, inst) -> {
            Widget w = (Widget) inst;
            int x = w.getOriginalX(), y = w.getOriginalY(), xm = w.getXPositionMode(), ym = w.getYPositionMode();
            w.setPos(x + 1, y + 1, xm, ym);
            int rx = w.getOriginalX(), ry = w.getOriginalY();
            w.setPos(x, y, xm, ym);
            p.set(row, rx == x + 1 && ry == y + 1 && w.getOriginalX() == x ? "pass" : "mismatch", "original " + x + "," + y + " modes " + xm + "," + ym + " -> " + rx + "," + ry + " -> restored " + w.getOriginalX() + "," + w.getOriginalY());
        });
        SPECIAL.put("Widget.setSize2", (p, row, inst) -> {
            Widget w = (Widget) inst;
            int x = w.getOriginalWidth(), y = w.getOriginalHeight();
            w.setSize(x + 1, y + 1);
            int rx = w.getOriginalWidth(), ry = w.getOriginalHeight();
            w.setSize(x, y);
            p.set(row, rx == x + 1 && ry == y + 1 && w.getOriginalWidth() == x ? "pass" : "mismatch", "original " + x + "x" + y + " -> " + rx + "x" + ry + " -> restored " + w.getOriginalWidth() + "x" + w.getOriginalHeight());
        });
        SPECIAL.put("Widget.setSize4", (p, row, inst) -> {
            Widget w = (Widget) inst;
            int x = w.getOriginalWidth(), y = w.getOriginalHeight(), xm = w.getWidthMode(), ym = w.getHeightMode();
            w.setSize(x + 1, y + 1, xm, ym);
            int rx = w.getOriginalWidth(), ry = w.getOriginalHeight();
            w.setSize(x, y, xm, ym);
            p.set(row, rx == x + 1 && ry == y + 1 && w.getOriginalWidth() == x ? "pass" : "mismatch", "original " + x + "x" + y + " modes " + xm + "," + ym + " -> " + rx + "x" + ry + " -> restored " + w.getOriginalWidth() + "x" + w.getOriginalHeight());
        });
        SPECIAL.put("Widget.setAction2", (p, row, inst) -> {
            Widget w = (Widget) inst;
            String[] a = w.getActions();
            String old = a != null && a.length > 0 ? a[0] : null;
            w.setAction(0, "api-probe");
            String[] r = w.getActions();
            String read = r != null && r.length > 0 ? r[0] : null;
            w.setAction(0, old);
            p.set(row, "api-probe".equals(read) ? "pass" : "mismatch", "action 0 " + describe(old) + " -> " + describe(read) + " -> restored " + describe(w.getActions() != null && w.getActions().length > 0 ? w.getActions()[0] : null));
        });
        SPECIAL.put("Widget.clearActions0", (p, row, inst) -> {
            Widget w = (Widget) inst;
            String[] a = w.getActions();
            w.clearActions();
            String[] r = w.getActions();
            boolean cleared = r == null || Arrays.stream(r).allMatch(s -> s == null || s.isEmpty());
            if (a != null) for (int i = 0; i < a.length; i++) w.setAction(i, a[i]);
            p.set(row, cleared ? "pass" : "mismatch", "actions " + Arrays.toString(a) + " -> " + Arrays.toString(r) + " -> restored " + Arrays.toString(w.getActions()));
        });
        // Dynamic children on the probe's widget (it holds none of the game's): create, count, delete all, count.
        SPECIAL.put("Widget.deleteAllChildren0", (p, row, inst) -> {
            Widget w = (Widget) inst;
            w.createChild(-1, 3);
            int before = w.getDynamicChildren().length;
            w.deleteAllChildren();
            int after = w.getDynamicChildren().length;
            p.set(row, before == 1 && after == 0 ? "pass" : "mismatch", "dynamic children after createChild " + before + ", after deleteAllChildren " + after);
        });
        Special child = (p, row, inst) -> {
            Widget w = (Widget) inst;
            Widget c = row.m.getParameterCount() == 1 ? w.createChild(3) : w.createChild(-1, 3);
            int n = w.getDynamicChildren().length;
            w.deleteAllChildren();
            p.set(row, c != null && n == 1 ? "pass" : "mismatch", describe(c) + "; dynamic children " + n + " (deleted again)");
        };
        SPECIAL.put("Widget.createChild1", child);
        SPECIAL.put("Widget.createChild2", child);
        // Spot anims: a level-up spot anim (199) with a long delay, so the game keeps it unstarted. The one
        // createSpotAnim adds stays (it is the ActorSpotAnim rows' live instance) until finish removes it.
        SPECIAL.put("Actor.createSpotAnim4", (p, row, inst) -> {
            net.runelite.api.Actor a = (net.runelite.api.Actor) inst;
            a.createSpotAnim(PROBE_SLOT, LEVELUP_SPOTANIM, 0, SPOTANIM_DELAY);
            net.runelite.api.ActorSpotAnim s = a.getSpotAnims().get(PROBE_SLOT);
            p.register(s, 0);
            p.cleanup.add(() -> a.removeSpotAnim(PROBE_SLOT));
            p.set(row, s != null && s.getId() == LEVELUP_SPOTANIM && a.hasSpotAnim(LEVELUP_SPOTANIM) ? "pass" : "mismatch",
                "slot " + PROBE_SLOT + " -> " + describe(s) + "; hasSpotAnim(" + LEVELUP_SPOTANIM + ") " + a.hasSpotAnim(LEVELUP_SPOTANIM));
        });
        SPECIAL.put("Actor.removeSpotAnim1", (p, row, inst) -> {
            net.runelite.api.Actor a = (net.runelite.api.Actor) inst;
            a.createSpotAnim(PROBE_SLOT + 1, LEVELUP_SPOTANIM, 0, SPOTANIM_DELAY);
            Object before = a.getSpotAnims().get(PROBE_SLOT + 1);
            a.removeSpotAnim(PROBE_SLOT + 1);
            Object after = a.getSpotAnims().get(PROBE_SLOT + 1);
            p.set(row, before != null && after == null ? "pass" : "mismatch", "slot " + (PROBE_SLOT + 1) + " " + describe(before) + " -> removed: " + describe(after));
        });
        // Projectiles: spotanim 100 (a fire strike orb, drawn on OpenRune) from the local player's tile to 3
        // tiles east, from the next cycle for 3000 cycles (60 s). Each must be the game's (listed in
        // getProjectiles with the given fields) and move by the next tick (the game advances it); the first is
        // the Projectile rows' live instance. finish ends them.
        Special proj = (p, row, inst) -> {
            Client c = p.client;
            net.runelite.api.Player me = c.getLocalPlayer();
            WorldPoint src = me.getWorldLocation(), dst = src.dx(3);
            LocalPoint lp = me.getLocalLocation();
            int now = c.getGameCycle();
            Object r = row.m.getParameterCount() == 11
                ? row.m.invoke(inst, PROJ_SPOTANIM, src, PROJ_HEIGHT, me, dst, PROJ_HEIGHT, null, now + 1, now + PROJ_CYCLES, 16, 0)
                : row.m.invoke(inst, PROJ_SPOTANIM, src.getPlane(), lp.getX(), lp.getY(), 0, now + 1, now + PROJ_CYCLES, 16, PROJ_HEIGHT, PROJ_HEIGHT,
                    null, lp.getX() + 3 * 128, lp.getY());
            net.runelite.api.Projectile pr = (net.runelite.api.Projectile) r;
            if (pr == null) { p.set(row, "mismatch", "returned null"); return; }
            p.cleanup.add(() -> pr.setEndCycle(c.getGameCycle()));
            p.register(pr, 0);
            boolean listed = false;
            for (net.runelite.api.Projectile q : c.getProjectiles()) listed |= q == pr;
            boolean fields = pr.getId() == PROJ_SPOTANIM && src.equals(pr.getSourcePoint()) && dst.equals(pr.getTargetPoint())
                && pr.getEndCycle() == now + PROJ_CYCLES;
            String ev = describe(pr) + " listed " + listed + " fields " + fields;
            if (!listed || !fields) { p.set(row, "mismatch", ev); return; }
            p.later(row, moved(p, row, pr, ev));
        };
        SPECIAL.put("Client.createProjectile11", proj);
        SPECIAL.put("Client.createProjectile13", proj);
        SPECIAL.put("WorldView.createProjectile13", proj);
        // Clears every spot anim, then puts back the ones there were (frame progress is not kept).
        SPECIAL.put("Actor.clearSpotAnims0", (p, row, inst) -> {
            net.runelite.api.Actor a = (net.runelite.api.Actor) inst;
            List<int[]> had = new ArrayList<>();
            for (net.runelite.api.ActorSpotAnim s : a.getSpotAnims())
                had.add(new int[] {(int) s.getHash(), s.getId(), s.getHeight(), Math.max(0, s.getStartCycle() - p.client.getGameCycle())});
            a.createSpotAnim(PROBE_SLOT + 1, LEVELUP_SPOTANIM, 0, SPOTANIM_DELAY);
            int n = 0;
            for (Object s : a.getSpotAnims()) n++;
            a.clearSpotAnims();
            int left = 0;
            for (Object s : a.getSpotAnims()) left++;
            for (int[] s : had) a.createSpotAnim(s[0], s[1], s[2], s[3]);
            int back = 0;
            for (Object s : a.getSpotAnims()) back++;
            p.set(row, n == had.size() + 1 && left == 0 && back == had.size() ? "pass" : "mismatch", "spot anims " + n + " -> cleared " + left + " -> restored " + back);
        });
        // The deprecated slot-0 accessors: round trip on a slot-0 spot anim (a temporary one when there is none).
        Special slot0 = (p, row, inst) -> {
            net.runelite.api.Actor a = (net.runelite.api.Actor) inst;
            boolean temp = a.getSpotAnims().get(0) == null;
            if (temp) a.createSpotAnim(0, LEVELUP_SPOTANIM, 0, SPOTANIM_DELAY);
            try {
                p.roundTrip(row, inst, setter(row.m));
            } finally {
                if (temp) a.removeSpotAnim(0);
            }
            if (temp) row.evidence += " (temporary slot-0 spot anim " + LEVELUP_SPOTANIM + ")";
        };
        SPECIAL.put("Actor.setGraphicHeight1", slot0);
        SPECIAL.put("Actor.setSpotAnimFrame1", slot0);
        // The image must match the sprite's size (desktop throws otherwise); the live BufferedImage may be any size.
        SPECIAL.put("SpritePixels.toBufferedImage1", (p, row, inst) -> {
            net.runelite.api.SpritePixels s = (net.runelite.api.SpritePixels) inst;
            java.awt.image.BufferedImage img = new java.awt.image.BufferedImage(s.getWidth(), s.getHeight(), java.awt.image.BufferedImage.TYPE_INT_ARGB);
            s.toBufferedImage(img);
            p.set(row, "pass", "drawn into a " + s.getWidth() + "x" + s.getHeight() + " image");
        });
        // Rasterizer draws: on a 20x20 corner of its pixels, cleared first; read the pixels back; restore them.
        SPECIAL.put("Rasterizer.fillRectangle5", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.fillRectangle(1, 1, 4, 4, 0x123456);
            return new int[] {px[w + 1], px[4 * w + 4], px[5 * w + 5]};
        }, v -> v[0] == 0x123456 && v[1] == 0x123456 && v[2] == 0));
        SPECIAL.put("Rasterizer.rasterFlat7", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.rasterFlat(0, 0, 16, 0, 16, 0, 0x654321);
            return new int[] {px[2 * w + 2], px[15 * w + 15]};
        }, v -> v[0] == 0x654321 && v[1] == 0));
        SPECIAL.put("Rasterizer.rasterGouraud9", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.rasterGouraud(0, 0, 16, 0, 16, 0, 0x7f, 0x7f, 0x7f);  // white
            return new int[] {px[2 * w + 2], px[15 * w + 15]};
        }, v -> v[0] != 0 && v[1] == 0));
        SPECIAL.put("Rasterizer.setDrawRegion4", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.setDrawRegion(4, 4, 8, 8);
            r.fillRectangle(0, 0, 20, 20, 0xffffff);
            r.resetRasterClipping();
            return new int[] {px[5 * w + 5], px[2 * w + 2], px[10 * w + 10]};
        }, v -> v[0] == 0xffffff && v[1] == 0 && v[2] == 0));
        SPECIAL.put("Rasterizer.resetRasterClipping0", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.setDrawRegion(4, 4, 8, 8);
            r.resetRasterClipping();
            r.fillRectangle(0, 0, 20, 20, 0xffffff);
            return new int[] {px[2 * w + 2], px[10 * w + 10]};
        }, v -> v[0] == 0xffffff && v[1] == 0xffffff));
        // The padding the game's health bar draw reads (the host config's padding(): the game type's field).
        SPECIAL.put("HealthBarConfig.setPadding1", (p, row, inst) -> {
            Method get = inst.getClass().getDeclaredMethod("padding");
            get.setAccessible(true);
            net.runelite.api.HealthBarConfig c = (net.runelite.api.HealthBarConfig) inst;
            int old = (Integer) get.invoke(inst);
            if (old < 0) { p.set(row, "mismatch", "no actor shows this health bar type now"); return; }
            c.setPadding(old + 1);
            int read = (Integer) get.invoke(inst);
            c.setPadding(old);
            int back = (Integer) get.invoke(inst);
            p.set(row, read == old + 1 && back == old ? "pass" : "mismatch", "game type padding " + old + " -> set " + (old + 1) + " -> " + read + " -> restored " + back);
        });
        // Text into the Rasterizer's pixels: white "Wg" in the cleared corner lights some of its pixels.
        SPECIAL.put("FontTypeFace.drawWidgetText11", (p, row, inst) -> raster(p, row, p.client.getRasterizer(), (r, px, w) -> {
            ((net.runelite.api.FontTypeFace) inst).drawWidgetText("Wg", 0, 0, 20, 20, 0xffffff, -1, 256, 0, 0, 0);
            int[] now = r.getPixels();
            int lit = 0;
            for (int y = 0; y < 20; y++) for (int x = 0; x < 20; x++) if ((now[y * w + x] & 0xffffff) == 0xffffff) lit++;
            return new int[] {lit};
        }, v -> v[0] > 0));
        // Low res: one colour per 8-pixel run of a gouraud gradient; full res: the colour changes inside a run.
        SPECIAL.put("Rasterizer.setRasterGouraudLowRes1", (p, row, inst) -> raster(p, row, (Rasterizer) inst, (r, px, w) -> {
            r.setRasterGouraudLowRes(true);
            r.rasterGouraud(0, 0, 19, 0, 19, 0, 0x0000, 0x007f, 0x0000);
            int lowRunEqual = px[w + 8] == px[w + 15] ? 1 : 0;
            r.setRasterGouraudLowRes(false);
            r.rasterGouraud(0, 0, 19, 0, 19, 0, 0x0000, 0x007f, 0x0000);
            int fullRunDiffers = px[w + 8] != px[w + 15] ? 1 : 0;
            return new int[] {lowRunEqual, fullRunDiffers};
        }, v -> v[0] == 1 && v[1] == 1));
    }

    // Caches: the entry count (the host cache's size()) before and after reset, which runs the game's clear.
    static String resetCache(Object cache, Runnable reset) throws Exception {
        Method size = cache.getClass().getMethod("size");
        size.setAccessible(true);
        Object before = size.invoke(cache);
        reset.run();
        return "entries " + before + " -> " + size.invoke(cache);
    }

    static {
        SPECIAL.put("NodeCache.reset0", (p, row, inst) -> {
            net.runelite.api.NodeCache c = (net.runelite.api.NodeCache) inst;
            try {
                c.getClass().getMethod("size");
            } catch (NoSuchMethodException e) {
                c.reset();  // a cache with nothing host-side to count
                p.set(row, "pass", "returned");
                return;
            }
            String ev = describe(c) + ": " + resetCache(c, c::reset);
            p.set(row, ev.endsWith("-> 0") ? "pass" : "mismatch", ev);
        });
        SPECIAL.put("Client.resetHealthBarCaches0", (p, row, inst) -> {
            Client c = (Client) inst;
            Object hb = null;
            try {
                java.lang.reflect.Field f = Class.forName("net.runelite.host.CacheConfig", false, Client.class.getClassLoader()).getDeclaredField("CACHES");
                f.setAccessible(true);
                hb = Array.get(f.get(null), 5);
            } catch (ReflectiveOperationException e) {
                c.resetHealthBarCaches();
                p.set(row, "pass", "returned (no host cache to count)");
                return;
            }
            String ev = "health bar types + sprites " + resetCache(hb, c::resetHealthBarCaches);
            p.set(row, ev.endsWith("-> 0") ? "pass" : "mismatch", ev);
        });
        // Index reads on a group the live index has: the first group with files, its first file's bytes.
        SPECIAL.put("IndexDataBase.getFileIds1", (p, row, inst) -> {
            int[] g = firstFile((net.runelite.api.IndexDataBase) inst);
            int[] ids = ((net.runelite.api.IndexDataBase) inst).getFileIds(g == null ? 0 : g[0]);
            p.set(row, ids != null && ids.length > 0 ? "pass" : "mismatch", describe(inst) + " group " + (g == null ? 0 : g[0]) + ": " + describe(ids)
                + (ids != null && ids.length > 0 ? " first " + ids[0] + " last " + ids[ids.length - 1] : ""));
        });
        SPECIAL.put("IndexDataBase.loadData2", (p, row, inst) -> {
            net.runelite.api.IndexDataBase idx = (net.runelite.api.IndexDataBase) inst;
            int[] g = firstFile(idx);
            byte[] b = g == null ? null : idx.loadData(g[0], g[1]);
            StringBuilder hex = new StringBuilder();
            for (int i = 0; b != null && i < Math.min(8, b.length); i++) hex.append(String.format("%02x", b[i] & 0xff));
            p.set(row, b != null && b.length > 0 ? "pass" : "mismatch", describe(inst) + (g == null ? " no group with files" : " group " + g[0] + " file " + g[1] + ": " + describe(b) + " " + hex));
        });
    }

    static final int VARP = 1000, VARBIT = 8119, SOUND = 2266;
    static final String[] LISTENER_KINDS = {"TIMER", "DRAG", "VAR_TRANSMIT", "STORED", "TARGET_ENTER", "TARGET_LEAVE"};  // WidgetListeners kinds

    // Setters and hooks without a getter: each effect read where it lands (an event, a game field, host state).
    static {
        // On a copy of the varps, so the game's are untouched: getVarbitValue(int[], int) reads it back.
        SPECIAL.put("Client.setVarbitValue3", (p, row, inst) -> {
            Client c = (Client) inst;
            int[] v = c.getVarps().clone();
            int old = c.getVarbitValue(v, VARBIT), alt = old == 0 ? 1 : 0;
            c.setVarbitValue(v, VARBIT, alt);
            int read = c.getVarbitValue(v, VARBIT);
            c.setVarbitValue(v, VARBIT, old);
            int back = c.getVarbitValue(v, VARBIT), game = c.getVarbitValue(VARBIT);
            p.set(row, read == alt && back == old && game == old ? "pass" : "mismatch", "varbit " + VARBIT + " in a copy of getVarps(): " + old + " -> set " + alt + " -> "
                + read + " -> restored " + back + "; the game's still " + game);
        });
        // Publishes an edit of the array getVarps() returned: the game's varp then reads the edited value.
        SPECIAL.put("Client.queueChangedVarp1", (p, row, inst) -> {
            Client c = (Client) inst;
            int[] v = c.getVarps();
            int old = v[VARP], alt = (Integer) alter(old, int.class);
            v[VARP] = alt;
            c.queueChangedVarp(VARP);
            int read = c.getVarpValue(VARP);
            v = c.getVarps();
            v[VARP] = old;
            c.queueChangedVarp(VARP);
            int back = c.getVarpValue(VARP);
            p.set(row, read == alt && back == old ? "pass" : "mismatch", "varp " + VARP + " " + old + ", array edited to " + alt + " and queued: getVarpValue " + read + " -> restored " + back);
        });
        // The game's changed-skills ring count (what the skill tab's onStatTransmit scripts run on).
        SPECIAL.put("Client.queueChangedSkill1", (p, row, inst) -> {
            int before = clientInt(CHANGED_SKILL_COUNT);
            ((Client) inst).queueChangedSkill(net.runelite.api.Skill.ATTACK);
            int after = clientInt(CHANGED_SKILL_COUNT);
            p.set(row, after == before + 1 ? "pass" : "mismatch", "ATTACK: changed-skills count " + before + " -> " + after);
        });
        // The chat cycle the chatbox rebuilds on: set back one, then refreshChat sets it again.
        SPECIAL.put("Client.refreshChat0", (p, row, inst) -> {
            int before = clientInt(CHAT_CYCLE);
            host("Native", "setClientInt", new Class<?>[] {int.class, int.class}, CHAT_CYCLE, before - 1);
            ((Client) inst).refreshChat();
            int after = clientInt(CHAT_CYCLE);
            p.set(row, after != before - 1 ? "pass" : "mismatch", "chat cycle " + before + " -> set " + (before - 1) + " -> refreshChat " + after + " (game cycle " + ((Client) inst).getGameCycle() + ")");
        });
        // The GE search results OC_FINDNEXT reads: the game's fields (Native.geSearch {count, index, ids...}), restored.
        Special ge = (p, row, inst) -> {
            Client c = (Client) inst;
            int[] old = geSearch();
            if (old == null) { p.set(row, "mismatch", "GE search fields unreadable"); return; }
            short[] ids = new short[old.length - 2];
            for (int i = 0; i < ids.length; i++) ids[i] = (short) old[i + 2];
            int[] want = old.clone();
            if (row.name.equals("setGeSearchResultIds")) { c.setGeSearchResultIds(new short[] {995, 1511}); want = new int[] {old[0], old[1], 995, 1511}; }
            else if (row.name.equals("setGeSearchResultCount")) { c.setGeSearchResultCount(old[0] + 1); want[0]++; }
            else { c.setGeSearchResultIndex(old[1] + 1); want[1]++; }
            int[] read = geSearch();
            c.setGeSearchResultIds(ids);
            c.setGeSearchResultCount(old[0]);
            c.setGeSearchResultIndex(old[1]);
            int[] back = geSearch();
            p.set(row, Arrays.equals(read, want) && Arrays.equals(back, old) ? "pass" : "mismatch",
                "{count, index, ids} " + Arrays.toString(old) + " -> " + Arrays.toString(read) + " -> restored " + Arrays.toString(back));
        };
        SPECIAL.put("Client.setGeSearchResultIds1", ge);
        SPECIAL.put("Client.setGeSearchResultCount1", ge);
        SPECIAL.put("Client.setGeSearchResultIndex1", ge);
        // Desktop's inventory drag delay: on mobile the inventory items' drag dead time (Widget +0x324).
        SPECIAL.put("Client.setInventoryDragDelay1", (p, row, inst) -> {
            Client c = (Client) inst;
            Widget inv = c.getWidget(149, 0);
            Widget[] items = inv == null ? null : inv.getDynamicChildren();
            if (items == null || items.length == 0) { p.set(row, "mismatch", "no inventory item widget (149:0) to read the drag dead time of"); return; }
            int old = items[0].getDragDeadTime();
            c.setInventoryDragDelay(old + 10);
            int read = items[0].getDragDeadTime();
            c.setInventoryDragDelay(old);
            p.set(row, read == old + 10 ? "pass" : "mismatch", "149:0[0] drag dead time " + old + " -> setInventoryDragDelay(" + (old + 10) + ") -> " + read);
        });
        // Camera: each setter's game field (Native.clientInt), set, read, restored.
        SPECIAL.put("Client.setCameraPitchRelaxerEnabled1", (p, row, inst) -> {
            int old = clientInt(PITCH_RELAXER);
            ((Client) inst).setCameraPitchRelaxerEnabled(old == 0);
            int read = clientInt(PITCH_RELAXER);
            ((Client) inst).setCameraPitchRelaxerEnabled(old != 0);
            int back = clientInt(PITCH_RELAXER);
            p.set(row, read == (old == 0 ? 1 : 0) && back == old ? "pass" : "mismatch", "pitch relaxer (+0x8951c) " + old + " -> " + read + " -> restored " + back);
        });
        Special orbSpeed = (p, row, inst) -> {
            int old = clientInt(ORB_SPEED);
            row.m.invoke(inst, old + 1);
            int read = clientInt(ORB_SPEED);
            row.m.invoke(inst, old);
            int back = clientInt(ORB_SPEED);
            p.set(row, read == old + 1 && back == old ? "pass" : "mismatch", "orb/free camera speed (+0x894c4) " + old + " -> set " + (old + 1) + " -> " + read + " -> restored " + back);
        };
        SPECIAL.put("Client.setOculusOrbNormalSpeed1", orbSpeed);
        SPECIAL.put("Client.setFreeCameraSpeed1", orbSpeed);
        // The touch camera's drag sensitivity (settings +0x82c): the game's own times the multiplier.
        SPECIAL.put("Client.setCameraSpeed1", (p, row, inst) -> {
            float old = (Float) host("Native", "cameraSensitivity", new Class<?>[0]);
            ((Client) inst).setCameraSpeed(2f);
            float read = (Float) host("Native", "cameraSensitivity", new Class<?>[0]);
            ((Client) inst).setCameraSpeed(1f);
            float back = (Float) host("Native", "cameraSensitivity", new Class<?>[0]);
            p.set(row, old != 0 && read == old * 2 && back == old ? "pass" : "mismatch", "drag sensitivity " + old + " -> setCameraSpeed(2) -> " + read + " -> setCameraSpeed(1) -> " + back);
        });
        // Sounds: the SoundEffectPlayed / AreaSoundEffectPlayed the game's queue raises, read on the next tick.
        // Each overload uses its own id, so their events don't mix.
        Special sound = (p, row, inst) -> {
            Client c = (Client) inst;
            int n = row.m.getParameterCount(), id = SOUND + n;
            LocalPoint lp = c.getLocalPlayer().getLocalLocation();
            int x = lp.getSceneX(), y = lp.getSceneY();
            if (n == 1) c.playSoundEffect(id);
            else if (n == 2) c.playSoundEffect(id, 32);
            else if (n == 4) c.playSoundEffect(id, x, y, 1);
            else c.playSoundEffect(id, x, y, 1, 5);
            p.later(row, () -> {
                net.runelite.api.events.SoundEffectPlayed s = null;
                net.runelite.api.events.AreaSoundEffectPlayed a = null;
                for (net.runelite.api.events.SoundEffectPlayed e : p.sounds) if (e.getSoundId() == id) s = e;
                for (net.runelite.api.events.AreaSoundEffectPlayed e : p.areaSounds) if (e.getSoundId() == id) a = e;
                String ev = "sound " + id + ": SoundEffectPlayed " + (s == null ? "none" : "delay " + s.getDelay())
                    + ", AreaSoundEffectPlayed " + (a == null ? "none" : "at " + a.getSceneX() + "," + a.getSceneY() + " range " + a.getRange() + " delay " + a.getDelay());
                boolean ok;
                if (n == 1) ok = s != null && s.getDelay() == 0;
                else if (n == 2) { ok = false; ev += "; volume 32 is not applied (desktop plays it at that volume)"; }
                else ok = a != null && a.getSceneX() == x && a.getSceneY() == y && a.getRange() == 1 && a.getDelay() == (n == 5 ? 5 : 0);
                if (n >= 4) ev += "; asked at " + x + "," + y + " range 1";
                p.set(row, ok ? "pass" : "mismatch", ev);
            });
        };
        SPECIAL.put("Client.playSoundEffect1", sound);
        SPECIAL.put("Client.playSoundEffect2", sound);
        SPECIAL.put("Client.playSoundEffect4", sound);
        SPECIAL.put("Client.playSoundEffect5", sound);
        // The probe's own chat line: once the game has it (next tick), removed; the buffer's line count drops.
        SPECIAL.put("ChatLineBuffer.removeMessageNode1", (p, row, inst) -> {
            net.runelite.api.MessageNode node = p.client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", "api-probe: removeMessageNode", null);
            int type = ChatMessageType.GAMEMESSAGE.getType();
            p.later(row, () -> {
                net.runelite.api.ChatLineBuffer b = p.client.getChatLineMap().get(type);
                int n1 = b == null ? 0 : b.getLength();
                boolean had = b != null && Arrays.asList(b.getLines()).contains(node);
                if (b != null) b.removeMessageNode(node);
                net.runelite.api.ChatLineBuffer a = p.client.getChatLineMap().get(type);
                int n2 = a == null ? 0 : a.getLength();
                boolean has = a != null && Arrays.asList(a.getLines()).contains(node);
                p.set(row, had && !has && n2 == n1 - 1 ? "pass" : "mismatch", "probe line id " + node.getId() + ": " + type + " buffer " + n1 + " lines (line in it " + had + ") -> removed: " + n2 + " (in it " + has + ")");
            });
        });
        // The game's ambient sounds, cleared and re-added in the same cycle (as Ambient Sound plugins do).
        Special deque = (p, row, inst) -> {
            net.runelite.api.Deque<net.runelite.api.AmbientSoundEffect> d = p.client.getAmbientSoundEffects();
            List<net.runelite.api.AmbientSoundEffect> had = new ArrayList<>();
            for (net.runelite.api.AmbientSoundEffect e : d) had.add(e);
            if (had.isEmpty()) { p.set(row, "mismatch", "no ambient sound in the scene to clear and re-add"); return; }
            d.clear();
            int left = 0;
            for (Object e : d) left++;
            for (net.runelite.api.AmbientSoundEffect e : had) d.addLast(e);
            int back = 0;
            for (Object e : d) back++;
            p.set(row, left == 0 && back == had.size() ? "pass" : "mismatch", "ambient sounds " + had.size() + " -> clear " + left + " -> addLast each " + back);
        };
        SPECIAL.put("Deque.clear0", deque);
        SPECIAL.put("Deque.addLast1", deque);
        // A probe-owned slot of the local player's spot anim table: put a copy of the probe's slot-7 spot anim.
        SPECIAL.put("IterableHashTable.put2", (p, row, inst) -> {
            net.runelite.api.Actor a = p.client.getLocalPlayer();
            net.runelite.api.ActorSpotAnim src = a.getSpotAnims().get(PROBE_SLOT);
            if (src == null) { p.set(row, "mismatch", "no probe spot anim in slot " + PROBE_SLOT + " to put"); return; }
            int slot = PROBE_SLOT + 2;
            Object before = a.getSpotAnims().get(slot);
            a.getSpotAnims().put(src, slot);
            p.later(row, () -> {
                net.runelite.api.ActorSpotAnim s = a.getSpotAnims().get(slot);
                a.removeSpotAnim(slot);
                p.set(row, before == null && s != null && s.getId() == src.getId() ? "pass" : "mismatch", "local player's spot anims slot " + slot + ": " + describe(before) + " -> put " + describe(src) + " -> " + describe(s) + " (removed again)");
            });
        });
        // The world map's display position: the game moves it to the target on its next cycle while the map is open.
        SPECIAL.put("WorldMap.setWorldMapPositionTarget1", (p, row, inst) -> {
            net.runelite.api.worldmap.WorldMap m = (net.runelite.api.worldmap.WorldMap) inst;
            net.runelite.api.Point old = m.getWorldMapPosition();
            if (old == null) { p.set(row, "mismatch", "no world map position (the map was not opened since the app started)"); return; }
            int plane = p.client.getTopLevelWorldView().getPlane();
            m.setWorldMapPositionTarget(new WorldPoint(old.getX() + 8, old.getY() + 8, plane));
            p.later(row, () -> {
                net.runelite.api.Point read = m.getWorldMapPosition();
                m.setWorldMapPositionTarget(new WorldPoint(old.getX(), old.getY(), plane));
                p.later(row, () -> {
                    net.runelite.api.Point back = m.getWorldMapPosition();
                    boolean ok = read != null && read.getX() == old.getX() + 8 && read.getY() == old.getY() + 8 && old.equals(back);
                    p.set(row, ok ? "pass" : "mismatch", "position " + old + " -> target +8,+8 -> next tick " + read + " -> restored " + back
                        + (ok ? "" : " (the game moves it only while the world map is open)"));
                });
            });
        });
        // Sub-ops: set, read back with getSubOps, restore (the host's entry removed when there was none).
        SPECIAL.put("Widget.setSubOp3", (p, row, inst) -> {
            Widget w = (Widget) inst;
            String[][] old = w.getSubOps();
            String prev = old != null && old[0] != null && old[0].length > 0 ? old[0][0] : null;
            w.setSubOp(0, 0, "api-probe");
            String[][] r = w.getSubOps();
            String read = r != null && r[0] != null && r[0].length > 0 ? r[0][0] : null;
            if (old == null) {
                @SuppressWarnings("unchecked") Map<Long, String[][]> t = (Map<Long, String[][]>) hostField("AndroidWidget", "SUB_OPS");
                synchronized (t) { t.remove(((long) w.getId() << 32) | (w.getIndex() & 0xffffffffL)); }
            } else {
                w.setSubOp(0, 0, prev);
            }
            p.set(row, "api-probe".equals(read) && (old == null ? w.getSubOps() == null : same(prev, w.getSubOps()[0][0])) ? "pass" : "mismatch",
                "sub-op 0/0 " + describe(prev) + " -> " + describe(read) + " -> restored " + (w.getSubOps() == null ? "none" : describe(w.getSubOps()[0][0])));
        });
        // Layout: a probe-created child moved by setOriginalX lands at its new relative x only after the call.
        Special reval = (p, row, inst) -> {
            Widget parent = p.listenerParent((Widget) inst);
            Widget c = parent.createChild(-1, 3);
            c.setOriginalWidth(10);
            c.setOriginalHeight(10);
            row.m.invoke(c);
            int x0 = c.getRelativeX();
            c.setOriginalX(c.getOriginalX() + 7);
            int x1 = c.getRelativeX();
            row.m.invoke(c);
            int x2 = c.getRelativeX();
            parent.deleteAllChildren();
            p.set(row, x1 == x0 && x2 == x0 + 7 ? "pass" : "mismatch", "child of " + wid(parent) + ": relative x " + x0 + ", original x +7 -> " + x1 + " -> " + row.name + " " + x2 + " (child deleted)");
        };
        SPECIAL.put("Widget.revalidate0", reval);
        SPECIAL.put("Widget.revalidateScroll0", reval);
        // Java listeners on a probe-created child: where the host keeps each, and whether its event runs it.
        Special listener = (p, row, inst) -> {
            Widget parent = p.listenerParent((Widget) inst);
            Widget c = parent.createChild(-1, 3);
            int[] runs = {0};
            net.runelite.api.widgets.JavaScriptCallback cb = ev -> runs[0]++;
            row.m.invoke(c, (Object) new Object[] {cb});
            int id = c.getId(), idx = c.getIndex();
            long key = (long) id << 32 | (idx & 0xffffffffL);
            String kind = null;
            @SuppressWarnings("unchecked") Map<Integer, Map<Long, Object[]>> all = (Map<Integer, Map<Long, Object[]>>) hostField("WidgetListeners", "javaListeners");
            for (Map.Entry<Integer, Map<Long, Object[]>> e : all.entrySet())
                if (e.getValue().containsKey(key)) kind = LISTENER_KINDS[e.getKey()];
            if (kind == null && ((Map<?, ?>) hostField("WidgetListeners", "dragComplete")).containsKey(key)) kind = "DRAG_COMPLETE";
            String where = "child " + wid(parent) + "[" + idx + "]: " + (kind == null ? "not stored" : "stored as " + kind);
            Check clear = () -> {
                row.m.invoke(c, (Object) null);
                parent.deleteAllChildren();
            };
            Class<?> cl = Client.class;
            if ("TIMER".equals(kind)) {
                p.later(row, () -> {
                    int n = runs[0];
                    boolean hidden = c.isHidden();
                    clear.run();
                    p.set(row, n > 0 ? "pass" : "mismatch", where + "; ran " + n + " times by the next tick" + (hidden ? " (hidden)" : ""));
                });
                return;
            }
            if ("DRAG".equals(kind)) host("WidgetListeners", "runDrag", new Class<?>[] {cl, int.class, int.class}, p.client, id, idx);
            else if ("DRAG_COMPLETE".equals(kind)) host("WidgetListeners", "runDragComplete", new Class<?>[] {cl, int.class, int.class, int.class, int.class}, p.client, id, idx, -1, -1);
            else if ("TARGET_ENTER".equals(kind) || "TARGET_LEAVE".equals(kind))
                host("WidgetListeners", "runFor", new Class<?>[] {cl, int.class, int.class, int.class}, p.client, Arrays.asList(LISTENER_KINDS).indexOf(kind), id, idx);
            int n = runs[0];
            clear.run();
            if ("STORED".equals(kind)) p.set(row, "inert", where + " (kept, never run: no host event runs a STORED listener)");
            else p.set(row, kind != null && n > 0 ? "pass" : "mismatch", where + "; the host's " + kind + " dispatch ran it " + n + " times");
        };
        for (String l : new String[] {"Click", "DialogAbort", "DragComplete", "Drag", "Hold", "MouseLeave", "MouseOver", "MouseRepeat", "Release",
            "ScrollWheel", "TargetEnter", "TargetLeave", "Timer"})
            SPECIAL.put("Widget.setOn" + l + "Listener1", listener);
    }

    // Session and server-visible methods. A client script that only reads (quest_status_get), a world that is the
    // current one, and the LAST rows, which each undo or settle what they do (see LAST).
    static final int QUEST = net.runelite.api.Quest.COOKS_ASSISTANT.getId();

    static {
        Special script = (p, row, inst) -> {
            Client c = p.client;
            c.setIntStackSize(0);
            String how;
            if (row.name.equals("runScript")) {
                c.runScript(net.runelite.api.ScriptID.QUEST_STATUS_GET, QUEST);
                how = "runScript";
            } else {
                c.createScriptEventBuilder(net.runelite.api.ScriptID.QUEST_STATUS_GET, QUEST).build().run();
                how = "a ScriptEvent's run";
            }
            int n = c.getIntStackSize(), v = n > 0 ? c.getIntStack()[0] : -1;
            p.set(row, n == 1 && v >= 0 && v <= 2 ? "pass" : "mismatch", how + " of script " + net.runelite.api.ScriptID.QUEST_STATUS_GET
                + " (quest_status_get, reads varps only) for quest " + QUEST + ": int stack size 0 -> " + n + ", value " + v + " (0 not started, 1 started, 2 done)");
        };
        // Ends the client (desktop's stopNow exits RuneLite): the probe would never write its report.
        SPECIAL.put("Client.stopNow0", (p, row, inst) -> p.set(row, "skipped", "ends the client; the report would never be written"));
        SPECIAL.put("Client.runScript1", script);
        SPECIAL.put("ScriptEvent.run0", script);
        // Desktop sets the world the next login uses: no getter, so the client's fields are compared around it.
        SPECIAL.put("Client.changeWorld1", (p, row, inst) -> {
            int before = p.client.getWorld();
            p.noGetter(row, inst);
            row.evidence += "; world " + before + ", asked for world " + before + ", getWorld after " + p.client.getWorld();
        });
        // A modal interface the run set-up opened (probewait "modal"), closed client-side as desktop does; the server
        // still has it open until the next row's walk closes it there (its move handler closes modals).
        SPECIAL.put("Client.closeInterface2", (p, row, inst) -> {
            Client c = (Client) inst;
            List<net.runelite.api.WidgetNode> modals = new ArrayList<>();  // a modal and its side panel (equipment stats: 84 and 85)
            for (net.runelite.api.WidgetNode n : c.getComponentTable()) if (n.getModalMode() == 0) modals.add(n);
            if (modals.isEmpty()) {
                if (!probewait().contains("modal")) { p.set(row, "skipped", "no modal interface open (name 'modal' in debug.osrsrl.probewait and open one, e.g. equipment stats, while it waits)"); return; }
                if (row.evidence.isEmpty()) log.info("api-probe: closeInterface waits for a modal interface (open one, e.g. equipment stats)");
                row.evidence = "waiting";
                p.later(row, () -> SPECIAL.get("Client.closeInterface2").run(p, row, inst));
                return;
            }
            boolean ok = true;
            StringBuilder ev = new StringBuilder();
            for (net.runelite.api.WidgetNode modal : modals) {
                int group = modal.getId(), comp = (int) modal.getHash();
                c.closeInterface(modal, true);
                boolean listed = false;
                for (net.runelite.api.WidgetNode n : c.getComponentTable()) listed |= n.getHash() == comp && n.getId() == group;
                Widget root = c.getWidget(group, 0);
                ok &= !listed && (root == null || root.isHidden());
                ev.append(ev.length() == 0 ? "" : "; ").append("modal group ").append(group).append(" in ").append(wid(comp)).append(" closed: in the component table ")
                    .append(listed).append(", widget ").append(group).append(":0 ").append(root == null ? "gone" : "hidden " + root.isHidden());
            }
            p.set(row, ok ? "pass" : "mismatch", ev.toString());
        });
        // A walk onto the local player's own tile through the game's menu (the server closes its modals and stops the
        // fight). Done once the game ran the click (MenuOptionClicked) or the host dropped it.
        // Invert: a drag on the scene (the canvas's mouse, as a touch) turns the camera; the same drag again with the
        // axis inverted must turn that axis the other way.
        Special invert = (p, row, inst) -> {
            boolean pitch = row.m.getName().equals("setInvertPitch");
            p.cameraDrag(row, (yaw0, pitch0) -> {
                row.m.invoke(inst, true);
                p.cameraDrag(row, (yaw1, pitch1) -> {
                    row.m.invoke(inst, false);
                    int base = pitch ? pitch0 : yaw0, inverted = pitch ? pitch1 : yaw1;
                    p.set(row, base != 0 && Integer.signum(inverted) == -Integer.signum(base) ? "pass" : "mismatch",
                        (pitch ? "pitch" : "yaw") + " target moved by a scene drag " + base + ", by the same drag with " + row.m.getName() + "(true) " + inverted + " (then false)");
                });
            });
        };
        SPECIAL.put("Client.setInvertPitch1", invert);
        SPECIAL.put("Client.setInvertYaw1", invert);
        SPECIAL.put("Client.menuAction7", (p, row, inst) -> {
            Client c = (Client) inst;
            Player me = c.getLocalPlayer();
            LocalPoint lp = me.getLocalLocation();
            WorldPoint at = me.getWorldLocation();
            c.menuAction(lp.getSceneX(), lp.getSceneY(), net.runelite.api.MenuAction.WALK, 0, -1, "Walk here", "");
            String ev = "WALK at scene " + lp.getSceneX() + "," + lp.getSceneY() + " (the player's tile " + at + ")";
            p.later(row, walked(p, row, ev, at, lp.getSceneX(), lp.getSceneY()));
        });
        // A scene reload (the only state the host honours; desktop's too): a ground object removed first comes back
        // with the rebuilt scene.
        SPECIAL.put("Client.setGameState1", (p, row, inst) -> {
            Client c = (Client) inst;
            Tile t = groundTile(c);
            if (t == null) { p.set(row, "mismatch", "no ground object in the scene to remove and see re-added by the reload"); return; }
            int x = t.getSceneLocation().getX(), y = t.getSceneLocation().getY(), z = t.getPlane();
            long hash = t.getGroundObject().getHash();
            int id = t.getGroundObject().getId();
            t.setGroundObject(null);
            boolean removed = t.getGroundObject() == null;
            c.setGameState(GameState.LOADING);
            String ev = "ground object " + id + " at scene " + x + "," + y + "," + z + " removed " + removed + "; setGameState(LOADING), state " + c.getGameState();
            p.later(row, () -> {
                net.runelite.api.GroundObject g = c.getTopLevelWorldView().getScene().getTiles()[z][x][y].getGroundObject();
                p.set(row, removed && g != null && g.getId() == id ? "pass" : "mismatch", ev + "; next tick: " + (g == null ? "none" : "ground object " + g.getId()
                    + (g.getHash() == hash ? " (same hash)" : "")) + ", state " + c.getGameState());
            });
        });
    }

    /** The walk's check: the click the game ran, the destination and where the player is; re-checked while the host holds it. */
    static Check walked(ApiProbePlugin p, Row row, String ev, WorldPoint at, int sx, int sy) {
        return () -> {
            net.runelite.api.MenuEntry walk = null;
            for (net.runelite.api.MenuEntry e : p.clicks) if (e.getType() == net.runelite.api.MenuAction.WALK) walk = e;
            if (walk == null && hostField("DevMenu", "pending") != null) { p.later(row, walked(p, row, ev, at, sx, sy)); return; }
            // In a fight the player may be a step away by now: the walk is right when it heads for (or is on) the asked tile.
            WorldPoint now = p.client.getLocalPlayer().getWorldLocation();
            LocalPoint dest = p.client.getLocalDestinationLocation();
            WorldPoint to = dest == null ? now : WorldPoint.fromLocal(p.client, dest);
            boolean clicked = walk != null && walk.getParam0() == sx && walk.getParam1() == sy;
            p.set(row, clicked && at.equals(to) ? "pass" : "mismatch", ev + ": " + (walk == null ? "no WALK click (the host dropped it: logcat menuAction)"
                : "MenuOptionClicked " + walk.getOption() + " " + walk.getType() + " " + walk.getParam0() + "," + walk.getParam1()) + ", destination "
                + (dest == null ? "none" : to.toString()) + ", player at " + now);
        };
    }

    /** A tile with a ground object, not the local player's. */
    static Tile groundTile(Client c) {
        LocalPoint lp = c.getLocalPlayer().getLocalLocation();
        for (Tile[] col : c.getTopLevelWorldView().getScene().getTiles()[c.getTopLevelWorldView().getPlane()])
            for (Tile t : col)
                if (t != null && t.getGroundObject() != null && !(t.getSceneLocation().getX() == lp.getSceneX() && t.getSceneLocation().getY() == lp.getSceneY())) return t;
        return null;
    }

    static String wid(int id) {
        return (id >>> 16) + ":" + (id & 0xffff);
    }

    static int[] geSearch() throws Exception {
        return (int[]) host("Native", "geSearch", new Class<?>[0]);
    }

    static String wid(Widget w) {
        return (w.getId() >>> 16) + ":" + (w.getId() & 0xffff);
    }

    /** A visible root other than the probe's widget, with no dynamic children: the children the listener and layout rows create go there. */
    Widget listenerParent(Widget seeded) {
        Widget[] roots = client.getWidgetRoots();
        if (roots != null)
            for (Widget w : roots)
                if (w != null && w != seeded && !w.isHidden() && (w.getDynamicChildren() == null || w.getDynamicChildren().length == 0)) return w;
        return seeded;
    }

    /** {group, file} of the first group of `idx` with a file, or null. */
    static int[] firstFile(net.runelite.api.IndexDataBase idx) {
        for (int g = 0; g < 4096; g++) {
            int[] ids = idx.getFileIds(g);
            if (ids != null && ids.length > 0) return new int[] {g, ids[0]};
        }
        return null;
    }

    interface Draw {
        int[] run(Rasterizer r, int[] px, int w);
    }

    private static void raster(ApiProbePlugin p, Row row, Rasterizer r, Draw draw, java.util.function.Predicate<int[]> ok) {
        int[] px = r.getPixels();
        int w = r.getWidth();
        if (w < 20 || r.getHeight() < 20) { p.set(row, "mismatch", "pixels " + w + "x" + r.getHeight() + " (smaller than 20x20)"); return; }
        int[] saved = px.clone();
        r.resetRasterClipping();
        r.fillRectangle(0, 0, 20, 20, 0);
        int[] v = draw.run(r, px, w);
        System.arraycopy(saved, 0, px, 0, px.length);
        String hex = Arrays.stream(v).mapToObj(Integer::toHexString).collect(java.util.stream.Collectors.joining(","));
        p.set(row, ok.test(v) ? "pass" : "mismatch", "drawn into " + w + "x" + r.getHeight() + " pixels; read back " + hex + " (restored)");
    }

    private final List<Runnable> cleanup = new ArrayList<>();

    // ---- arguments ----

    static final class Missing extends Exception {
        Missing(String type) {
            super(type);
        }
    }

    /** Arguments that reach real data where 0 would not. */
    private Object[] args(Row row) throws Missing {
        String key = simple(row.iface) + "." + row.name + row.desc;
        switch (key) {
            case "Client.getItemContainer(I)Lnet/runelite/api/ItemContainer;": return new Object[] {93};
            case "Client.getItemDefinition(I)Lnet/runelite/api/ItemComposition;": return new Object[] {995};
            case "Client.createItemSprite(IIIIIZI)Lnet/runelite/api/SpritePixels;": return new Object[] {995, 1, 1, 3153952, 0, false, 512};
            case "Client.getWidget(I)Lnet/runelite/api/widgets/Widget;": return new Object[] {widgetId()};
            case "Client.getWidget(II)Lnet/runelite/api/widgets/Widget;": return new Object[] {widgetId() >>> 16, widgetId() & 0xffff};
            case "Client.getVarbitValue(I)I": return new Object[] {8119};
            case "Client.getVarpValue(I)I": return new Object[] {1000};
            // A model the cache has (the live NPC's) and that NPC's idle sequence, so the rows read real geometry.
            case "Client.loadModel(I)Lnet/runelite/api/Model;": return new Object[] {npcModel()};
            case "Client.loadModel(I[S[S)Lnet/runelite/api/Model;": return new Object[] {npcModel(), new short[0], new short[0]};
            case "Client.loadModelData(I)Lnet/runelite/api/ModelData;": return new Object[] {npcModel()};
            case "Client.applyTransformations(Lnet/runelite/api/Model;Lnet/runelite/api/Animation;ILnet/runelite/api/Animation;I)Lnet/runelite/api/Model;": {
                Object npc = live.get(net.runelite.api.NPC.class);
                int seq = npc != null ? ((net.runelite.api.NPC) npc).getIdlePoseAnimation() : -1;
                return new Object[] {arg(net.runelite.api.Model.class), client.loadAnimation(seq), 0, null, 0};
            }
            // The config archive (2), so the IndexDataBase rows read config files; a DB row the cache has.
            case "Client.getIndex(I)Lnet/runelite/api/IndexDataBase;": return new Object[] {2};
            case "Client.getDBRowConfig(I)Lnet/runelite/api/dbtable/DBRowConfig;": {
                for (int r = 0; r < 20000; r++) if (client.getDBRowConfig(r) != null) return new Object[] {r};
                return new Object[] {0};
            }
            // The world the client is on (desktop would hop to it or log in to it next).
            case "Client.changeWorld(Lnet/runelite/api/World;)V":
            case "Client.hopToWorld(Lnet/runelite/api/World;)V": {
                net.runelite.api.World w = client.createWorld();
                w.setId(client.getWorld());
                return new Object[] {w};
            }
            // scale 0 would flatten the live model the Model rows read next; 128 = 1:1
            case "Mesh.scale(III)Lnet/runelite/api/Mesh;": return new Object[] {128, 128, 128};
            default: break;
        }
        Class<?>[] ps = row.m.getParameterTypes();
        Object[] a = new Object[ps.length];
        for (int i = 0; i < ps.length; i++) a[i] = arg(ps[i]);
        return a;
    }

    private int npcModel() {
        Object npc = live.get(net.runelite.api.NPC.class);
        int[] m = npc != null && ((net.runelite.api.NPC) npc).getComposition() != null ? ((net.runelite.api.NPC) npc).getComposition().getModels() : null;
        return m != null && m.length > 0 ? m[0] : 0;
    }

    private int widgetId() {
        Object w = live.get(Widget.class);
        return w != null ? ((Widget) w).getId() : 0;
    }

    Object arg(Class<?> t) throws Missing {
        if (t == int.class) return 0;
        if (t == long.class) return 0L;
        if (t == boolean.class) return false;
        if (t == short.class) return (short) 0;
        if (t == byte.class) return (byte) 0;
        if (t == char.class) return 'a';
        if (t == float.class) return 0f;
        if (t == double.class) return 0d;
        if (t == String.class) return "api-probe";
        // The kinds an overlay hands a plugin: a host Graphics2D (as an overlay's) and a colour.
        if (t == java.awt.Graphics2D.class) return new java.awt.image.BufferedImage(64, 64, java.awt.image.BufferedImage.TYPE_INT_ARGB).createGraphics();
        if (t == java.awt.Color.class) return java.awt.Color.RED;
        if (t.isArray()) return Array.newInstance(t.getComponentType(), 0);
        if (t.isEnum()) return t.getEnumConstants()[0];
        Object v = live.get(t);
        if (v != null) return v;
        if (t == Object.class) return 0;
        if (t == List.class || t == Collection.class || t == Iterable.class) return new ArrayList<>();
        if (t == Set.class) return new HashSet<>();
        if (t == Map.class) return new HashMap<>();
        if (t.isInterface() && (!t.getName().startsWith(API) || EXCLUDED.containsKey(t.getName())))
            return Proxy.newProxyInstance(t.getClassLoader(), new Class<?>[] {t}, (o, m, a) -> dflt(m.getReturnType()));
        throw new Missing(t.getName().startsWith(API) ? t.getName().substring(API.length()) : t.getName());
    }

    static Object dflt(Class<?> r) {
        if (r == boolean.class) return false;
        if (r == int.class) return 0;
        if (r == long.class) return 0L;
        if (r == float.class) return 0f;
        if (r == double.class) return 0d;
        if (r == short.class) return (short) 0;
        if (r == byte.class) return (byte) 0;
        if (r == char.class) return (char) 0;
        return null;
    }

    /** A different value of the same type, so a read-back proves the write; objects round-trip unchanged. */
    static Object alter(Object old, Class<?> t) {
        if (t == int.class) return (Integer) old <= 0 ? (Integer) old + 1 : (Integer) old - 1;  // ids stay >= -1, 1-bit values flip
        if (t == boolean.class) return !(Boolean) old;
        if (t == long.class) return (Long) old + 1;
        if (t == short.class) return (short) ((Short) old + 1);
        if (t == byte.class) return (byte) ((Byte) old + 1);
        if (t == char.class) return (char) ((Character) old + 1);
        if (t == float.class) return (Float) old + 1;
        if (t == double.class) return (Double) old + 1;
        if (t == String.class) return old == null ? "api-probe" : old + "~";
        if (t.isEnum() && old != null) {
            Object[] all = t.getEnumConstants();
            return all[(((Enum<?>) old).ordinal() + 1) % all.length];
        }
        return old;
    }

    static boolean same(Object a, Object b) {
        return Arrays.deepEquals(new Object[] {a}, new Object[] {b});
    }

    private static Class<?> box(Class<?> c) {
        if (!c.isPrimitive()) return c;
        return Array.get(Array.newInstance(c, 1), 0).getClass();
    }

    // ---- report ----

    void set(Row row, String result, String evidence) {
        row.result = result;
        row.evidence = evidence == null ? "" : evidence;
    }

    static String describe(Object v) {
        if (v == null) return "null";
        Class<?> c = v.getClass();
        String s;
        if (c.isArray()) s = c.getComponentType().getSimpleName() + "[" + Array.getLength(v) + "]";
        else if (v instanceof Collection) s = c.getSimpleName() + "(" + ((Collection<?>) v).size() + ")";
        else if (v instanceof Map) s = c.getSimpleName() + "(" + ((Map<?, ?>) v).size() + ")";
        else if (v instanceof Number || v instanceof Boolean || v instanceof Character || v instanceof Enum) s = String.valueOf(v);
        else if (v instanceof String) s = "\"" + v + "\"";
        else if (c.getName().startsWith("java.") || c.getName().startsWith(API)) s = String.valueOf(v);
        else if (ownToString(c)) s = String.valueOf(v);  // a host object that says what it holds
        else s = c.getSimpleName().isEmpty() ? c.getName() : c.getSimpleName();
        s = s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
        return s.length() > 120 ? s.substring(0, 117) + "..." : s;
    }

    private static boolean ownToString(Class<?> c) {
        try {
            return c.getMethod("toString").getDeclaringClass() != Object.class;
        } catch (NoSuchMethodException e) {
            return false;
        }
    }

    private void finish() throws Exception {
        done = true;
        eventBus.unregister(this);
        for (Runnable r : cleanup) r.run();
        for (Map.Entry<String, List<Row>> e : byIface.entrySet())
            for (Row r : e.getValue())
                if (r.result == null) set(r, "unreached", "no live " + simple(r.iface) + " instance");
        for (Row r : rows) if (r.result == null || r.result.equals("deferred")) set(r, "unreached", "no live " + simple(r.iface) + " instance");
        Map<String, Integer> counts = new TreeMap<>();
        for (Row r : rows) counts.merge(r.result, 1, Integer::sum);
        int pass = counts.getOrDefault("pass", 0);
        String pct = String.format(java.util.Locale.ROOT, "%.1f", 100.0 * pass / rows.size());
        File tmp = new File(dir, "api-probe.tsv.part");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            w.write("# api-probe: every abstract method of every net.runelite.api interface, called on a live instance in the app\n");
            w.write("# api jar: " + apiJar + "\n");
            w.write("# build: " + build + "\n");
            for (Map.Entry<String, String> e : EXCLUDED.entrySet()) w.write("# excluded: " + e.getKey() + ": " + e.getValue() + "\n");
            w.write("# denominator: " + rows.size() + "\n");
            w.write("# counts: " + counts + "\n");
            w.write("# result: pass " + pass + " / " + rows.size() + " = " + pct + "%\n");
            w.write("interface\tmethod\tresult\tnative\tevidence\n");
            for (Row r : rows) w.write(r.iface + "\t" + r.name + r.desc + "\t" + r.result + "\t" + (r.nat ? "native" : "") + "\t" + clean(r.evidence) + "\n");
        }
        File out = new File(dir, "api-probe.tsv");
        if (!tmp.renameTo(out)) throw new java.io.IOException("rename to " + out + " failed");
        log.info("api-probe: done, pass {} / {} = {}%, {}", pass, rows.size(), pct, counts);
    }

    private static String clean(String s) {
        s = s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
        return s.length() > 300 ? s.substring(0, 297) + "..." : s;
    }

    private static String key(Row r) {
        return simple(r.iface) + "." + r.name;
    }

    private static String simple(String iface) {
        return iface.substring(iface.lastIndexOf('.') + 1);
    }

    static String desc(Method m) {
        StringBuilder sb = new StringBuilder("(");
        for (Class<?> p : m.getParameterTypes()) sb.append(desc(p));
        return sb.append(')').append(desc(m.getReturnType())).toString();
    }

    static String desc(Class<?> c) {
        if (c.isArray()) return "[" + desc(c.getComponentType());
        if (!c.isPrimitive()) return "L" + c.getName().replace('.', '/') + ";";
        return String.valueOf("VZBCSIJFD".charAt(Arrays.asList(void.class, boolean.class, byte.class, char.class, short.class,
            int.class, long.class, float.class, double.class).indexOf(c)));
    }
}
