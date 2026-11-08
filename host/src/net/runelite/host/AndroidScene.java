package net.runelite.host;

import java.util.Collections;
import java.util.List;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;

/**
 * The top-level scene: tiles with locations, ground heights, scene objects and ground items
 * (SceneObjects, re-read once per server tick).
 */
final class AndroidScene extends SceneStub {
    private static final int SIZE = 104;
    private final AndroidWorldView wv;
    private Tile[][][] tiles;
    private int tilesBase = Integer.MIN_VALUE;

    final SceneObjects objects = new SceneObjects(this);

    AndroidScene(AndroidWorldView wv) {
        this.wv = wv;
    }

    @Override
    public synchronized net.runelite.api.Tile[][][] getTiles() {
        int base = wv.getBaseX() << 16 | wv.getBaseY();
        if (tiles == null || base != tilesBase) {
            Tile[][][] t = new Tile[4][SIZE][SIZE];
            for (int z = 0; z < 4; z++)
                for (int x = 0; x < SIZE; x++)
                    for (int y = 0; y < SIZE; y++) t[z][x][y] = new Tile(x, y, z);
            tiles = t;
            tilesBase = base;
            removedTiles.clear();
        } else if (!removedTiles.isEmpty() && Native.isClientThread()) {
            // A removed tile is null until the game builds it again (the scene's reload).
            for (java.util.Iterator<int[]> it = removedTiles.iterator(); it.hasNext(); ) {
                int[] r = it.next();
                if (!Native.sceneRemoveTile(r[0], r[1], r[2], false)) continue;
                tiles[r[0]][r[1]][r[2]] = new Tile(r[1], r[2], r[0]);
                it.remove();
            }
        }
        return tiles;
    }

    private final List<int[]> removedTiles = new java.util.ArrayList<>();  // {plane, x, y}

    /** As desktop: the game's tile leaves the scene (its slot emptied) and getTiles() has null there. */
    @Override public void removeTile(net.runelite.api.Tile tile) {
        if (tile == null) return;
        if (!Native.isClientThread()) { Host.invoke(() -> removeTile(tile)); return; }
        int z = tile.getPlane(), x = tile.getSceneLocation().getX(), y = tile.getSceneLocation().getY();
        synchronized (this) {
            getTiles();
            if (!Native.sceneRemoveTile(z, x, y, true)) return;
            tiles[z][x][y] = null;
            removedTiles.add(new int[] {z, x, y});
        }
    }

    /** As desktop: the scenery object leaves every tile it covers (scene only; collision stays). */
    @Override public void removeGameObject(net.runelite.api.GameObject o) {
        if (!(o instanceof AndroidTileObject.Game)) return;
        if (Native.isClientThread()) objects.removeGame((AndroidTileObject.Game) o);
        else Host.invoke(() -> objects.removeGame((AndroidTileObject.Game) o));
    }

    @Override public net.runelite.api.Tile[][][] getExtendedTiles() { return getTiles(); }
    @Override public int[][][] getTileHeights() { return wv.getTileHeights(); }
    private byte[][][] extSettings, extSource;

    /**
     * RuneLite's extended scene: 184 x 184 with the 104-tile scene at offset 40 (Perspective indexes
     * it with that margin for the top-level view). Only the inner scene is known; the margin is 0.
     */
    @Override public synchronized byte[][][] getExtendedTileSettings() {
        byte[][][] src = wv.getTileSettings();
        if (src != extSource || extSettings == null) {
            int ext = net.runelite.api.Constants.EXTENDED_SCENE_SIZE, off = (ext - SIZE) / 2;
            byte[][][] e = new byte[src.length][ext][ext];
            for (int z = 0; z < src.length; z++)
                for (int x = 0; x < src[z].length && x < SIZE; x++)
                    System.arraycopy(src[z][x], 0, e[z][x + off], off, Math.min(SIZE, src[z][x].length));
            extSettings = e;
            extSource = src;
        }
        return extSettings;
    }
    @Override public int[] getMapRegions() { return wv.getMapRegions(); }
    // Roof removal: the mode is stored only. The mobile renderer's roof hiding isn't mapped, so
    // buildRoofs does nothing and getRoofs is empty. // unverified: default (Roof Removal starts
    // but hides no roofs)
    private volatile int roofRemovalMode;
    @Override public void setRoofRemovalMode(int mode) { roofRemovalMode = mode; }
    @Override public int getRoofRemovalMode() { return roofRemovalMode; }
    @Override public void buildRoofs() {}
    @Override public int[][][] getRoofs() { return new int[4][SIZE][SIZE]; }
    @Override public int getBaseX() { return wv.getBaseX(); }
    @Override public int getBaseY() { return wv.getBaseY(); }
    @Override public int getWorldViewId() { return -1; }
    @Override public boolean isInstance() { return AndroidWorldView.instanced(); }
    @Override public int[][][] getInstanceTemplateChunks() { return AndroidWorldView.templateChunks(); }

    // Terrain (game::scene_terrain): read on the client thread, the last value elsewhere; setters
    // write only on the client thread, as the host's other game writes.
    private int[] fields = new int[3];
    private int[] fields() {
        int[] f = Native.isClientThread() ? Native.sceneTerrain(0, 0, 0, 0) : null;
        if (f != null) fields = f;
        return fields;
    }
    @Override public int getDrawDistance() { return fields()[0]; }
    @Override public void setDrawDistance(int d) { if (Native.isClientThread()) Native.sceneSet(0, 0, 0, 0, 0, 0, d); }
    @Override public int getMinLevel() { return fields()[1]; }
    @Override public void setMinLevel(int l) { if (Native.isClientThread()) Native.sceneSet(0, 0, 0, 0, 0, 1, l); }
    @Override public byte getOverrideHue() { return (byte) fields()[2]; }
    @Override public byte getOverrideSaturation() { return (byte) (fields()[2] >> 8); }
    @Override public byte getOverrideLuminance() { return (byte) (fields()[2] >> 16); }
    @Override public byte getOverrideAmount() { return (byte) (fields()[2] >> 24); }
    @Override public net.runelite.api.Model getSkybox() {
        return Native.isClientThread() ? AndroidModel.parse(Native.sceneTerrain(6, 0, 0, 0)) : null;
    }

    // The loader's map arrays, [plane][x][y], re-read when the scene base changes.
    private final Object[] terrain = new Object[3];
    private final int[] terrainBase = {Integer.MIN_VALUE, Integer.MIN_VALUE, Integer.MIN_VALUE};
    private synchronized Object terrain(int which) {
        int base = wv.getBaseX() << 16 | wv.getBaseY();
        if (terrain[which] != null && terrainBase[which] == base || !Native.isClientThread()) return terrain[which];
        int[] a = Native.sceneTerrain(3 + which, 0, 0, 0);
        if (a == null) return terrain[which];
        int planes = a[0], sx = a[1], sy = a[2], i = 3;
        Object out;
        if (which == 2) {
            byte[][][] b = new byte[planes][sx][sy];
            for (byte[][] p : b) for (byte[] col : p) for (int y = 0; y < sy; y++) col[y] = (byte) a[i++];
            out = b;
        } else {
            short[][][] s = new short[planes][sx][sy];
            for (short[][] p : s) for (short[] col : p) for (int y = 0; y < sy; y++) col[y] = (short) a[i++];
            out = s;
        }
        terrain[which] = out;
        terrainBase[which] = base;
        return out;
    }
    @Override public short[][][] getUnderlayIds() { return (short[][][]) terrain(0); }
    @Override public short[][][] getOverlayIds() { return (short[][][]) terrain(1); }
    @Override public byte[][][] getTileShapes() { return (byte[][][]) terrain(2); }

    /**
     * A tile's paint: a copy of the game's (re-read when the tile's paint changes); colour and
     * texture setters also write the game's. Buffer fields are the caller's (GPU uploaders).
     * Detached (ptr 0) when made by Client.createSceneTilePaint.
     */
    static final class TilePaint implements net.runelite.api.SceneTilePaint {
        final long ptr;
        private final int plane, x, y;
        final int[] c;  // sw, se, ne, nw, texture, rgb
        private final boolean flat;
        private int bufferOffset, uvBufferOffset, bufferLen;

        TilePaint(long ptr, int plane, int x, int y, int sw, int se, int ne, int nw, int texture, int rgb, boolean flat) {
            this.ptr = ptr;
            this.plane = plane;
            this.x = x;
            this.y = y;
            this.c = new int[] {sw, se, ne, nw, texture, rgb};
            this.flat = flat;
        }

        private void set(int field, int v) {
            c[field] = v;
            if (ptr != 0 && Native.isClientThread()) Native.sceneSet(1, plane, x, y, ptr, field, v);
        }
        @Override public int getSwColor() { return c[0]; }
        @Override public void setSwColor(int v) { set(0, v); }
        @Override public int getSeColor() { return c[1]; }
        @Override public void setSeColor(int v) { set(1, v); }
        @Override public int getNeColor() { return c[2]; }
        @Override public void setNeColor(int v) { set(2, v); }
        @Override public int getNwColor() { return c[3]; }
        @Override public void setNwColor(int v) { set(3, v); }
        @Override public int getTexture() { return c[4]; }
        @Override public void setTexture(int v) { set(4, v); }
        @Override public int getRBG() { return c[5]; }
        @Override public boolean isFlat() { return flat; }
        @Override public int getBufferOffset() { return bufferOffset; }
        @Override public void setBufferOffset(int v) { bufferOffset = v; }
        @Override public int getUvBufferOffset() { return uvBufferOffset; }
        @Override public void setUvBufferOffset(int v) { uvBufferOffset = v; }
        @Override public int getBufferLen() { return bufferLen; }
        @Override public void setBufferLen(int v) { bufferLen = v; }
    }

    /** A shaped tile's model, copied from the game's; buffer fields are the caller's. */
    static final class TileModel implements net.runelite.api.SceneTileModel {
        final long ptr;
        private final int shape, rotation, underlay, overlay;
        private final int[] vx, vy, vz, fa, fb, fc, ca, cb, cc, tex;
        private final boolean flat;
        private int bufferOffset, uvBufferOffset, bufferLen;

        TileModel(int[] m) {
            ptr = (m[0] & 0xffffffffL) | (long) m[1] << 32;
            shape = m[2];
            rotation = m[3];
            underlay = m[4] & 0xffffff;  // the game keeps ARGB; desktop's minimap colours are RGB
            overlay = m[5] & 0xffffff;
            int texture = m[6], mask = m[7], nv = m[8], nf = m[9], i = 10;
            vx = new int[nv]; vy = new int[nv]; vz = new int[nv];
            for (int v = 0; v < nv; v++) { vx[v] = m[i++]; vy[v] = m[i++]; vz[v] = m[i++]; }
            fa = new int[nf]; fb = new int[nf]; fc = new int[nf];
            for (int f = 0; f < nf; f++) { fa[f] = m[i++]; fb[f] = m[i++]; fc[f] = m[i++]; }
            ca = new int[nf]; cb = new int[nf]; cc = new int[nf];
            for (int f = 0; f < nf; f++) { ca[f] = m[i++]; cb[f] = m[i++]; cc[f] = m[i++]; }
            // As the desktop client: only overlay triangles carry the overlay's texture, none without one.
            tex = texture == -1 ? null : new int[nf];
            if (tex != null) for (int f = 0; f < nf; f++) tex[f] = (mask >>> f & 1) != 0 ? texture : -1;
            boolean level = true;
            for (int v = 1; v < nv; v++) level &= vy[v] == vy[0];
            flat = level;
        }

        @Override public int getModelUnderlay() { return underlay; }
        @Override public int getModelOverlay() { return overlay; }
        @Override public int getShape() { return shape; }
        @Override public int getRotation() { return rotation; }
        @Override public int[] getFaceX() { return fa; }
        @Override public int[] getFaceY() { return fb; }
        @Override public int[] getFaceZ() { return fc; }
        @Override public int[] getVertexX() { return vx; }
        @Override public int[] getVertexY() { return vy; }
        @Override public int[] getVertexZ() { return vz; }
        @Override public int[] getTriangleColorA() { return ca; }
        @Override public int[] getTriangleColorB() { return cb; }
        @Override public int[] getTriangleColorC() { return cc; }
        @Override public int[] getTriangleTextureId() { return tex; }
        @Override public boolean isFlat() { return flat; }
        @Override public int getBufferOffset() { return bufferOffset; }
        @Override public void setBufferOffset(int v) { bufferOffset = v; }
        @Override public int getUvBufferOffset() { return uvBufferOffset; }
        @Override public void setUvBufferOffset(int v) { uvBufferOffset = v; }
        @Override public int getBufferLen() { return bufferLen; }
        @Override public void setBufferLen(int v) { bufferLen = v; }
    }

    final class Tile extends TileStub {
        private final int x, y, z;

        Tile(int x, int y, int z) {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        @Override public WorldPoint getWorldLocation() { return new WorldPoint(wv.getBaseX() + x, wv.getBaseY() + y, z); }
        @Override public net.runelite.api.Point getSceneLocation() { return new net.runelite.api.Point(x, y); }
        @Override public LocalPoint getLocalLocation() { return LocalPoint.fromScene(x, y, wv); }
        @Override public int getPlane() { return z; }
        @Override public int getRenderLevel() { return z; }
        private SceneObjects.Contents contents() { return objects.at(z, x, y); }
        @Override public List<net.runelite.api.TileItem> getGroundItems() {
            SceneObjects.Contents c = contents();
            return c != null && c.items != null ? Collections.unmodifiableList(c.items) : null;
        }
        @Override public net.runelite.api.GameObject[] getGameObjects() {
            SceneObjects.Contents c = contents();
            return c == null ? new net.runelite.api.GameObject[0] : c.games.toArray(new net.runelite.api.GameObject[0]);
        }
        @Override public net.runelite.api.WallObject getWallObject() {
            SceneObjects.Contents c = contents();
            return c != null ? c.wall : null;
        }
        @Override public net.runelite.api.GroundObject getGroundObject() {
            SceneObjects.Contents c = contents();
            return c != null ? c.ground : null;
        }
        /** Only null (remove) is supported: the game's scene can't take a host-made object. */
        @Override public void setGroundObject(net.runelite.api.GroundObject g) {
            if (g == null) {
                objects.removeGround(z, x, y);
                return;
            }
            if (g != getGroundObject()) throw new UnsupportedOperationException("Tile.setGroundObject(non-null)");
        }
        @Override public net.runelite.api.DecorativeObject getDecorativeObject() {
            SceneObjects.Contents c = contents();
            return c != null ? c.decorative : null;
        }
        @Override public net.runelite.api.ItemLayer getItemLayer() {
            SceneObjects.Contents c = contents();
            return c != null ? c.itemLayer : null;
        }
        @Override public net.runelite.api.Tile getBridge() { return null; }

        // The same wrapper while the game's paint/model stays; off the client thread the last one.
        private TilePaint paint;
        private TileModel model;
        @Override public net.runelite.api.SceneTilePaint getSceneTilePaint() {
            if (!Native.isClientThread()) return paint;
            int[] p = Native.sceneTerrain(1, z, x, y);
            long ptr = p == null ? 0 : (p[0] & 0xffffffffL) | (long) p[1] << 32;
            if (ptr == 0) paint = null;
            else if (paint == null || paint.ptr != ptr) paint = new TilePaint(ptr, z, x, y, p[2], p[3], p[4], p[5], p[6], p[7] & 0xffffff, p[8] != 0);
            else { System.arraycopy(p, 2, paint.c, 0, 6); paint.c[5] &= 0xffffff; }  // same address after a same-base rebuild: fresh values
            return paint;
        }
        @Override public net.runelite.api.SceneTileModel getSceneTileModel() {
            if (!Native.isClientThread()) return model;
            int[] m = Native.sceneTerrain(2, z, x, y);
            long ptr = m == null ? 0 : (m[0] & 0xffffffffL) | (long) m[1] << 32;
            if (ptr == 0) model = null;
            else if (model == null || model.ptr != ptr) model = new TileModel(m);
            return model;
        }
        /** Only the tile's own paint/model (a no-op): the game's tile owns and frees them. */
        @Override public void setSceneTilePaint(net.runelite.api.SceneTilePaint p) {
            if (p != getSceneTilePaint()) throw new UnsupportedOperationException("Tile.setSceneTilePaint(another paint)");
        }
        @Override public void setSceneTileModel(net.runelite.api.SceneTileModel m) {
            if (m != getSceneTileModel()) throw new UnsupportedOperationException("Tile.setSceneTileModel(another model)");
        }
    }
}
