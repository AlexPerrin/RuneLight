package net.runelite.host;

import android.util.Log;
import com.google.inject.AbstractModule;
import com.google.inject.Guice;
import com.google.inject.Injector;
import java.io.File;
import java.util.Enumeration;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import dalvik.system.DexClassLoader;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.events.ScriptPreFired;
import net.runelite.api.events.ScriptPostFired;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.ClientTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.events.VarbitChanged;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.plugins.HostPluginManager;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;

/** Plugin host entry points, called from native code (native/src/jni_bridge.cpp). */
public final class Host {
    public static final String TAG = "osrsrl-host";

    // Must match client_thread::EventType.
    private static final int CLIENT_TICK = 0, GAME_TICK = 1, GAME_STATE_CHANGED = 2, VARBIT_CHANGED = 3,
            NPC_SPAWNED = 4, NPC_DESPAWNED = 5, CHAT_MESSAGE = 6, WIDGET_LOADED = 7, WIDGET_CLOSED = 8,
            PLAYER_SPAWNED = 9, PLAYER_DESPAWNED = 10, ITEM_CONTAINER_CHANGED = 11, STAT_CHANGED = 12,
            MENU_OPENED = 13, MENU_OPTION_CLICKED = 14, MENU_TAP = 15, WIDGET_DRAG = 16, SOUND_EFFECT = 17, AREA_SOUND_EFFECT = 18;

    private static final net.runelite.client.callback.ClientThread clientThread = new net.runelite.client.callback.ClientThread();
    private static volatile EventBus eventBus;
    private static boolean profilePosted;
    private static Client client;

    public static Client client() { return client; }
    static EventBus eventBus() { return eventBus; }
    private static final NpcCache npcCache = new NpcCache();
    private static final PlayerCache playerCache = new PlayerCache();
    static AndroidNPC npc(int index) { return npcCache.get(index); }

    /** AnimationChanged / InteractingChanged / OverheadTextChanged from the per-cycle snapshots. */
    private static void postActorChanges(EventBus bus) {
        if (client.getGameState() != GameState.LOGGED_IN) return;
        List<Object[]> all = new java.util.ArrayList<>(npcCache.drainChanges());
        all.addAll(playerCache.drainChanges());
        boolean log = !all.isEmpty() && "1".equals(Native.property("debug.osrsrl.actorlog"));
        for (Object[] c : all) {
            net.runelite.api.Actor a = (net.runelite.api.Actor) c[0];
            int bits = (Integer) c[1];
            if (log) Log.i(TAG, "actor change " + bits + " " + a + " anim " + a.getAnimation() + " interacting " + a.getInteracting()
                + " overhead " + a.getOverheadText() + " orient " + a.getOrientation() + "/" + a.getCurrentOrientation());
            try {
                if ((bits & AndroidNPC.CHANGED_ANIMATION) != 0) {
                    net.runelite.api.events.AnimationChanged e = new net.runelite.api.events.AnimationChanged();
                    e.setActor(a);
                    bus.post(e);
                }
                if ((bits & AndroidNPC.CHANGED_INTERACTING) != 0) bus.post(new net.runelite.api.events.InteractingChanged(a, a.getInteracting()));
                if ((bits & AndroidNPC.CHANGED_OVERHEAD) != 0) bus.post(new net.runelite.api.events.OverheadTextChanged(a, a.getOverheadText()));
                if ((bits & AndroidNPC.CHANGED_HITSPLAT) != 0) {
                    java.util.List<net.runelite.api.Hitsplat> hits = a instanceof AndroidNPC ? ((AndroidNPC) a).newHitsplats : ((AndroidPlayer) a).newHitsplats;
                    if (hits != null) for (net.runelite.api.Hitsplat h : hits) {
                        if (log) Log.i(TAG, "HitsplatApplied " + a.getName() + " " + h + " health " + a.getHealthRatio() + "/" + a.getHealthScale());
                        net.runelite.api.events.HitsplatApplied e = new net.runelite.api.events.HitsplatApplied();
                        e.setActor(a);
                        e.setHitsplat(h);
                        bus.post(e);
                    }
                }
            } catch (Throwable t) {
                Log.e(TAG, "actor event failed", t);
            }
        }
        boolean hit = false;
        for (Object[] c : all) hit |= ((Integer) c[1] & AndroidNPC.CHANGED_HITSPLAT) != 0;
        if (hit) AndroidHealthBarConfig.poll(bus);  // a head bar comes with a hitsplat
    }
    static AndroidPlayer player(int index) { return playerCache.get(index); }
    /** RuneLite's interacting encoding: NPC index, or player index + 32768; -1 = none. */
    static net.runelite.api.Actor actor(int encoded) {
        if (encoded < 0) return null;
        return encoded < 32768 ? npcCache.get(encoded) : playerCache.get(encoded - 32768);
    }
    private static final AndroidWorldView worldView = new AndroidWorldView(npcCache, playerCache);
    private static final ClientTick clientTick = new ClientTick();
    private static final net.runelite.api.events.BeforeRender beforeRender = new net.runelite.api.events.BeforeRender();
    // Client.getFPS: frames (BeforeRender) counted over the last whole second.
    private static long fpsWindowStart;
    private static int fpsFrames;
    static volatile int fps;
    private static void noteFrame() {
        long now = android.os.SystemClock.uptimeMillis();
        if (fpsWindowStart == 0) fpsWindowStart = now;
        fpsFrames++;
        if (now - fpsWindowStart >= 1000) {
            fps = Math.round(fpsFrames * 1000f / (now - fpsWindowStart));
            fpsFrames = 0;
            fpsWindowStart = now;
        }
    }
    private static final net.runelite.client.ui.overlay.OverlayManager overlayManager =
            new net.runelite.client.ui.overlay.OverlayManager();
    private static final OverlayRenderer overlayRenderer = new OverlayRenderer(overlayManager);
    private static net.runelite.client.ui.overlay.tooltip.TooltipManager tooltipManager;
    private static final GameTick gameTick = new GameTick();
    private static final HostDrawManager drawManager = new HostDrawManager();

    private Host() {}

    /**
     * The host only loads plugins from `pluginDir`. OpenRune-Developer-Tools' self-updater (now
     * stubbed out of our build) could once download a release jar into
     * `<user.home>/<client>/sideloaded-plugins` and stage it in java.io.tmpdir; remove any such file.
     */
    private static void removeDownloadedPluginJars() {
        java.util.List<File> found = new java.util.ArrayList<>();
        for (String dir : new String[] {".runelite", ".rsprox", ".fluxious"}) {
            File d = new File(System.getProperty("user.home"), dir + "/sideloaded-plugins");
            found.add(new File(d, "OpenRune-Developer-Tools.jar"));
            found.add(new File(d, "OpenRune-Developer-Tools.jar.new"));
        }
        File[] tmp = new File(System.getProperty("java.io.tmpdir", "/nonexistent")).listFiles(
                (d, n) -> n.startsWith("openrune-devtools") && n.endsWith(".jar"));
        if (tmp != null) found.addAll(java.util.Arrays.asList(tmp));
        for (File f : found)
            if (f.exists()) Log.w(TAG, "removed downloaded plugin jar " + f + ": " + f.delete());
    }

    /** Called once on a background thread. `pluginDir` holds plugin jars (classes.dex inside). */
    public static void start(String pluginDir, String cacheDir) {
        ModalDialogs.install();
        Edt.start();
        Log.i(TAG, "host starting; plugins from " + pluginDir);
        // RuneLite.RUNELITE_DIR/CACHE_DIR come from user.home (empty on Android, so plugins wrote to
        // relative paths that fail: Emojis' cache). Use the app's files dir, before RuneLite loads.
        System.setProperty("user.home", new File(pluginDir).getParentFile().getPath());
        removeDownloadedPluginJars();
        client = new AndroidClient(worldView);
        EventBus bus = new EventBus();
        final HostPluginManager[] pluginManager = new HostPluginManager[1];
        final net.runelite.client.config.ConfigManager configManager = new net.runelite.client.config.ConfigManager(
                new File(new File(pluginDir).getParentFile(), "settings.properties"), bus::post);
        Injector injector = Guice.createInjector(new AbstractModule() {
            @Override
            protected void configure() {
                bind(Client.class).toInstance(client);
                bind(EventBus.class).toInstance(bus);
                // Named constants RuneLiteModule provides on desktop.
                for (String flag : new String[] {"developerMode", "safeMode", "insecureWriteCredentials", "noupdate", "disableTelemetry"}) {
                    bindConstant().annotatedWith(com.google.inject.name.Names.named(flag)).to(false);
                }
                bind(File.class).annotatedWith(com.google.inject.name.Names.named("runeLiteDir")).toInstance(new File(pluginDir).getParentFile());
                bind(File.class).annotatedWith(com.google.inject.name.Names.named("sessionfile"))
                        .toInstance(new File(new File(pluginDir).getParentFile(), "session"));
                bind(String.class).annotatedWith(com.google.inject.name.Names.named("profile")).toInstance("default");
                bind(String.class).annotatedWith(com.google.inject.name.Names.named("runelite.title")).toInstance("RuneLite");
                bind(EventBus.class).annotatedWith(com.google.inject.name.Names.named("Deferred EventBus")).toInstance(bus);
                bind(net.runelite.client.callback.ClientThread.class).toInstance(clientThread);
                bind(net.runelite.client.ui.overlay.OverlayManager.class).toInstance(overlayManager);
                bind(net.runelite.client.ui.DrawManager.class).toInstance(drawManager);
                bind(net.runelite.client.config.ConfigManager.class).toInstance(configManager);
                Host.configManager = configManager;
                bind(net.runelite.client.config.RuneLiteConfig.class)
                        .toInstance(configManager.getConfig(net.runelite.client.config.RuneLiteConfig.class));
                bind(net.runelite.client.config.ChatColorConfig.class)
                        .toInstance(configManager.getConfig(net.runelite.client.config.ChatColorConfig.class));
                bind(java.util.concurrent.ScheduledExecutorService.class)
                        // As on desktop, task exceptions are logged instead of vanishing in the Future.
                        .toInstance(new net.runelite.client.util.ExecutorServiceExceptionLogger(
                                java.util.concurrent.Executors.newSingleThreadScheduledExecutor()));
                // HTTP for plugin services (NPCManager, HiscoreManager, ...): okhttp 3.14 (pure Java,
                // API-compatible with the calls RuneLite's client makes).
                // NetGuard refuses uploads (non-GET, xp/update) by default: the host plays on a test server.
                // As desktop's base client (RuneLite.buildHttpClient): every request carries
                // "User-Agent: RuneLite/<version>-<commit>[+]" and nothing else that identifies the player.
                final String userAgent = "RuneLite/" + net.runelite.client.RuneLiteProperties.getVersion() + "-"
                        + net.runelite.client.RuneLiteProperties.getCommit() + (net.runelite.client.RuneLiteProperties.isDirty() ? "+" : "");
                bind(okhttp3.OkHttpClient.class).toInstance(new okhttp3.OkHttpClient.Builder()
                        .addNetworkInterceptor(chain -> {
                            okhttp3.Request r = chain.request().newBuilder().header("User-Agent", userAgent).build();
                            if ("1".equals(Native.property("debug.osrsrl.netlog")))
                                Log.i(NetGuard.TAG, "sent " + r.method() + " " + r.url() + " " + r.headers().toString().replace('\n', ';'));
                            return chain.proceed(r);
                        })
                        .addInterceptor(new NetGuard()).build());
                bind(okhttp3.HttpUrl.class).annotatedWith(com.google.inject.name.Names.named("runelite.static.base"))
                        .toInstance(okhttp3.HttpUrl.get("https://static.runelite.net"));
                bind(okhttp3.HttpUrl.class).annotatedWith(com.google.inject.name.Names.named("runelite.ws"))
                        .toInstance(okhttp3.HttpUrl.get("https://api.runelite.net/ws"));
                bind(okhttp3.HttpUrl.class).annotatedWith(com.google.inject.name.Names.named("runelite.api.base"))
                        .toInstance(okhttp3.HttpUrl.get("https://api.runelite.net/runelite-1.13.1"));
                // ItemStatChangesService/XpTrackerService come from their plugins' getPublicModule()
                // (HostPluginManager installs them for dependents, as RuneLite does).
                // As RuneLiteModule: RuneLite's Gson (Color/Instant adapters); Tile Packs reads
                // "#AARRGGBB" colours through it. Without the binding Guice made a plain Gson.
                bind(com.google.gson.Gson.class).toInstance(net.runelite.http.api.RuneLiteAPI.GSON);
                bind(net.runelite.client.callback.Hooks.class).toInstance(hooks);
                bind(net.runelite.client.ui.ClientToolbar.class).toInstance(new net.runelite.client.ui.ClientToolbar());
                // RuneLite's ClientUI constructor casts the Client to an AWT Component (the applet). Plugins
                // that inject it without using it (Ground Object Hider) get an unconstructed instance;
                // its methods are not usable. Verified gen 34: Ground Object Hider starts.
                Object ui = unconstructed(net.runelite.client.ui.ClientUI.class);
                if (ui != null) bind(net.runelite.client.ui.ClientUI.class).toInstance((net.runelite.client.ui.ClientUI) ui);
                bind(net.runelite.client.game.SpriteManager.class).toInstance(new net.runelite.client.game.SpriteManager());
                bind(net.runelite.client.chat.ChatMessageManager.class).toInstance(new net.runelite.client.chat.ChatMessageManager());
                bind(net.runelite.client.game.chatbox.ChatboxPanelManager.class)
                        .toInstance(new net.runelite.client.game.chatbox.ChatboxPanelManager());
                bind(net.runelite.client.plugins.PluginManager.class).toProvider(
                        () -> new net.runelite.client.plugins.PluginManager(pluginManager[0]));
            }
        });
        // Some plugins call RuneLite.getInjector(); desktop main() sets this static.
        try {
            net.runelite.client.RuneLite.setInjector(injector);
        } catch (Throwable t) {
            Log.e(TAG, "could not set RuneLite.injector", t);
        }
        Sidebar.start(injector.getInstance(net.runelite.client.ui.ClientToolbar.class), new File(pluginDir).getParentFile());
        try {
            TouchMouse.manager = injector.getInstance(net.runelite.client.input.MouseManager.class);
            KeyRouter.manager = injector.getInstance(net.runelite.client.input.KeyManager.class);
            KeyRouter.clientThread = injector.getInstance(net.runelite.client.callback.ClientThread.class);
        } catch (Throwable t) {
            Log.e(TAG, "MouseManager failed", t);
        }
        // RuneLite core overlay (desktop adds it in RuneLite.start): draws TooltipManager's tooltips
        // at the "mouse", which is the touch point here (AndroidClient.getMouseCanvasPosition).
        try {
            overlayManager.add(injector.getInstance(net.runelite.client.ui.overlay.tooltip.TooltipOverlay.class));
            tooltipManager = injector.getInstance(net.runelite.client.ui.overlay.tooltip.TooltipManager.class);
        } catch (Throwable t) {
            Log.e(TAG, "tooltip overlay failed", t);
        }
        // Desktop's OverlayManager.init also adds WorldMapOverlay: it draws every plugin's
        // WorldMapPointManager points on the open world map (interface 595). Without it World Map's
        // points were never drawn (gen 31).
        try {
            overlayManager.add(injector.getInstance(net.runelite.client.ui.overlay.worldmap.WorldMapOverlay.class));
        } catch (Throwable t) {
            Log.e(TAG, "world map overlay failed", t);
        }
        pluginManager[0] = new HostPluginManager(injector, bus);
        HostPluginManager plugins = pluginManager[0];
        File[] jars = new File(pluginDir).listFiles((d, n) -> n.endsWith(".jar"));
        if (jars != null) {
            for (File jar : jars) loadJar(plugins, jar, cacheDir);
        }
        loader = jar -> loadJar(plugins, new File(pluginDir, jar), cacheDir);
        worldView().androidScene().objects.bus = bus;
        Probe.init(plugins.getPlugins());
        eventBus = bus;
        hooks.attach(bus, clientThread);
        bus.subscribersChanged = Host::updateScriptEvents;
        updateScriptEvents();
        Log.i(TAG, "host ready; " + plugins.getPlugins().size() + " plugin(s) running");
        startStackSampler();
    }

    /** Loads one more jar from the plugins dir after start (Probe's `load` hook; tools/api-probe.sh). */
    static java.util.function.Consumer<String> loader;

    private static void loadJar(HostPluginManager plugins, File jar, String cacheDir) {
        try {
            // Android 14+ refuses writable dex files; harmless before that.
            File copy = new File(cacheDir, jar.getName());
            java.nio.file.Files.copy(jar.toPath(), copy.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            copy.setReadOnly();
            // R1 (docs/app-telemetry.md): scan the copy that would be loaded, so the checked bytes are the loaded bytes.
            String refused = NetScan.refusal(copy);
            if (refused != null) {
                Log.w(TAG, "netscan: refused " + jar.getName() + ": " + refused
                        + " (vet it, or list it in debug.osrsrl.netallow)");
                copy.delete();
                return;
            }
            ClassLoader loader = new DexClassLoader(copy.getPath(), cacheDir, null, Host.class.getClassLoader());
            // Plugin classes are found by scanning the jar's class list (kept beside the dex
            // by tools/dex-plugin.sh) for @PluginDescriptor.
            try (ZipFile zip = new ZipFile(copy)) {
                ZipEntry list = zip.getEntry("plugin-classes.txt");
                if (list == null) {
                    Log.e(TAG, jar + ": no plugin-classes.txt");
                    return;
                }
                String[] names = new String(readAll(zip.getInputStream(list)), "UTF-8").split("\n");
                for (String name : names) {
                    name = name.trim();
                    if (name.isEmpty()) continue;
                    Class<?> c = loader.loadClass(name);
                    if (Plugin.class.isAssignableFrom(c) && c.isAnnotationPresent(PluginDescriptor.class)) {
                        plugins.start(c.asSubclass(Plugin.class));
                    }
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "failed to load " + jar, t);
        }
    }

    private static byte[] readAll(java.io.InputStream in) throws java.io.IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
        return out.toByteArray();
    }

    /** Called on the game thread after every client cycle and for each derived event. */
    private static volatile Thread gameThread;
    static boolean onGameThread() { return Thread.currentThread() == gameThread; }
    /** Incremented every client cycle (Java-side caches key on it). */
    static volatile int cycleStamp;

    /** `setprop debug.osrsrl.stacks 1`: logs the game and main threads' Java stacks every 2 s. */
    private static void startStackSampler() {
        Thread t = new Thread(() -> {
            while (true) {
                try { Thread.sleep(2000); } catch (InterruptedException e) { return; }
                if (!"1".equals(Native.property("debug.osrsrl.stacks"))) continue;
                for (Thread th : new Thread[] {gameThread, android.os.Looper.getMainLooper().getThread()}) {
                    if (th == null) continue;
                    StringBuilder sb = new StringBuilder("stack " + th.getName() + ":");
                    StackTraceElement[] st = th.getStackTrace();
                    for (int i = 0; i < Math.min(st.length, 14); i++) sb.append("\n  ").append(st[i]);
                    Log.i(TAG, sb.toString());
                }
            }
        }, "osrsrl-stacks");
        t.setDaemon(true);
        t.start();
    }

    public static void onEvent(int type, int a, int b) {
        EventBus bus = eventBus;
        if (bus == null) return;
        boolean first = gameThread == null;
        if (first) gameThread = Thread.currentThread();
        try {
            // Native posts a state only on a change, and changes before 'host ready' were dropped
            // (bus null): post the game's current state once, as desktop plugins see it from the start.
            if (first && type != GAME_STATE_CHANGED) dispatch(bus, GAME_STATE_CHANGED, Native.gameState(), -1);
            dispatch(bus, type, a, b);
        } finally {
            AndroidWidget.applyChildEdits();
        }
    }

    /**
     * RuneLite's chat scripts call runelite_callback "chatFilterCheck" for each line they build
     * (int stack: ..., keep flag, type, message id; object stack: ..., text); the game's scripts
     * here don't, so it is raised once when the message arrives. A rewritten text is written
     * into the game's message; a blocked one (keep flag 0) is blanked. Duplicate collapsing,
     * which relies on the per-redraw calls, doesn't happen.
     */
    private static void chatFilterCheck(EventBus bus, AndroidMessageNode node, int type) {
        if (!bus.hasSubscribers(net.runelite.api.events.ScriptCallbackEvent.class)) return;
        String text = node.getValue() != null ? node.getValue() : "";
        AndroidClient client = (AndroidClient) Host.client;
        client.fakeStacks(new int[] {1, type, node.getId()}, new Object[] {text});
        net.runelite.api.events.ScriptCallbackEvent e = new net.runelite.api.events.ScriptCallbackEvent();
        e.setEventName("chatFilterCheck");
        bus.post(e);
        int[] ints = client.getIntStack();
        Object out = client.getObjectStack()[0];
        if (ints[0] == 0) {
            node.setValue("");
        } else if (out instanceof String && !out.equals(text)) {
            node.setValue((String) out);
        }
    }

    /**
     * RuneLite's chat line builder calls runelite_callback "chatMessageBuilding" (int stack: ...,
     * message id; object stack: ..., timestamp text) and puts the returned text before the line
     * (Chat Timestamps). The game's scripts don't, so it is raised once per new message and a
     * non-empty result is put before the message text.
     */
    private static void chatMessageBuilding(EventBus bus, AndroidMessageNode node) {
        if (!bus.hasSubscribers(net.runelite.api.events.ScriptCallbackEvent.class)) return;
        String text = node.getValue();
        if (text == null || text.isEmpty()) return;  // blocked by chatFilterCheck
        AndroidClient client = (AndroidClient) Host.client;
        client.fakeStacks(new int[] {node.getId()}, new Object[] {""});
        net.runelite.api.events.ScriptCallbackEvent e = new net.runelite.api.events.ScriptCallbackEvent();
        e.setEventName("chatMessageBuilding");
        bus.post(e);
        Object out = client.getObjectStack()[0];
        if (out instanceof String && !((String) out).isEmpty()) node.setValue(out + " " + text);
    }

    private static final java.util.List<Integer> pendingStats = new java.util.ArrayList<>();
    /** Inventories changed before the local player exists (login burst), posted once it does. */
    private static final java.util.Set<Integer> pendingInvs = new java.util.LinkedHashSet<>();
    private static final java.util.Map<Integer, AndroidNPC> pendingNpcs = new java.util.LinkedHashMap<>();

    private static boolean pendingLoggedIn;
    /** Cycles a LOGGED_IN after LOADING has waited for the new scene base, or -1. */
    private static int pendingSceneCycles = -1;
    private static GameState lastPostedState;
    /** The login screen's page last logged ('LoginIndex <n>'), or -1 off the login screen (device_login reads it). */
    private static int lastLoginIndex = -1;

    /**
     * Latched with the posted GameStateChanged: false from a posted non-LOGGED_IN state until
     * LOGGED_IN is posted (after the local player exists). AndroidClient.getGameState reads LOADING
     * while the game is at 30 but LOGGED_IN hasn't been posted yet, as desktop never reports
     * LOGGED_IN without a local player.
     */
    static volatile boolean loggedInPosted;

    private static void postLoggedIn(EventBus bus) {
        pendingLoggedIn = false;
        postGameState(bus, GameState.LOGGED_IN);
    }

    /** An instance of `c` without running a constructor (sun.misc.Unsafe.allocateInstance), or null. */
    private static Object unconstructed(Class<?> c) {
        try {
            Class<?> u = Class.forName("sun.misc.Unsafe");
            java.lang.reflect.Field f;
            try { f = u.getDeclaredField("theUnsafe"); } catch (NoSuchFieldException e) { f = u.getDeclaredField("THE_ONE"); }
            f.setAccessible(true);
            return u.getMethod("allocateInstance", Class.class).invoke(f.get(null), c);
        } catch (Throwable t) {
            Log.w(TAG, "no unconstructed " + c.getName() + ": " + t);
            return null;
        }
    }

    /** HostPluginManager: startUp as on desktop's EDT (Edt.runAsEdtAtStartup). */
    public static void runAsEdtAtStartup(java.util.concurrent.Callable<Void> r) throws Exception {
        Edt.runAsEdtAtStartup(r);
    }

    private static void pollAmbientSounds(EventBus bus) {
        try {
            ((AndroidClient) client).ambientSounds.poll(bus);
        } catch (Throwable t) {
            Log.e(TAG, "ambient sounds failed", t);
        }
    }

    /** The game's last native state: values outside GameState (1000, the start error page) log as UNKNOWN. */
    private static volatile int rawGameState = -1;

    private static void postGameState(EventBus bus, GameState s) {
        GameStateChanged e = new GameStateChanged();
        // Desktop's ConfigManager switches the RuneScape profile while logging in; plugins
        // (Quest Helper) wait for that event before their first LOGGED_IN setup.
        if (s == GameState.LOGIN_SCREEN) { profilePosted = false; pendingNpcs.clear(); ChatChannels.reset(); }
        if (s == GameState.LOGGED_IN) ((AndroidClient) client).applyIdleTimeout();
        if (s == GameState.LOGGED_IN && !profilePosted) {
            profilePosted = true;
            try {
                net.runelite.api.Player me = localPlayer();
                configManager.updateRSProfile(me != null ? me.getName() : null,
                        net.runelite.client.config.RuneScapeProfileType.getCurrent(client), client.getAccountHash());
            } catch (RuntimeException ex) {
                Log.e(TAG, "rs profile update failed", ex);
            }
            bus.post(new net.runelite.client.events.RuneScapeProfileChanged(null, "default"));
            // Desktop posts UsernameChanged when the login name is set; Android token logins have no
            // name field, so it is posted once per login here, once getUsername/getAccountHash are
            // valid (Mahogany Homes reloads its saved contract on it; verified on the device gen 34, with
            // AndroidClient.getAccountHash reading the raw state while getGameState still reads LOADING).
            bus.post(new net.runelite.api.events.UsernameChanged());
        }
        // Desktop posts WorldViewLoaded for the top-level view when a scene load finishes, before
        // the LOADING -> LOGGED_IN change (Ground Markers/Object Indicators reload their points
        // for the new regions, Wilderness Lines rebuilds its paths).
        if (s == GameState.LOGGED_IN) postWorldViewLoaded(bus);
        if (s == GameState.LOADING && worldView != null) worldView.androidScene().objects.forgetObjects();
        else if (s == GameState.LOGIN_SCREEN || s == GameState.HOPPING) loadedBase = Long.MIN_VALUE;
        // Desktop creates the scene's ambient sounds while loading, before LOGGED_IN.
        if (s == GameState.LOGGED_IN) pollAmbientSounds(bus);
        else if (s == GameState.LOGIN_SCREEN || s == GameState.HOPPING) ((AndroidClient) client).ambientSounds.reset();
        e.setGameState(s != null ? s : GameState.UNKNOWN);
        loggedInPosted = e.getGameState() == GameState.LOGGED_IN;
        lastPostedState = e.getGameState();
        if (e.getGameState() != GameState.LOGGED_IN) pendingSceneCycles = -1;
        Log.i(TAG, "GameStateChanged " + e.getGameState() + " (raw " + rawGameState + ", getGameState " + client.getGameState() + ", local player "
            + (localPlayer() != null) + ")");
        bus.post(e);
    }

    /** True while the host posts a teleport/region rebuild's GameStateChanged(LOADING): getGameState reads LOADING. */
    static volatile boolean sceneReloading;

    /**
     * A scene rebuild while logged in (teleport, region crossing) runs within one cycle, so the
     * state poll never sees LOADING. Desktop posts GameStateChanged(LOADING), then WorldViewLoaded,
     * then GameStateChanged(LOGGED_IN) for each one (Herbiboar re-checks its area on LOADING), so
     * replay that order when the scene base changes.
     */
    private static void postSceneReload(EventBus bus) {
        AndroidWorldView wv = worldView;
        if (wv == null || client.getTopLevelWorldView() == null) return;
        long base = ((long) wv.getBaseX() << 32) | (wv.getBaseY() & 0xffffffffL);
        if (base == loadedBase || base == 0) return;
        if (loadedBase == Long.MIN_VALUE) { postWorldViewLoaded(bus); return; }  // first scene: LOGGED_IN was just posted
        postLoading(bus);
        postGameState(bus, GameState.LOGGED_IN);  // posts WorldViewLoaded first
    }

    /** -1, or the cycles a rebuild's LOADING (native 25 while logged in) has been held for its new scene base. */
    private static int heldRebuildCycles = -1;

    /** GameStateChanged(LOADING) for a rebuild while logged in; getGameState reads LOADING meanwhile. */
    private static void postLoading(EventBus bus) {
        if (worldView != null) worldView.androidScene().objects.forgetObjects();
        GameStateChanged loading = new GameStateChanged();
        loading.setGameState(GameState.LOADING);
        Log.i(TAG, "GameStateChanged LOADING (scene rebuild)");
        sceneReloading = true;
        try {
            bus.post(loading);
        } finally {
            sceneReloading = false;
        }
    }

    /** The world view shows a scene base other than the last WorldViewLoaded's (and not 0,0 mid-rebuild). */
    private static boolean sceneBaseChanged() {
        AndroidWorldView wv = worldView;
        if (wv == null || client.getTopLevelWorldView() == null) return false;
        long base = ((long) wv.getBaseX() << 32) | (wv.getBaseY() & 0xffffffffL);
        return base != 0 && base != loadedBase;
    }

    /** Scene base (x << 32 | y) the last WorldViewLoaded was posted for. */
    private static long loadedBase = Long.MIN_VALUE;

    /**
     * Posts WorldViewLoaded for the top-level view once per scene. The game rebuilds a scene
     * (state 30 -> 25 -> 30) within one cycle on a teleport or region crossing, so the state poll
     * doesn't see LOADING; a changed scene base marks a new scene instead.
     */
    private static void postWorldViewLoaded(EventBus bus) {
        AndroidWorldView wv = worldView;
        if (wv == null || client.getTopLevelWorldView() == null) return;
        long base = ((long) wv.getBaseX() << 32) | (wv.getBaseY() & 0xffffffffL);
        if (base == loadedBase || base == 0) return;  // 0,0 = mid-rebuild (seen on a teleport)
        loadedBase = base;
        Log.i(TAG, "WorldViewLoaded base " + wv.getBaseX() + "," + wv.getBaseY());
        bus.post(new net.runelite.api.events.WorldViewLoaded(client.getTopLevelWorldView()));
    }

    private static void dispatch(EventBus bus, int type, int a, int b) {
        switch (type) {
            case CLIENT_TICK:
                cycleStamp++;
                AndroidWidget.reapplyHidden();
                if (client instanceof AndroidClient) ((AndroidClient) client).syncModIcons();
                KeyRouter.syncShift();
                bus.post(clientTick);
                WidgetListeners.runCycle(client);
                WidgetListeners.runTargetMode(client);
                postActorChanges(bus);
                if (pendingLoggedIn && localPlayer() != null) postLoggedIn(bus);
                int loginIndex = lastPostedState == GameState.LOGIN_SCREEN ? Native.loginIndex() : -1;
                if (loginIndex != lastLoginIndex && (lastLoginIndex = loginIndex) >= 0) Log.i(TAG, "LoginIndex " + loginIndex);
                if (loggedInPosted) pollAmbientSounds(bus);
                if (loggedInPosted && client instanceof AndroidClient) {
                    ((AndroidClient) client).projectiles.poll(bus, client);
                    ((AndroidClient) client).graphicsObjects.poll(bus);
                } else if (client instanceof AndroidClient) {
                    ((AndroidClient) client).projectiles.reset();
                    ((AndroidClient) client).graphicsObjects.reset();
                }
                if (pendingSceneCycles >= 0 && (sceneBaseChanged() || ++pendingSceneCycles > 50)) {
                    pendingSceneCycles = -1;
                    postGameState(bus, GameState.LOGGED_IN);  // WorldViewLoaded, then LOGGED_IN
                }
                if (heldRebuildCycles >= 0 && client.getGameState() == GameState.LOGGED_IN) {
                    // A held rebuild: the new base replays below; the same scene (a reload, a teleport
                    // within it) gets its LOADING/LOGGED_IN pair after 50 cycles, as before.
                    if (sceneBaseChanged()) heldRebuildCycles = -1;
                    else if (++heldRebuildCycles > 50) {
                        heldRebuildCycles = -1;
                        postLoading(bus);
                        postGameState(bus, GameState.LOGGED_IN);
                    }
                }
                if (!pendingLoggedIn && heldRebuildCycles < 0 && client.getGameState() == GameState.LOGGED_IN && localPlayer() != null) {
                    postSceneReload(bus);
                    try {
                        GeOffers.poll(bus);
                    } catch (Throwable t) {
                        Log.e(TAG, "GE offers failed", t);
                    }
                } else if (client.getGameState().getState() < GameState.LOADING.getState()) {
                    GeOffers.reset();
                }
                if (!pendingNpcs.isEmpty() && localPlayer() != null) {
                    java.util.List<AndroidNPC> held = new java.util.ArrayList<>(pendingNpcs.values());
                    pendingNpcs.clear();
                    for (AndroidNPC npc : held) bus.post(new NpcSpawned(npc));
                }
                if (!pendingStats.isEmpty() && localPlayer() != null) {
                    java.util.List<Integer> held = new java.util.ArrayList<>(new java.util.LinkedHashSet<>(pendingStats));
                    pendingStats.clear();
                    for (int skill : held) dispatch(bus, STAT_CHANGED, skill, 0);
                }
                if (!pendingInvs.isEmpty() && localPlayer() != null) {
                    java.util.List<Integer> held = new java.util.ArrayList<>(pendingInvs);
                    pendingInvs.clear();
                    for (int inv : held) dispatch(bus, ITEM_CONTAINER_CHANGED, inv, 0);
                }
                runQueued();
                AndroidMenu.onCycle();
                ((AndroidClient) client).fetchWantedDbFields();
                HostSprites.onCycle(client);
                ((AndroidClient) client).drawMinimapTiles();
                applyDrawListeners();
                if (cycleStamp % 10 == 0) ((AndroidClient) client).applyMapDots();
                Sidebar.onCycle();
                try {
                    DevMenu.onCycle();
                } catch (Throwable t) {
                    Log.e(TAG, "dev menu failed", t);
                }
                try {
                    drawManager.onCycle();
                } catch (Throwable t) {
                    Log.e(TAG, "draw manager failed", t);
                }
                // Desktop posts BeforeRender once per frame before drawing (Customizable XP Drops
                // advances its drops there); here one cycle is one rendered frame.
                noteFrame();
                bus.post(beforeRender);
                try {
                    overlayRenderer.render();
                    // Tooltips added in BeforeRender are drawn and cleared by TooltipOverlay; on the
                    // cycles overlays skip, drop them, or the next frame shows them twice (Item Prices).
                    if (overlayRenderer.skipped && tooltipManager != null) tooltipManager.clear();
                } catch (Throwable t) {
                    Log.e(TAG, "overlay rendering failed", t);
                }
                break;
            case GAME_TICK:
                // The desktop client doesn't tick while LOADING (it loads the map synchronously).
                // QH's quest list update runs on GameTick and drops its one-shot flag if the state
                // isn't LOGGED_IN; OpenRune's login sends a rebuild, so ticks arrive while LOADING.
                if (client.getGameState() == GameState.LOADING) break;
                // Nor before the local player exists (Tree Despawn Timer reads it every tick).
                if (localPlayer() == null) break;
                if (client.getGameState() == GameState.LOGGED_IN) {
                    try {
                        worldView().androidScene().objects.refresh();
                    } catch (Throwable t) {
                        Log.e(TAG, "scene refresh failed", t);
                    }
                }
                postActorChanges(bus);  // hitsplats of this tick before its GameTick, as on desktop
                try {
                    ChatChannels.postChanges(bus);
                } catch (RuntimeException ex) {
                    Log.e(TAG, "chat channel events", ex);
                }
                bus.post(gameTick);
                clientThread.runTickEnd();
                Probe.onGameTick();
                break;
            case GAME_STATE_CHANGED: {
                rawGameState = a;
                GameState s = GameState.of(a);
                // Desktop has the local player when GameStateChanged(LOGGED_IN) is posted (Spawn
                // Predictor reads its name there); the game's player list can still be empty on
                // that cycle, so LOGGED_IN waits for it (flushed on ClientTick, before held spawns).
                if (pendingLoggedIn) postLoggedIn(bus);
                if (s == GameState.LOGGED_IN && localPlayer() == null) { pendingLoggedIn = true; break; }
                // A rebuild the game reports as 30 -> 25 -> 30 (OpenRune's teleports) can reach 30
                // before the world view shows the new base; desktop posts WorldViewLoaded and then
                // LOGGED_IN once, so LOGGED_IN waits for the new base (or 50 cycles: same scene).
                // Without this, the scene-base replay below posted a second LOADING/LOGGED_IN.
                if (s == GameState.LOGGED_IN && lastPostedState == GameState.LOADING && !sceneBaseChanged()) {
                    pendingSceneCycles = 0;
                    break;
                }
                // A rebuild while logged in (OpenRune's teleports: 30 -> 25 -> 30) reaches 25 before the
                // world view has the new scene base, so getMapRegions would still answer the old scene's
                // regions (Herbiboar checks its area on LOADING). Desktop has the new regions from the
                // rebuild packet by then. The LOADING is held until the new base shows; then the
                // scene-base replay (postSceneReload) posts LOADING, WorldViewLoaded and LOGGED_IN.
                if (s == GameState.LOADING && lastPostedState == GameState.LOGGED_IN && loggedInPosted) {
                    heldRebuildCycles = 0;
                    break;
                }
                if (heldRebuildCycles >= 0 && s == GameState.LOGGED_IN) break;  // still held: the tick posts it
                // The game's 30 can come after the tick that released the hold (the replay already
                // posted LOADING, WorldViewLoaded, LOGGED_IN); desktop never posts LOGGED_IN twice in a row.
                if (s == GameState.LOGGED_IN && lastPostedState == GameState.LOGGED_IN && loggedInPosted) {
                    Log.i(TAG, "GameStateChanged LOGGED_IN again: already posted (rebuild released)");
                    break;
                }
                heldRebuildCycles = -1;
                postGameState(bus, s);
                break;
            }
            case NPC_SPAWNED: {
                AndroidNPC npc = npcCache.getFresh(a);
                if (npc == null) Log.w(TAG, "NpcSpawned " + a + " (id " + b + "): not in the NPC snapshot");
                if (npc != null && "1".equals(Native.property("debug.osrsrl.actorlog"))) {
                    net.runelite.api.coords.LocalPoint lp = npc.getLocalLocation();
                    Log.i(TAG, "NpcSpawned " + npc.getName() + " id " + npc.getId() + " idx " + a + " local " + lp
                        + " world " + npc.getWorldLocation() + " fromLocalInstance " + net.runelite.api.coords.WorldPoint.fromLocalInstance(client, lp)
                        + " base " + worldView.getBaseX() + "," + worldView.getBaseY() + " state " + client.getGameState());
                }
                if (npc == null) break;
                // Desktop reads the local player before the NPCs of the same update; plugins
                // (Attack Timer Metronome) read it in onNpcSpawned. Held until it exists.
                if (localPlayer() == null) { pendingNpcs.put(a, npc); break; }
                bus.post(new NpcSpawned(npc));
                break;
            }
            case NPC_DESPAWNED: {
                if (pendingNpcs.remove(a) != null) { npcCache.despawned(a); break; }
                AndroidNPC npc = npcCache.cached(a);
                npcCache.despawned(a);
                if (npc != null) bus.post(new NpcDespawned(npc));
                break;
            }
            case CHAT_MESSAGE: {
                if (ScriptCallbacks.takeReplaced(a)) break;  // re-added with another type (ExamineItem)
                String[] m = Native.message(a);
                if (m == null) break;
                ChatMessageType chatType = ChatMessageType.of(b);
                int now = (int) (System.currentTimeMillis() / 1000);
                // A message a plugin added through Client.addChatMessage keeps the node it was given.
                AndroidMessageNode node = AndroidClient.takePending(chatType, m[2]);
                if (node != null) node.bind(a, m[2]);
                else node = new AndroidMessageNode(a, chatType, m[0], m[1], m[2], now);
                AndroidClient.rememberMessage(node);
                if (node.postEvent) bus.post(new ChatMessage(node, chatType, node.getName(), node.getValue(), node.getSender(), node.getTimestamp()));
                // RuneLite's ChatMessageManager applies a node's RuneLite-format message (colour
                // placeholders such as <colHIGHLIGHT>, set by Chat Notifications) when the chat
                // line is built ("chatMessageBuilding"); here once, after the ChatMessage handlers.
                if (node.getRuneLiteFormatMessage() != null)
                    node.setValue(net.runelite.client.chat.ChatMessageManager.format(node.getRuneLiteFormatMessage()));
                chatFilterCheck(bus, node, b);
                chatMessageBuilding(bus, node);
                if ("1".equals(Native.property("debug.osrsrl.chatlog")))
                    Log.i(TAG, "chat id " + a + " type " + b + " (" + chatType + ") name '" + m[0] + "' sender '" + m[1]
                        + "' text '" + m[2] + "' -> '" + node.getValue() + "'");
                break;
            }
            case PLAYER_SPAWNED: {
                AndroidPlayer p = playerCache.getFresh(a);
                if (p != null) bus.post(new net.runelite.api.events.PlayerSpawned(p));
                break;
            }
            case PLAYER_DESPAWNED: {
                AndroidPlayer p = playerCache.cached(a);
                playerCache.despawned(a);
                if (p != null) bus.post(new net.runelite.api.events.PlayerDespawned(p));
                break;
            }
            case ITEM_CONTAINER_CHANGED: {
                // The login burst sends inventories before the local player exists; desktop plugins
                // (Looting Bag's handleInventoryUpdated reads getLocalPlayer().getWorldLocation()) assume
                // one, so hold them like StatChanged (posted with the containers as of that cycle).
                // Verified on the device gen 34 (Looting Bag's NPE at login is gone).
                if (localPlayer() == null) {
                    pendingInvs.add(a);
                    break;
                }
                AndroidItemContainer c = AndroidItemContainer.of(a);
                if (c != null) bus.post(new net.runelite.api.events.ItemContainerChanged(a, c));
                break;
            }
            case STAT_CHANGED: {
                net.runelite.api.Skill[] all = net.runelite.api.Skill.values();
                if (a < 0 || a >= all.length) break;
                // The login burst sends stats before the local player exists; desktop plugins (Agility)
                // assume a local player in StatChanged, so hold them until it does.
                if (localPlayer() == null) {
                    pendingStats.add(a);
                    break;
                }
                int[] sk = Native.skills();
                bus.post(new net.runelite.api.events.StatChanged(all[a], sk[50 + a], sk[25 + a], sk[a]));
                break;
            }
            case WIDGET_LOADED: {
                net.runelite.api.events.WidgetLoaded e = new net.runelite.api.events.WidgetLoaded();
                e.setGroupId(a);
                if ("1".equals(Native.property("debug.osrsrl.widgetlog"))) Log.i(TAG, "WidgetLoaded " + a);
                bus.post(e);
                break;
            }
            case WIDGET_CLOSED:
                if ("1".equals(Native.property("debug.osrsrl.widgetlog"))) Log.i(TAG, "WidgetClosed " + a);
                bus.post(new net.runelite.api.events.WidgetClosed(a, 0, true));
                break;
            case MENU_OPENED: {
                AndroidClient.MENU.clearAdded();
                // Remember the menu's tile for entries that run after the menu closes.
                try {
                    net.runelite.api.WorldView wv = client.getTopLevelWorldView();
                    if (wv != null) wv.getSelectedSceneTile();
                } catch (RuntimeException ignored) { }
                // The game builds the whole menu at once; RuneLite posts MenuEntryAdded as each entry
                // is inserted (bottom first), so post them now in that order, then PostMenuSort as on
                // the frame before the menu opened (Menu Entry Swapper's order is shown), and write
                // option/target edits (e.g. Ground Items' recoloured item names) back into the game.
                AndroidClient.MENU.presort(bus);
                // Plugin player options (MenuManager sets them from MenuEntryAdded, so after presort).
                for (net.runelite.api.MenuEntry m : AndroidClient.MENU.addPlayerOptions(
                        client.getPlayerOptions(), client.getPlayerMenuTypes()))
                    bus.post(new net.runelite.api.events.MenuEntryAdded(m));
                // MenuOpened runs in a session so a plugin's setMenuEntries (No Examine's shorter array)
                // persists until the game's menu is ordered or shortened to match (AndroidMenu.applyOrder).
                net.runelite.api.MenuEntry[] entries = AndroidClient.MENU.beginSession();
                try {
                    net.runelite.api.events.MenuOpened e = new net.runelite.api.events.MenuOpened();
                    e.setMenuEntries(entries);
                    for (net.runelite.api.MenuEntry m : e.getMenuEntries()) Log.i(TAG, "MenuOpened " + m);
                    bus.post(e);
                    AndroidClient.MENU.applyOrder(entries, "MenuOpened");
                } finally {
                    AndroidClient.MENU.endSession();
                }
                AndroidClient.MENU.publishPluginEntries();
                break;
            }
            case MENU_TAP: {
                // A plain tap runs the default entry (node a). As on desktop before each frame:
                // MenuEntryAdded per entry (bottom first), then PostMenuSort with the menu closed,
                // where Menu Entry Swapper reorders; the entry left on top is what the tap runs.
                if (!bus.hasSubscribers(net.runelite.api.events.PostMenuSort.class)
                        && !bus.hasSubscribers(net.runelite.api.events.MenuEntryAdded.class)
                        && !bus.hasSubscribers(net.runelite.api.events.ClientTick.class)) break;
                AndroidMenu menu = AndroidClient.MENU;
                menu.clearAdded();
                net.runelite.api.MenuEntry[] entries = menu.beginSession();
                try {
                    menu.postAdded(bus, entries);
                    AndroidMenu.postDesktopAliases(bus, entries);
                    // Desktop's ClientTick also runs with this hover menu (menu closed); plugins that swap
                    // the left-click entry there (Custom Left Click Drop) decide what the tap runs, as in
                    // AndroidMenu.presort for long-press menus.
                    AndroidMenu.presorting = true;
                    try {
                        bus.post(new net.runelite.api.events.ClientTick());
                    } finally {
                        AndroidMenu.presorting = false;
                    }
                    bus.post(new net.runelite.api.events.PostMenuSort());
                    net.runelite.api.MenuEntry[] sorted = menu.getMenuEntries();
                    net.runelite.api.MenuEntry top = sorted.length > 0 ? sorted[sorted.length - 1] : null;
                    if (top instanceof AndroidMenuEntry && !((AndroidMenuEntry) top).synthetic
                            && ((AndroidMenuEntry) top).nativeIndex >= 0 && ((AndroidMenuEntry) top).nativeIndex != a) {
                        Log.i(TAG, "tap: " + top + " moved to the top by a plugin");
                        Native.redirectMenuTap(((AndroidMenuEntry) top).nativeIndex);
                    }
                    // Plugin entries created for a widget tap (right-click options on desktop) get the strip.
                    // Only for single-op buttons (one game entry besides Cancel/Walk): those have no long-press
                    // menu to carry the entries, unlike items (Bank Tags' entries would pop up on every bank tap).
                    int gameOps = 0;
                    for (net.runelite.api.MenuEntry m : entries) {
                        if (!(m instanceof AndroidMenuEntry) || ((AndroidMenuEntry) m).synthetic) continue;
                        int tt = ((AndroidMenuEntry) m).targetType();
                        if (tt != AndroidMenuEntry.T_CANCEL && tt != AndroidMenuEntry.T_WALK) gameOps++;
                    }
                    if (gameOps == 1 && top instanceof AndroidMenuEntry && ((AndroidMenuEntry) top).targetType() == AndroidMenuEntry.T_WIDGET) {
                        net.runelite.api.Point p = client.getMouseCanvasPosition();
                        if (p != null && p.getX() >= 0) menu.publishTapEntries(p.getX(), p.getY());
                    }
                } finally {
                    menu.endSession();
                    menu.clearAdded();
                }
                break;
            }
            case SOUND_EFFECT: {
                // Every sound effect the game queues (SYNTH_SOUND, CS2 sound_synth, Client.playSoundEffect),
                // before it is queued; a consumed event drops it (verified 2026-10-17 gen 29: Music's
                // prayer-sound mute; a consumed sound is not queued).
                // unverified: source = the local player (desktop's queueSoundEffect source assumed)
                if (!bus.hasSubscribers(net.runelite.api.events.SoundEffectPlayed.class)) break;
                net.runelite.api.events.SoundEffectPlayed e = new net.runelite.api.events.SoundEffectPlayed(client.getLocalPlayer());
                e.setSoundId(a);
                e.setDelay(b);
                bus.post(e);
                if (e.isConsumed()) Native.consumeSound();
                if ("1".equals(Native.property("debug.osrsrl.soundlog")))
                    Log.i(TAG, "SoundEffectPlayed " + a + " delay " + b + (e.isConsumed() ? " consumed" : ""));
                break;
            }
            case AREA_SOUND_EFFECT: {
                // SOUND_AREA zone prots and actor/loc animation frame sounds with a range, before the
                // game queues them (inline hook on 0x4ae650); consume() drops the sound. Source: the
                // NPC/player at the animated position (animation sounds), else null as desktop's
                // packet sounds. // unverified: on the device
                if (!bus.hasSubscribers(net.runelite.api.events.AreaSoundEffectPlayed.class)) break;
                int[] s = Native.areaSound();
                if (s == null || s.length < 10) break;
                net.runelite.api.Actor src = s[6] == 1 ? npc(s[7]) : s[6] == 2 ? player(s[7]) : null;
                net.runelite.api.events.AreaSoundEffectPlayed e = new net.runelite.api.events.AreaSoundEffectPlayed(src);
                e.setSoundId(s[0]);
                e.setSceneX(s[1]);
                e.setSceneY(s[2]);
                e.setRange(s[3]);
                e.setDelay(s[4]);
                bus.post(e);
                if (e.isConsumed()) Native.consumeSound();
                if ("1".equals(Native.property("debug.osrsrl.soundlog")))
                    Log.i(TAG, "AreaSoundEffectPlayed " + s[0] + " at " + s[1] + "," + s[2] + " range " + s[3] + " delay " + s[4]
                        + " source " + (src != null ? src.getName() : "null") + (s[9] != 0 ? " (animation)" : " (packet)")
                        + (e.isConsumed() ? " consumed" : ""));
                break;
            }
            case WIDGET_DRAG: {
                // Each cycle a widget is held, before the game's drag processing (a = touch state, 4 =
                // this cycle releases it). As desktop, WidgetDrag is posted while the drag is past its
                // dead zone/time, including the release cycle, where getMouseCurrentButton reads 0 and
                // setDraggedOnWidget(null) still cancels the game's swap.
                // Verified 2026-10-17 (Spellbook reorder, inventory drag).
                // On the release cycle the dragged widget's Java onDragComplete listener runs after
                // WidgetDrag, still before the game's swap (WidgetListeners.runDragComplete).
                boolean dragSubs = bus.hasSubscribers(net.runelite.api.events.WidgetDrag.class);
                boolean dragListeners = a != 4 && WidgetListeners.hasListeners(WidgetListeners.DRAG);
                if (!dragSubs && !dragListeners && !(a == 4 && WidgetListeners.hasDragComplete())) break;
                int[] d = Native.dragState();
                if (d == null || d[4] == 0) break;
                AndroidClient ac = (AndroidClient) client;
                if (a == 4 && "1".equals(Native.property("debug.osrsrl.draglog")))
                    Log.i(TAG, "WidgetDrag release: " + d[0] + "/" + d[1] + " on " + d[2] + "/" + d[3] + " cycles " + d[5]);
                ac.dragEventTouch = a;
                try {
                    if (dragSubs) bus.post(new net.runelite.api.events.WidgetDrag());
                    if (dragListeners) WidgetListeners.runDrag(client, d[0], d[1]);  // Java onDrag
                    if (a == 4) {
                        int[] r = Native.dragState();  // the target as WidgetDrag subscribers left it
                        if (r != null) WidgetListeners.runDragComplete(client, r[0], r[1], r[2], r[3]);
                    }
                } catch (RuntimeException ex) {
                    Log.w(TAG, "drag handler", ex);
                } finally {
                    ac.dragEventTouch = -1;
                }
                break;
            }
            case MENU_OPTION_CLICKED: {
                AndroidMenuEntry[] es = AndroidMenuEntry.fromNative(Native.clickedMenuEntry());
                if (es.length == 0) break;
                net.runelite.api.events.MenuOptionClicked e = new net.runelite.api.events.MenuOptionClicked(es[0]);
                Log.i(TAG, "MenuOptionClicked " + es[0]);
                bus.post(e);
                if (e.isConsumed()) Native.consumeMenuClick();
                else if (es[0].targetType() == AndroidMenuEntry.T_WIDGET && WidgetListeners.dispatch(es[0], client))
                    Native.consumeMenuClick();
                break;
            }
            case VARBIT_CHANGED: {
                if ("1".equals(Native.property("debug.osrsrl.varlog")))
                    Log.i(TAG, "VarbitChanged varp " + a + " = " + b + " state " + client.getGameState());
                WidgetListeners.varpTransmitted(a);
                Integer prev = varpValues.put(a, b);
                int old = prev != null ? prev : 0;
                VarbitChanged e = new VarbitChanged();
                e.setVarpId(a);
                e.setVarbitId(-1);
                e.setValue(b);
                bus.post(e);
                // As desktop: one more event per varbit of this varp whose value changed.
                if (old != b) {
                    int[] bits = varbitsOf(a);
                    for (int i = 0; i + 2 < bits.length; i += 3) {
                        int lsb = bits[i + 1], msb = bits[i + 2];
                        int mask = msb - lsb >= 31 ? -1 : (1 << (msb - lsb + 1)) - 1;
                        int ov = (old >>> lsb) & mask, nv = (b >>> lsb) & mask;
                        if (ov == nv) continue;
                        VarbitChanged vb = new VarbitChanged();
                        vb.setVarpId(a);
                        vb.setVarbitId(bits[i]);
                        vb.setValue(nv);
                        bus.post(vb);
                    }
                }
                break;
            }
        }
    }

    // Last value seen per varp (VarbitChanged), and varp -> {varbit, lsb, msb, ...} built once from
    // the game's varbit definitions (game thread; stops after 4096 consecutive missing ids).
    private static final java.util.Map<Integer, Integer> varpValues = new java.util.HashMap<>();
    private static java.util.Map<Integer, int[]> varbitIndex;

    private static int[] varbitsOf(int varp) {
        if (varbitIndex == null) {
            java.util.Map<Integer, java.util.List<Integer>> m = new java.util.HashMap<>();
            long t0 = System.nanoTime();
            int missing = 0, count = 0;
            for (int id = 0; id < 1 << 16 && missing < 4096; id++) {
                int[] d = Native.varbitDef(id);
                // (An undefined id may come back as an empty definition: varp 0, bits 0..0.)
                if (d == null || (d[0] == 0 && d[1] == 0 && d[2] == 0)) { missing++; continue; }
                missing = 0;
                count++;
                java.util.List<Integer> l = m.computeIfAbsent(d[0], k -> new java.util.ArrayList<>());
                l.add(id); l.add(d[1]); l.add(d[2]);
            }
            varbitIndex = new java.util.HashMap<>();
            for (java.util.Map.Entry<Integer, java.util.List<Integer>> e : m.entrySet()) {
                int[] arr = new int[e.getValue().size()];
                for (int i = 0; i < arr.length; i++) arr[i] = e.getValue().get(i);
                varbitIndex.put(e.getKey(), arr);
            }
            Log.i(TAG, "varbit index: " + count + " varbits over " + varbitIndex.size() + " varps in "
                + (System.nanoTime() - t0) / 1000000 + " ms");
        }
        int[] r = varbitIndex.get(varp);
        return r != null ? r : new int[0];
    }

    static AndroidPlayer localPlayer() {
        return playerCache.local();
    }

    static AndroidWorldView worldView() {
        return worldView;
    }

    // --- ScriptPreFired / ScriptPostFired (native/src/script_hooks.cpp) ---

    private static boolean scriptEventsOn;

    static void updateScriptEvents() {
        EventBus bus = eventBus;
        // Proc entry/return also drives runelite_callback emulation (questFilter, bankSearchFilter,
        // musicTrackFilter, spellbookSort), so callback subscribers need the events too.
        boolean on = bus != null && (bus.hasSubscribers(ScriptPreFired.class) || bus.hasSubscribers(ScriptPostFired.class)
                || bus.hasSubscribers(net.runelite.api.events.ScriptCallbackEvent.class));
        ScriptCallbacks.update(bus);
        if (on == scriptEventsOn) return;
        scriptEventsOn = on;
        Native.setScriptEvents(on);
        Log.i(TAG, "script events " + (on ? "on" : "off"));
    }

    /**
     * From the script VM hooks, on the game thread, while the script runs. phase 0 = a proc is
     * entered (GOSUB; its arguments are on the stacks), 1 = a script returned (results on the
     * stacks), 2 = a root script starts (its ScriptEvent's arguments are available), 3/4 = before/
     * after a watched instruction (id = opcode << 16 | script, ScriptCallbacks).
     */
    private static final int BANKMAIN_FINISHBUILDING = 505, BANK_ITEM_LAYER = 12 << 16 | 12;

    public static void onScript(int phase, int id) {
        EventBus bus = eventBus;
        if (bus == null) return;
        AndroidClient ac = (AndroidClient) client;
        ac.beginScriptEvent();
        try {
            if (phase == 3 || phase == 4) {  // a watched instruction (ScriptCallbacks)
                ScriptCallbacks.onOp(bus, ac, phase, id & 0xffff, id >>> 16);
                return;
            }
            if (id == QUEST_FILTER_PROC) questFilter(bus, ac, phase);
            if (id == BANK_SEARCH_FILTER_PROC) bankSearchFilter(bus, ac, phase);
            if (id == ScriptCallbacks.MUSIC_TRACK_SHOWN) ScriptCallbacks.musicTrackFilter(bus, ac, phase);
            if (phase == 1) {
                ScriptCallbacks.onReturn(bus, ac, id);
                ac.beginScriptEvent();  // the callback's fake stacks end here
                bus.post(new ScriptPostFired(id));
            } else {
                if (phase == 2) {
                    ScriptCallbacks.onRootStart(bus, ac, id);
                    ac.beginScriptEvent();  // the callback's fake stacks end here
                }
                // Desktop's bank build re-sets each item's CS2 drag listeners, replacing Java onDragComplete
                // listeners from an earlier tag-tab layout; the mobile build sets none, so they are dropped
                // here, before plugins (Bank Tags' LayoutManager, Bank Tag Layouts) set them again.
                if (id == BANKMAIN_FINISHBUILDING) WidgetListeners.forgetDragComplete(BANK_ITEM_LAYER);
                ScriptPreFired e = new ScriptPreFired(id);
                if (phase == 2) {
                    Object[] args = Native.scriptEventArgs();
                    if (args != null) {
                        AndroidScriptEvent ev = new AndroidScriptEvent(client, args);
                        int[] src = Native.scriptEventSource();
                        if (src != null) ev.setSource(AndroidWidget.of(src[0], src[1]));
                        e.setScriptEvent(ev);
                    }
                }
                bus.post(e);
            }
        } catch (Throwable t) {
            Log.e(TAG, "script event " + id + " failed", t);
        } finally {
            AndroidWidget.applyChildEdits();
            ac.endScriptEvent();
        }
    }

    /**
     * RuneLite's patched quest filter proc 3238 (runelite-client/src/main/scripts/QuestFilter.rs2asm,
     * 8 int args, arg 0 = the quest's dbrow; returns 1 to hide the row, 0 to show it) raises
     * runelite_callback "questFilter" with int stack [..., -1, dbrow] and returns what a plugin
     * left in the -1 slot unless it is still -1. The game's proc has no callback, so it is raised
     * here when the proc returns: the dbrow is taken from its arguments at entry, and a plugin's
     * answer replaces the proc's result on the int stack (Quest List's search). Only the
     * questFilter check before the proc's own filters is lost when the answer is -1, which leaves
     * the game's result as is.
     */
    /**
     * RuneLite's patched bank search filter proc 279 (BankSearchFilter.rs2asm: args item id, an int,
     * filter string; returns 1 to show the item; bankmain_build 277 calls it for every item) starts with `iconst -1; iload 0; sload 0; runelite_callback
     * "bankSearchFilter"` and returns the plugin's answer unless it is still -1 (Bank Tags: tag tabs
     * and "tag:" searches). Raised here when the proc returns, the answer replacing its result.
     * // unverified: that the mobile bank runs 279 (the desktop id) and with these args.
     */
    static final int BANK_SEARCH_FILTER_PROC = 279;
    private static final java.util.ArrayDeque<Object[]> bankFilterArgs = new java.util.ArrayDeque<>();

    private static void bankSearchFilter(EventBus bus, AndroidClient ac, int phase) {
        // Called for every bank item on each rebuild: single-slot reads, no full stack copies.
        if (phase == 0) {
            bankFilterArgs.push(new Object[] {Native.scriptIntTop(1, false, 0), Native.scriptStringTop()});
            return;
        }
        if (phase != 1 || bankFilterArgs.isEmpty()) return;
        Object[] args = bankFilterArgs.pop();
        int item = (Integer) args[0];
        if (args[1] == null || item == Integer.MIN_VALUE || !bus.hasSubscribers(net.runelite.api.events.ScriptCallbackEvent.class)) return;
        int result = Native.scriptIntTop(0, false, 0);
        if (result == Integer.MIN_VALUE) return;
        ac.fakeStacks(new int[] {-1, item}, new Object[] {args[1]});
        net.runelite.api.events.ScriptCallbackEvent e = new net.runelite.api.events.ScriptCallbackEvent();
        e.setEventName("bankSearchFilter");
        bus.post(e);
        int answer = ac.getIntStack()[0];
        ac.beginScriptEvent();
        if (answer != -1 && answer != result) Native.scriptIntTop(0, true, answer);
    }


    static final int QUEST_FILTER_PROC = 3238;
    private static final java.util.ArrayDeque<Integer> questFilterRows = new java.util.ArrayDeque<>();

    private static void questFilter(EventBus bus, AndroidClient ac, int phase) {
        if (phase == 0) {
            int n = ac.getIntStackSize();
            int row = n >= 8 ? ac.getIntStack()[n - 8] : -1;
            questFilterRows.push(row);
            // Quest List reads the quest's name (column 2) in the callback: have it cached by then.
            if (row >= 0) ac.wantDbField(row, 2, 0);
            return;
        }
        if (phase != 1 || questFilterRows.isEmpty()) return;
        int row = questFilterRows.pop();
        if (row < 0 || !bus.hasSubscribers(net.runelite.api.events.ScriptCallbackEvent.class)) return;
        int n = ac.getIntStackSize();
        if (n < 1) return;
        int[] live = java.util.Arrays.copyOf(ac.getIntStack(), n);
        ac.fakeStacks(new int[] {-1, row}, new Object[0]);
        net.runelite.api.events.ScriptCallbackEvent e = new net.runelite.api.events.ScriptCallbackEvent();
        e.setEventName("questFilter");
        bus.post(e);
        int answer = ac.getIntStack()[0];
        ac.beginScriptEvent();  // back to the live stacks; the fake ones are not written back
        if (answer != -1 && answer != live[n - 1]) {
            live[n - 1] = answer;
            Native.setScriptIntStack(live, n);
        }
    }

    /** RuneLite's ClientThread.invoke equivalent. */
    public static void invoke(Runnable r) {
        clientThread.invoke(r);
    }

    /** Runs on the client thread, again each cycle until it returns true. */
    public static void invokeUntil(java.util.function.BooleanSupplier r) {
        clientThread.invoke(r);
    }

    private static final net.runelite.client.callback.Hooks hooks = new net.runelite.client.callback.Hooks();
    static net.runelite.client.callback.Hooks hooks() { return hooks; }
    private static boolean hiding;
    /** The host's ConfigManager (test hook `debug.osrsrl.set "config <group> <key=value>"`). */
    static volatile net.runelite.client.config.ConfigManager configManager;

    /** Renderable draw listeners (Entity Hider): actors they reject are not drawn by the game this cycle. */
    private static void applyDrawListeners() {
        AndroidWorldView wv = worldView();
        if (!hooks.hasListeners() || wv == null) {
            if (hiding) Native.setHiddenActors(new int[0], new int[0]);
            hiding = false;
            return;
        }
        int[] npcs = new int[64], players = new int[64];
        int n = 0, p = 0;
        try {
            for (net.runelite.api.NPC npc : wv.npcs()) {
                if (npc == null || hooks.draw(npc, false)) continue;
                if (n == npcs.length) npcs = java.util.Arrays.copyOf(npcs, n * 2);
                npcs[n++] = npc.getIndex();
            }
            // Graphics objects: listeners see them; Random Event Hider calls setFinished(true) on poofs,
            // which ends them in the game (AndroidGraphicsObject). A plain false from draw() hides nothing yet.
            if (client instanceof AndroidClient)
                for (AndroidGraphicsObject g : ((AndroidClient) client).graphicsObjects.all()) hooks.draw(g, false);
            for (net.runelite.api.Player pl : wv.players()) {
                if (pl == null || hooks.draw(pl, false)) continue;
                if (p == players.length) players = java.util.Arrays.copyOf(players, p * 2);
                players[p++] = pl.getId();
            }
        } catch (Throwable t) {
            Log.e(TAG, "draw listener threw", t);
        }
        if (n == 0 && p == 0 && !hiding) return;
        if ((n > 0 || p > 0) != hiding || (cycleStamp % 500) == 0) Log.i(TAG, "draw listeners hide " + n + " npcs, " + p + " players");
        Native.setHiddenActors(java.util.Arrays.copyOf(npcs, n), java.util.Arrays.copyOf(players, p));
        hiding = n > 0 || p > 0;
    }

    static void runQueued() {
        clientThread.runQueued();
    }
}
