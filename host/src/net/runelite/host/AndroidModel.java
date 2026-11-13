package net.runelite.host;

import net.runelite.api.AABB;
import net.runelite.api.Model;
import net.runelite.api.Node;
import net.runelite.api.Perspective;

/**
 * A lit model as desktop's Model: vertices in model space (local units, y negative = up), faces, lit
 * face colours (colours3 -1 = flat, -2 = hidden) and the per-face arrays the game keeps. Built from the
 * game's posed actor Model (Native.actorMesh, NPC/Player.getModel) or by AndroidModelData.light
 * (Client.loadModel). Bounds and the per-orientation AABB follow the game's own code (bounds in the
 * renderer 0x73ba8c, AABB 0x71ae98; layout notes at game.cpp pack_mesh).
 */
final class AndroidModel implements Model {
    int verts, faces;
    float[] x, y, z;
    int[] a, b, c, c1, c2, c3;
    short[] unlit, textures;
    byte[] priorities, alpha, textureFaces;
    int[] tex1, tex2, tex3;
    int[] normalsX, normalsY, normalsZ;
    byte priority;
    boolean singleTile;
    int id = -1;  // the model store id of a loaded model (Client.applyTransformations re-poses it natively)
    private int sceneId, bufferOffset, uvBufferOffset;
    private int height, bottomY, xzMag, radius, diameter;
    private boolean bounds;
    private final java.util.HashMap<Integer, Box> aabbs = new java.util.HashMap<>();

    /** The packed mesh of a game Model or ModelData (game.cpp pack_mesh); null if absent. */
    static AndroidModel parse(int[] m) {
        if (m == null || m.length < 7) return null;
        AndroidModel r = new AndroidModel();
        Packed p = new Packed(m);
        r.verts = p.n;
        r.faces = p.f;
        r.x = p.floats(r.verts);
        r.y = p.floats(r.verts);
        r.z = p.floats(r.verts);
        r.a = p.ints(r.faces);
        r.b = p.ints(r.faces);
        r.c = p.ints(r.faces);
        if (p.has(1)) {
            r.c1 = p.ints(r.faces);
            r.c2 = p.ints(r.faces);
            r.c3 = p.ints(r.faces);
        } else {
            r.c1 = new int[r.faces];
            r.c2 = new int[r.faces];
            r.c3 = new int[r.faces];
            java.util.Arrays.fill(r.c3, -1);
        }
        if (p.has(2)) r.unlit = p.shorts(r.faces);
        if (p.has(4)) p.skip(r.faces);  // render types: a lit Model encodes them in colours3
        if (p.has(8)) r.priorities = p.bytes(r.faces);
        if (p.has(16)) r.alpha = p.bytes(r.faces);
        if (p.has(32)) r.textures = p.shorts(r.faces);
        if (p.has(64)) r.textureFaces = p.bytes(r.faces);
        if (p.has(128)) p.skip(p.t);
        if (p.t > 0) {
            r.tex1 = p.ints(p.t);
            r.tex2 = p.ints(p.t);
            r.tex3 = p.ints(p.t);
        }
        r.priority = (byte) p.priority;
        r.singleTile = p.singleTile;
        r.height = p.height;
        return r;
    }

    /** Cursor over a pack_mesh int[]: {height, n, f, flags, t, priority, singleTile, ...}. */
    static final class Packed {
        final int[] m;
        final int height, n, f, flags, t, priority;
        final boolean singleTile;
        int o = 7;

        Packed(int[] m) {
            this.m = m;
            height = m[0];
            n = m[1];
            f = m[2];
            flags = m[3];
            t = m[4];
            priority = m[5];
            singleTile = m[6] != 0;
        }

        boolean has(int bit) { return (flags & bit) != 0; }
        void skip(int k) { o += k; }

        float[] floats(int k) {
            float[] r = new float[k];
            for (int i = 0; i < k; i++) r[i] = m[o + i];
            o += k;
            return r;
        }

        int[] ints(int k) {
            int[] r = java.util.Arrays.copyOfRange(m, o, o + k);
            o += k;
            return r;
        }

        short[] shorts(int k) {
            short[] r = new short[k];
            for (int i = 0; i < k; i++) r[i] = (short) m[o + i];
            o += k;
            return r;
        }

        byte[] bytes(int k) {
            byte[] r = new byte[k];
            for (int i = 0; i < k; i++) r[i] = (byte) m[o + i];
            o += k;
            return r;
        }
    }

    private static int cacheStamp = -1;
    private static final java.util.HashMap<Integer, AndroidModel> cache = new java.util.HashMap<>();
    private static final AndroidModel NONE = new AndroidModel();

    /**
     * The posed model of an NPC (player=false) or player, or null (off the game thread, no model).
     * Read once per game cycle per actor (overlays ask every frame, several times per actor).
     */
    static AndroidModel of(boolean player, int index) {
        if (!Native.isClientThread()) return null;
        if (cacheStamp != Host.cycleStamp) { cache.clear(); cacheStamp = Host.cycleStamp; }
        Integer key = player ? ~index : index;
        AndroidModel r = cache.get(key);
        if (r == null) {
            r = parse(Native.actorMesh(player, index));
            if (r == null) r = NONE;
            cache.put(key, r);
        }
        return r == NONE ? null : r;
    }

    /**
     * Desktop's Client.applyTransformations: a copy posed by sequence a at frame fa (and b at fb) through the
     * game's animate (Native.poseModel), from this model's current vertices; its colours stay this model's.
     * Null for a model the game can't rebuild (not from Client.loadModel/loadModelData, or merged) and for a
     * skeletal sequence or a frame out of range.
     */
    AndroidModel transformed(net.runelite.api.Animation a, int fa, net.runelite.api.Animation b, int fb) {
        if (id < 0 || a == null) return null;
        int[] xyz = new int[3 * verts];
        for (int i = 0; i < verts; i++) { xyz[i] = (int) x[i]; xyz[verts + i] = (int) y[i]; xyz[2 * verts + i] = (int) z[i]; }
        AndroidModel p = parse(Native.poseModel(id, xyz, a.getId(), fa, b != null ? b.getId() : -1, b != null ? fb : -1));
        if (p == null || p.verts != verts || p.faces != faces) return null;
        p.id = id;
        p.c1 = c1; p.c2 = c2; p.c3 = c3; p.unlit = unlit; p.textures = textures; p.priorities = priorities;
        p.textureFaces = textureFaces; p.tex1 = tex1; p.tex2 = tex2; p.tex3 = tex3; p.priority = priority;
        if (p.alpha == null) p.alpha = alpha;
        return p;
    }

    // ---- Mesh ----

    @Override public int getVerticesCount() { return verts; }
    @Override public float[] getVerticesX() { return x; }
    @Override public float[] getVerticesY() { return y; }
    @Override public float[] getVerticesZ() { return z; }
    @Override public int getFaceCount() { return faces; }
    @Override public int[] getFaceIndices1() { return a; }
    @Override public int[] getFaceIndices2() { return b; }
    @Override public int[] getFaceIndices3() { return c; }
    // null when the model has no alpha / textures, as desktop (callers null-check them).
    @Override public byte[] getFaceTransparencies() { return alpha; }
    @Override public short[] getFaceTextures() { return textures; }

    @Override public Model rotateY90Ccw() {
        for (int i = 0; i < verts; i++) { float t = x[i]; x[i] = z[i]; z[i] = -t; }
        return changed();
    }

    @Override public Model rotateY180Ccw() {
        for (int i = 0; i < verts; i++) { x[i] = -x[i]; z[i] = -z[i]; }
        return changed();
    }

    @Override public Model rotateY270Ccw() {
        for (int i = 0; i < verts; i++) { float t = z[i]; z[i] = x[i]; x[i] = -t; }
        return changed();
    }

    @Override public Model translate(int dx, int dy, int dz) {
        for (int i = 0; i < verts; i++) { x[i] += dx; y[i] += dy; z[i] += dz; }
        return changed();
    }

    @Override public Model scale(int sx, int sy, int sz) {
        for (int i = 0; i < verts; i++) {
            x[i] = (int) x[i] * sx / 128;
            y[i] = (int) y[i] * sy / 128;
            z[i] = (int) z[i] * sz / 128;
        }
        return changed();
    }

    private Model changed() {
        bounds = false;
        aabbs.clear();
        normalsX = normalsY = normalsZ = null;
        return this;
    }

    // ---- Model ----

    @Override public int[] getFaceColors1() { return c1; }
    @Override public int[] getFaceColors2() { return c2; }
    @Override public int[] getFaceColors3() { return c3; }
    // The ModelData's HSL colours for a loaded model; null for a posed actor model (the game's Model keeps none).
    @Override public short[] getUnlitFaceColors() { return unlit; }
    // GPU-plugin bookkeeping: plain fields, as desktop.
    @Override public int getSceneId() { return sceneId; }
    @Override public void setSceneId(int v) { sceneId = v; }
    @Override public int getBufferOffset() { return bufferOffset; }
    @Override public void setBufferOffset(int v) { bufferOffset = v; }
    @Override public int getUvBufferOffset() { return uvBufferOffset; }
    @Override public void setUvBufferOffset(int v) { uvBufferOffset = v; }
    @Override public byte getTransparency() { return 0; }  // model-wide alpha: the game's light pass sets none
    @Override public byte[] getFaceRenderPriorities() { return priorities; }  // null when the model priority is uniform
    @Override public byte[] getFaceBias() { return null; }  // no per-face bias in the game's Model or ModelData
    // Colour overrides (NPC tints): the game's Model has none outside its draw call.
    @Override public byte getOverrideAmount() { return 0; }
    @Override public byte getOverrideHue() { return 0; }
    @Override public byte getOverrideSaturation() { return 0; }
    @Override public byte getOverrideLuminance() { return 0; }
    @Override public byte[] getTextureFaces() { return textureFaces; }
    @Override public int[] getTexIndices1() { return tex1; }
    @Override public int[] getTexIndices2() { return tex2; }
    @Override public int[] getTexIndices3() { return tex3; }
    @Override public Model getUnskewedModel() { return this; }  // never skewed: no ground contouring here
    @Override public boolean useBoundingBox() { return singleTile; }

    /** Bounds as the game's renderer computes them (0x73ba8c): height, bottomY, xz magnitude, radius, diameter. */
    @Override public void calculateBoundsCylinder() {
        if (bounds) return;
        bounds = true;
        int h = 0, bottom = 0;
        long mag = 0;
        for (int i = 0; i < verts; i++) {
            int vx = (int) x[i], vy = (int) y[i], vz = (int) z[i];
            if (-vy > h) h = -vy;
            if (vy > bottom) bottom = vy;
            long m = (long) vx * vx + (long) vz * vz;
            if (m > mag) mag = m;
        }
        height = h;
        bottomY = bottom;
        xzMag = (int) (Math.sqrt(mag) + 0.99);
        radius = (int) (Math.sqrt((double) xzMag * xzMag + (double) h * h) + 0.99);
        diameter = radius + (int) (Math.sqrt((double) xzMag * xzMag + (double) bottom * bottom) + 0.99) + 1;
    }

    @Override public int getBottomY() { calculateBoundsCylinder(); return bottomY; }
    @Override public int getRadius() { calculateBoundsCylinder(); return radius; }
    @Override public int getDiameter() { calculateBoundsCylinder(); return diameter; }
    @Override public int getXYZMag() { calculateBoundsCylinder(); return xzMag; }

    /** The axis-aligned box of the model rotated by orientation (0..2047), as the game's 0x71ae98. */
    @Override public void calculateExtreme(int orientation) {
        orientation &= 2047;
        if (aabbs.containsKey(orientation)) return;
        int sin = Perspective.SINE[orientation], cos = Perspective.COSINE[orientation];
        int minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
        for (int i = 0; i < verts; i++) {
            int vx = (int) x[i], vy = (int) y[i], vz = (int) z[i];
            int rx = (vx * cos + vz * sin) >> 16, rz = (vz * cos - vx * sin) >> 16;
            minX = Math.min(minX, rx); maxX = Math.max(maxX, rx);
            minY = Math.min(minY, vy); maxY = Math.max(maxY, vy);
            minZ = Math.min(minZ, rz); maxZ = Math.max(maxZ, rz);
        }
        int ex = Math.max(32, (maxX - minX + 1) >> 1), ez = Math.max(32, (maxZ - minZ + 1) >> 1);
        if (singleTile) { ex += 8; ez += 8; }
        aabbs.put(orientation, new Box((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2, ex, (maxY - minY + 1) / 2, ez));
    }

    @Override public AABB getAABB(int orientation) {
        calculateExtreme(orientation);
        return aabbs.get(orientation & 2047);
    }

    static final class Box implements AABB {
        final int cx, cy, cz, ex, ey, ez;

        Box(int cx, int cy, int cz, int ex, int ey, int ez) {
            this.cx = cx; this.cy = cy; this.cz = cz; this.ex = ex; this.ey = ey; this.ez = ez;
        }

        @Override public int getCenterX() { return cx; }
        @Override public int getCenterY() { return cy; }
        @Override public int getCenterZ() { return cz; }
        @Override public int getExtremeX() { return ex; }
        @Override public int getExtremeY() { return ey; }
        @Override public int getExtremeZ() { return ez; }
        @Override public String toString() { return "AABB[" + cx + "," + cy + "," + cz + " +-" + ex + "," + ey + "," + ez + "]"; }
    }

    /** Vertex normals: the game's normal pass (0x72a0e0) on this geometry. */
    private void normals() {
        if (normalsX != null) return;
        int[][] n = AndroidModelData.normals(verts, faces, x, y, z, a, b, c, null);
        normalsX = n[0];
        normalsY = n[1];
        normalsZ = n[2];
    }

    @Override public int[] getVertexNormalsX() { normals(); return normalsX; }
    @Override public int[] getVertexNormalsY() { normals(); return normalsY; }
    @Override public int[] getVertexNormalsZ() { normals(); return normalsZ; }

    // Desktop rasterises into its software 3D buffer; the Android game draws with GL and has none, so these
    // return without drawing.
    @Override public void drawFrustum(int a, int b, int c, int d, int e, int f, int g) {}
    @Override public void drawOrtho(int a, int b, int c, int d, int e, int f, int g, int h) {}

    // ---- Renderable / Node ----

    @Override public Model getModel() { return this; }
    @Override public int getModelHeight() { if (height == 0) calculateBoundsCylinder(); return height; }
    @Override public void setModelHeight(int h) { height = h; }
    @Override public int getAnimationHeightOffset() { return 0; }
    @Override public int getRenderMode() { return RENDERMODE_DEFAULT; }
    @Override public Node getNext() { return null; }
    @Override public Node getPrevious() { return null; }
    @Override public long getHash() { return 0; }
}
