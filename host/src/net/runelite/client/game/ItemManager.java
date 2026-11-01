package net.runelite.client.game;

import java.awt.Color;
import java.awt.image.BufferedImage;
import java.util.Collections;
import java.util.List;
import javax.inject.Inject;
import net.runelite.api.Client;
import net.runelite.api.SpritePixels;
import net.runelite.api.GameState;
import net.runelite.api.ItemComposition;
import net.runelite.client.util.AsyncBufferedImage;

/**
 * Replaces RuneLite's ItemManager (same name). Definitions come from the client; images are the
 * game's own item icons. Prices come from RuneLite's price API like desktop's (ItemClient's
 * read-only GET of `<api base>/item/prices.js`, refreshed every 30 minutes). In the OpenRune build
 * NetGuard refuses it (off-server): one failed fetch, no retry, and an item's store value stands in, so value thresholds still order items sensibly.
 */
@javax.inject.Singleton
public class ItemManager {
    private final Client client;
    private final net.runelite.client.callback.ClientThread clientThread;
    private final java.util.Map<String, AsyncBufferedImage> images = java.util.Collections.synchronizedMap(new LRU<String, AsyncBufferedImage>(512));
    private final java.util.Map<String, BufferedImage> outlines = java.util.Collections.synchronizedMap(new LRU<String, BufferedImage>(128));

    @Inject
    public ItemManager(Client client, net.runelite.client.callback.ClientThread clientThread,
                       okhttp3.OkHttpClient http, @javax.inject.Named("runelite.api.base") okhttp3.HttpUrl apiBase,
                       com.google.gson.Gson gson, net.runelite.client.config.ConfigManager configManager) {
        ItemManager.configManager = configManager;
        this.client = client;
        this.clientThread = clientThread;
        startIndexBuild();
        startPriceFetch(http, apiBase, gson);
    }

    // --- Prices (shared by every ItemManager instance; plugins get their own) ---
    private static volatile java.util.Map<Integer, net.runelite.http.api.item.ItemPrice> itemPrices;
    private static boolean priceFetchStarted;

    private static void startPriceFetch(final okhttp3.OkHttpClient http, final okhttp3.HttpUrl apiBase, final com.google.gson.Gson gson) {
        synchronized (ItemManager.class) {
            if (priceFetchStarted || http == null || apiBase == null) return;
            priceFetchStarted = true;
        }
        Thread t = new Thread(() -> {
            // Desktop refreshes every 30 min; a failure (NetGuard refuses runelite.net off-server) ends the thread: no retry.
            while (fetchPrices(http, apiBase, gson)) {
                try {
                    Thread.sleep(30 * 60_000L);
                } catch (InterruptedException e) {
                    return;
                }
            }
        }, "item-prices");
        t.setDaemon(true);
        t.start();
    }

    private static boolean fetchPrices(okhttp3.OkHttpClient http, okhttp3.HttpUrl apiBase, com.google.gson.Gson gson) {
        okhttp3.HttpUrl url = apiBase.newBuilder().addPathSegment("item").addPathSegment("prices.js").build();
        okhttp3.Request req = new okhttp3.Request.Builder().url(url).build();
        try (okhttp3.Response r = http.newCall(req).execute()) {
            if (!r.isSuccessful() || r.body() == null) {
                android.util.Log.w("osrsrl-host", "item prices: HTTP " + r.code());
                return false;
            }
            net.runelite.http.api.item.ItemPrice[] prices = gson.fromJson(
                    new java.io.InputStreamReader(r.body().byteStream(), java.nio.charset.StandardCharsets.UTF_8),
                    net.runelite.http.api.item.ItemPrice[].class);
            if (prices == null || prices.length == 0) return false;
            java.util.Map<Integer, net.runelite.http.api.item.ItemPrice> m = new java.util.HashMap<>(prices.length * 2);
            for (net.runelite.http.api.item.ItemPrice p : prices) m.put(p.getId(), p);
            itemPrices = java.util.Collections.unmodifiableMap(m);
            android.util.Log.i("osrsrl-host", "item prices: " + m.size() + " from " + url.host());
            return true;
        } catch (Exception e) {
            android.util.Log.w("osrsrl-host", "item prices: " + e);
            return false;
        }
    }

    /** True once RuneLite's price list has loaded (prices are live, not store values). */
    public static boolean hasLivePrices() { return itemPrices != null; }

    // Desktop ItemManager.WORN_ITEMS (1.13.1 <clinit>): worn variant -> the tradeable item it prices as.
    private static final java.util.Map<Integer, Integer> WORN_ITEMS = new java.util.HashMap<>();
    static {
        int[] kv = {
            89, 88, 10554, 10553, 11851, 11850, 11853, 11852, 11855, 11854, 11857, 11856, 11859, 11858, 11861,
            11860, 13580, 13579, 13582, 13581, 13584, 13583, 13586, 13585, 13588, 13587, 13590, 13589, 13592,
            13591, 13594, 13593, 13596, 13595, 13598, 13597, 13600, 13599, 13602, 13601, 13604, 13603, 13606,
            13605, 13608, 13607, 13610, 13609, 13612, 13611, 13614, 13613, 13616, 13615, 13618, 13617, 13620,
            13619, 13622, 13621, 13624, 13623, 13626, 13625, 13628, 13627, 13630, 13629, 13632, 13631, 13634,
            13633, 13636, 13635, 13638, 13637, 13668, 13667, 13670, 13669, 13672, 13671, 13674, 13673, 13676,
            13675, 13678, 13677, 21063, 21061, 21066, 21064, 21069, 21067, 21072, 21070, 21075, 21073, 21078,
            21076, 24745, 24743, 24748, 24746, 24751, 24749, 24754, 24752, 24757, 24755, 24760, 24758, 25071,
            25069, 25074, 25072, 25077, 25075, 25080, 25078, 25083, 25081, 25086, 25084, 27446, 27444, 27449,
            27447, 27452, 27450, 27455, 27453, 27458, 27456, 27461, 27459, 30047, 30045, 30050, 30048, 30053,
            30051, 30056, 30054, 30059, 30057, 30062, 30060, 13342, 13280, 10073, 10069, 10074, 10071, 13341,
            9772, 13340, 9771, 28839, 10053, 28842, 10055, 28845, 10057, 28848, 10059, 28851, 10061, 28854,
            10063, 28857, 10065, 28860, 10067};
        for (int i = 0; i + 1 < kv.length; i += 2) WORN_ITEMS.put(kv[i], kv[i + 1]);
    }

    private static volatile net.runelite.client.config.ConfigManager configManager;

    private static boolean useWikiPrices() {
        net.runelite.client.config.ConfigManager cm = configManager;
        String v = cm != null ? cm.getConfiguration("runelite", "useWikiItemPrices") : null;
        return v == null || Boolean.parseBoolean(v);  // RuneLiteConfig default: true
    }

    private static final class LRU<K, V> extends java.util.LinkedHashMap<K, V> {
        private final int max;
        LRU(int max) { super(16, 0.75f, true); this.max = max; }
        @Override protected boolean removeEldestEntry(java.util.Map.Entry<K, V> e) { return size() > max; }
    }

    public ItemComposition getItemComposition(int id) { return client.getItemDefinition(id); }
    public int canonicalize(int id) {
        ItemComposition c = client.getItemDefinition(id);
        if (c == null) return id;
        if (c.getNote() != -1) return c.getLinkedNoteId();
        if (c.getPlaceholderTemplateId() != -1) return c.getPlaceholderId();
        return id;
    }
    /**
     * No price API on the device: coins and platinum tokens have their fixed value, everything else
     * its store value (ItemComposition.getPrice), so value thresholds still order items sensibly.
     */
    public long getItemPrice(int id) { return getItemPriceWithSource(id, useWikiPrices()); }

    /** As desktop: noted -> unnoted, worn -> tradeable, then the sum of the item's ItemMapping parts
     *  if it has any, else the price list. Store value only while the price list hasn't loaded. */
    public long getItemPriceWithSource(int id, boolean useWiki) {
        if (id == 995) return 1;          // ItemID.COINS_995
        if (id == 13204) return 1000;     // ItemID.PLATINUM_TOKEN
        java.util.Map<Integer, net.runelite.http.api.item.ItemPrice> prices = itemPrices;
        if (prices == null) {
            ItemComposition c = client.getItemDefinition(canonicalize(id));
            return c != null ? c.getPrice() : 0;
        }
        ItemComposition c = client.getItemDefinition(id);
        if (c != null && c.getNote() != -1) id = c.getLinkedNoteId();
        Integer worn = WORN_ITEMS.get(id);
        if (worn != null) id = worn;
        java.util.Collection<net.runelite.client.game.ItemMapping> parts = net.runelite.client.game.ItemMapping.map(id);
        if (parts == null) {  // as desktop: the item's own price only when it has no mapping
            net.runelite.http.api.item.ItemPrice ip = prices.get(id);
            return ip == null ? 0 : useWiki ? getWikiPrice(ip) : ip.getPrice();
        }
        long price = 0;
        for (net.runelite.client.game.ItemMapping m : parts)
            price += getItemPriceWithSource(m.getTradeableItem(), useWiki) * m.getQuantity();
        return price;
    }
    /**
     * Desktop searches its price list (tradeable items) by name. Here the list is built from the
     * game's item definitions: a pass over all ids after login, 4096 ids per client cycle (~180 ms
     * in all on the emulator, so no single long frame); a search on the client thread before it is
     * done finishes the pass at once. Off the client thread before then, the result is empty.
     */
    public List<net.runelite.http.api.item.ItemPrice> search(String query) {
        List<net.runelite.http.api.item.ItemPrice> idx = priceIndex();
        String q = query.toLowerCase();
        List<net.runelite.http.api.item.ItemPrice> out = new java.util.ArrayList<>();
        for (net.runelite.http.api.item.ItemPrice p : idx)
            if (p.getName().toLowerCase().contains(q)) out.add(withPrices(p));
        return out;
    }

    /** The live price list's entry when loaded (desktop searches that list), else the index's store-value entry. */
    private static net.runelite.http.api.item.ItemPrice withPrices(net.runelite.http.api.item.ItemPrice p) {
        java.util.Map<Integer, net.runelite.http.api.item.ItemPrice> prices = itemPrices;
        net.runelite.http.api.item.ItemPrice live = prices != null ? prices.get(p.getId()) : null;
        return live != null ? live : p;
    }

    /** Desktop prefers the wiki price when there is one. */
    public long getWikiPrice(net.runelite.http.api.item.ItemPrice p) {
        return p.getWikiPrice() > 0 ? p.getWikiPrice() : p.getPrice();
    }

    // Shared by every ItemManager instance (plugins get their own; desktop has one singleton).
    private static volatile List<net.runelite.http.api.item.ItemPrice> priceIndex;
    private static boolean indexBuildStarted;
    private static final List<net.runelite.http.api.item.ItemPrice> building = new java.util.ArrayList<>();
    private static int buildNext;
    private static long buildNanos;

    private void startIndexBuild() {
        synchronized (ItemManager.class) {
            if (indexBuildStarted) return;
            indexBuildStarted = true;
        }
        clientThread.invoke(() -> {
            if (priceIndex != null) return true;
            if (client.getGameState() != GameState.LOGGED_IN) return false;
            indexStep(4096);
            return priceIndex != null;
        });
    }

    /** Client thread: indexes up to `max` more ids; publishes the index when all are done. */
    private void indexStep(int max) {
        long t0 = System.nanoTime();
        int n = client.getItemCount();
        int end = (int) Math.min((long) n, (long) buildNext + max);
        for (int id = buildNext; id < end; id++) {
            ItemComposition c = client.getItemDefinition(id);
            if (c == null || !c.isTradeable() || c.getNote() != -1 || c.getPlaceholderTemplateId() != -1) continue;
            String name = c.getName();
            if (name == null || name.isEmpty() || name.equals("null")) continue;
            net.runelite.http.api.item.ItemPrice p = new net.runelite.http.api.item.ItemPrice();
            p.setId(id);
            p.setName(name);
            p.setPrice(getItemPrice(id));
            p.setWikiPrice(p.getPrice());
            building.add(p);
        }
        buildNext = end;
        buildNanos += System.nanoTime() - t0;
        if (buildNext >= n) {
            priceIndex = Collections.unmodifiableList(new java.util.ArrayList<>(building));
            building.clear();
            android.util.Log.i("osrsrl-host", "item search index: " + priceIndex.size() + " of " + n + " items in "
                + buildNanos / 1000000 + " ms");
        }
    }

    private List<net.runelite.http.api.item.ItemPrice> priceIndex() {
        List<net.runelite.http.api.item.ItemPrice> idx = priceIndex;
        if (idx != null) return idx;
        if (!client.isClientThread() || client.getGameState().getState() < GameState.LOGIN_SCREEN.getState())
            return Collections.emptyList();
        indexStep(Integer.MAX_VALUE);
        return priceIndex != null ? priceIndex : Collections.<net.runelite.http.api.item.ItemPrice>emptyList();
    }

    public ItemStats getItemStats(int id) { return null; }
    public net.runelite.http.api.item.ItemStats getItemStats(int id, boolean allowNote) { return null; }

    public AsyncBufferedImage getImage(int id) { return getImage(id, 1, false); }
    /**
     * The game's inventory icon (Client.createItemSprite, rendered on the game thread like
     * RuneLite's loader); blank until loaded, then onLoaded listeners run. Gives up after ~10 s
     * (unknown ids).
     */
    public AsyncBufferedImage getImage(int id, int quantity, boolean stackable) {
        String key = id + ":" + quantity + ":" + stackable;
        AsyncBufferedImage img = images.get(key);
        if (img != null) return img;
        AsyncBufferedImage fresh = new AsyncBufferedImage(clientThread, 36, 32, BufferedImage.TYPE_INT_ARGB);
        images.put(key, fresh);
        final int[] tries = {0};
        clientThread.invoke(() -> {
            if (client.getGameState().ordinal() < GameState.LOGIN_SCREEN.ordinal()) return false;
            SpritePixels sprite = client.createItemSprite(id, quantity, 1, 3153952, stackable ? 1 : 0, false, 512);
            if (sprite == null) return ++tries[0] > 500;
            sprite.toBufferedImage(fresh);
            fresh.loaded();
            return true;
        });
        return fresh;
    }

    /**
     * Outline of the item's icon (Inventory Tags, Quest Helper). Rendered on the game thread; until
     * then (or off it) a blank image is returned and not cached.
     */
    public BufferedImage getItemOutline(int id, int quantity, Color color) {
        String key = id + ":" + quantity + ":" + (color != null ? color.getRGB() : 0);
        BufferedImage cached = outlines.get(key);
        if (cached != null) return cached;
        SpritePixels sprite = client.createItemSprite(id, quantity, 1, 0, 0, false, 512);
        if (sprite == null) {
            if (!clientThread.isClientThread()) clientThread.invokeLater(() -> { getItemOutline(id, quantity, color); });
            return new BufferedImage(38, 34, BufferedImage.TYPE_INT_ARGB);
        }
        BufferedImage out = sprite.toBufferedOutline(color);
        outlines.put(key, out);
        return out;
    }
}
