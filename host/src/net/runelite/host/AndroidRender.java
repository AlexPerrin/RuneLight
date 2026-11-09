package net.runelite.host;

import java.awt.image.BufferedImage;
import net.runelite.api.FloatProjection;
import net.runelite.api.IntProjection;
import net.runelite.api.MainBufferProvider;
import net.runelite.api.Rasterizer;

/**
 * The rendering surface plugins see. The mobile game draws with the GPU, so the desktop CPU
 * buffers have these Android meanings:
 * - BUFFER (MainBufferProvider): the last finished game frame (frame_capture.cpp, glReadPixels),
 *   scaled to the UI canvas; getPixels asks for the next one. It is a copy: writing it doesn't
 *   change what the game shows.
 * - RASTERIZER: desktop's Rasterizer2D/3D fills, drawing into BUFFER's pixels.
 * - PROJECTION: the main camera (Client getCameraX/Y/Z, pitch, yaw), as desktop's IntProjection and
 *   as a 4x4 matrix (FloatProjection); the top-level WorldView's main and canvas projection.
 */
final class AndroidRender {
    private AndroidRender() {}

    static final Buffer BUFFER = new Buffer();
    static final HostRasterizer RASTERIZER = new HostRasterizer();
    static final CameraProjection PROJECTION = new CameraProjection();

    static final class Buffer implements MainBufferProvider {
        private BufferedImage image = new BufferedImage(1, 1, BufferedImage.TYPE_INT_RGB);
        private boolean requested;

        /** The image sized to the canvas (black until a frame arrives). */
        private BufferedImage image() {
            java.awt.Dimension c = OverlayRenderer.canvasSize();
            if (c.width > 0 && c.height > 0 && (image.getWidth() != c.width || image.getHeight() != c.height)) {
                image = new BufferedImage(c.width, c.height, BufferedImage.TYPE_INT_RGB);
                RASTERIZER.resetRasterClipping();
            }
            return image;
        }

        void request() {
            if (requested) return;
            requested = true;
            Native.requestFrame();
        }

        /** Game thread (HostDrawManager.onCycle): a captured frame {w, h, ARGB...}, scaled into the buffer. */
        void adopt(int[] f) {
            requested = false;
            BufferedImage img = image();
            int w = img.getWidth(), h = img.getHeight();
            android.graphics.Bitmap b = android.graphics.Bitmap.createBitmap(f, 2, f[0], f[0], f[1], android.graphics.Bitmap.Config.ARGB_8888);
            if (b.getWidth() != w || b.getHeight() != h) b = android.graphics.Bitmap.createScaledBitmap(b, w, h, true);
            b.getPixels(img.getPixels(), 0, w, 0, 0, w, h);
        }

        @Override public int[] getPixels() { request(); return image().getPixels(); }
        @Override public int getWidth() { return image().getWidth(); }
        @Override public int getHeight() { return image().getHeight(); }
        @Override public java.awt.Image getImage() { return image(); }
    }

    /** A Graphics2D on BUFFER clipped to the Rasterizer's draw region (FontTypeFace.drawWidgetText); dispose it. */
    static java.awt.Graphics2D rasterGraphics() {
        java.awt.Graphics2D g = BUFFER.image().createGraphics();
        HostRasterizer r = RASTERIZER;
        g.setClip(r.left, r.top, r.x1() - r.left, r.y1() - r.top);
        return g;
    }

    /**
     * Client.drawOriginalMenu / draw2010Menu: the open menu (getMenuX/Y/Width/Height, getMenuEntries)
     * drawn into BUFFER in desktop's two styles, `alpha` its opacity (0-255). BUFFER is the copy of the
     * captured frame plugins read; the screen shows the game's own native menu.
     */
    static int drawMenu(net.runelite.api.Client c, int alpha, boolean style2010) {
        if (!c.isMenuOpen()) return 0;
        int x = c.getMenuX(), y = c.getMenuY(), w = c.getMenuWidth(), h = c.getMenuHeight();
        net.runelite.api.MenuEntry[] e = c.getMenuEntries();
        if (w <= 0 || h <= 0 || e == null) return 0;
        int a = Math.max(0, Math.min(255, alpha)) << 24;
        java.awt.Graphics2D g = BUFFER.image().createGraphics();
        g.setColor(new java.awt.Color(a | (style2010 ? 0x3e3529 : 0x5d5447), true));
        g.fillRect(x, y, w, h);
        g.setColor(new java.awt.Color(a | (style2010 ? 0x2a2620 : 0x000000), true));
        g.fillRect(x + 1, y + 1, w - 2, 16);
        g.drawRect(x + 1, y + 18, w - 3, h - 20);
        g.setColor(new java.awt.Color(a | (style2010 ? 0xc6b895 : 0x5d5447), true));
        g.drawString("Choose Option", x + 3, y + 14);
        g.setColor(new java.awt.Color(a | 0xffffff, true));
        for (int i = 0; i < e.length; i++) {
            net.runelite.api.MenuEntry m = e[e.length - 1 - i];  // the last entry is the top row
            String t = net.runelite.client.util.Text.removeTags(m.getOption() + (m.getTarget().isEmpty() ? "" : " " + m.getTarget()));
            g.drawString(t, x + 3, y + 31 + 15 * i);
        }
        g.dispose();
        return e.length;
    }

    /** Desktop's Rasterizer2D clip region and Rasterizer3D flat/gouraud triangles, into BUFFER. */
    static final class HostRasterizer implements Rasterizer {
        private int left, top, right = Integer.MAX_VALUE, bottom = Integer.MAX_VALUE;
        private boolean lowRes;
        private int[] target;  // another image drawn into (the minimap tile drawer's pass), else BUFFER
        private int targetW, targetH;

        /** Draws into px (w x h) until target(null, 0, 0); the clip is reset either way. */
        void target(int[] px, int w, int h) {
            target = px;
            targetW = w;
            targetH = h;
            resetRasterClipping();
        }

        @Override public int[] getPixels() { return target != null ? target : BUFFER.image().getPixels(); }
        @Override public int getWidth() { return target != null ? targetW : BUFFER.getWidth(); }
        @Override public int getHeight() { return target != null ? targetH : BUFFER.getHeight(); }

        private int x1() { return Math.min(right, getWidth()); }
        private int y1() { return Math.min(bottom, getHeight()); }

        @Override public void setDrawRegion(int l, int t, int r, int b) {
            left = Math.max(0, l);
            top = Math.max(0, t);
            right = Math.min(getWidth(), r);
            bottom = Math.min(getHeight(), b);
        }

        @Override public void resetRasterClipping() {
            left = top = 0;
            right = bottom = Integer.MAX_VALUE;
        }

        @Override public void fillRectangle(int x, int y, int w, int h, int rgb) {
            int[] px = getPixels();
            int stride = getWidth();
            int xa = Math.max(left, x), ya = Math.max(top, y), xb = Math.min(x1(), x + w), yb = Math.min(y1(), y + h);
            for (int j = ya; j < yb; j++) java.util.Arrays.fill(px, j * stride + xa, Math.max(j * stride + xa, j * stride + xb), rgb);
        }

        @Override public void setRasterGouraudLowRes(boolean on) { lowRes = on; }

        @Override public void rasterFlat(int y0, int y1, int y2, int x0, int x1, int x2, int rgb) {
            triangle(y0, y1, y2, x0, x1, x2, rgb, 0, 0, false);
        }

        @Override public void rasterGouraud(int y0, int y1, int y2, int x0, int x1, int x2, int hsl0, int hsl1, int hsl2) {
            triangle(y0, y1, y2, x0, x1, x2, hsl0, hsl1, hsl2, true);
        }

        /**
         * Coordinates are relative to the draw region's origin, as desktop's Rasterizer3D offsets
         * them by its clip start. Gouraud colours are 16-bit HSL through the brightness palette;
         * low res computes the colour once per 8-pixel run, as desktop.
         */
        private void triangle(int y0, int y1, int y2, int x0, int x1, int x2, int c0, int c1, int c2, boolean gouraud) {
            int[] px = getPixels();
            int stride = getWidth();
            x0 += left; x1 += left; x2 += left;
            y0 += top; y1 += top; y2 += top;
            long area = (long) (x1 - x0) * (y2 - y0) - (long) (x2 - x0) * (y1 - y0);
            if (area == 0) return;
            int xa = Math.max(left, Math.min(x0, Math.min(x1, x2))), xb = Math.min(x1() - 1, Math.max(x0, Math.max(x1, x2)));
            int ya = Math.max(top, Math.min(y0, Math.min(y1, y2))), yb = Math.min(y1() - 1, Math.max(y0, Math.max(y1, y2)));
            int[] palette = gouraud ? TEXTURES.palette() : null;
            for (int y = ya; y <= yb; y++) {
                int colour = c0;
                boolean first = true;
                for (int x = xa; x <= xb; x++) {
                    long w0 = (long) (x1 - x) * (y2 - y) - (long) (x2 - x) * (y1 - y);
                    long w1 = (long) (x2 - x) * (y0 - y) - (long) (x0 - x) * (y2 - y);
                    long w2 = area - w0 - w1;
                    if (area > 0 ? (w0 < 0 || w1 < 0 || w2 < 0) : (w0 > 0 || w1 > 0 || w2 > 0)) continue;
                    if (gouraud && (first || !lowRes || (x & 7) == 0))
                        colour = palette[(int) ((w0 * c0 + w1 * c1 + w2 * c2) / area) & 0xffff];
                    first = false;
                    px[y * stride + x] = colour;
                }
            }
        }
    }

    /** The main camera: getCameraX/Y/Z (local x, local y, height), pitch and yaw (0..16383). */
    static final class CameraProjection implements IntProjection, FloatProjection {
        private static net.runelite.api.Client c() { return Host.client(); }
        private static float rad(int a) { return (float) (a * Math.PI * 2 / 16384); }

        @Override public int getCameraX() { return c().getCameraX(); }
        @Override public int getCameraY() { return c().getCameraY(); }
        @Override public int getCameraZ() { return c().getCameraZ(); }
        @Override public float getPitchSin() { return (float) Math.sin(rad(c().getCameraPitch())); }
        @Override public float getPitchCos() { return (float) Math.cos(rad(c().getCameraPitch())); }
        @Override public float getYawSin() { return (float) Math.sin(rad(c().getCameraYaw())); }
        @Override public float getYawCos() { return (float) Math.cos(rad(c().getCameraYaw())); }

        @Override public float[] project(float x, float y, float z) { return project(x, y, z, new float[3]); }

        /** (local x, height, local y) to camera space {x, y, depth}, as Perspective's CPU path. */
        @Override public float[] project(float x, float y, float z, float[] out) {
            if (out == null || out.length < 3) out = new float[3];
            float[] m = getProjection();
            for (int r = 0; r < 3; r++) out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
            return out;
        }

        /** The same transform as a column-major 4x4 matrix over (local x, height, local y, 1). */
        @Override public float[] getProjection() {
            float ps = getPitchSin(), pc = getPitchCos(), ys = getYawSin(), yc = getYawCos();
            float cx = getCameraX(), cy = getCameraY(), cz = getCameraZ();
            // x1 = dx*yc + dy*ys; y1 = dy*yc - dx*ys; out = {x1, dh*pc - y1*ps, y1*pc + dh*ps}
            float[] m = {
                yc, ys * ps, -ys * pc, 0,   // column: local x
                0, pc, ps, 0,               // column: height
                ys, -yc * ps, yc * pc, 0,   // column: local y
                0, 0, 0, 1};
            for (int r = 0; r < 3; r++) m[12 + r] = -(m[r] * cx + m[4 + r] * cz + m[8 + r] * cy);
            return m;
        }
    }

    static final Textures TEXTURES = new Textures();

    /**
     * The game's texture provider (Client +0x4710: definitions from cache index 9, pixels expanded
     * from the texture's sprite with the brightness palette, 128x128, LRU of 20). Game thread for
     * live values; elsewhere the last values read.
     */
    static final class Textures implements net.runelite.api.TextureProvider {
        private double paletteBrightness = -1, brightness = 0.8;
        private final int[] palette = new int[65536];
        private int[] info = new int[0];
        private int infoStamp = -1;
        private HostTexture[] textures = new HostTexture[0];

        /** Per texture 8 ints (Native.textures), refreshed once per cycle on the game thread. */
        int[] info() {
            if (infoStamp != Host.cycleStamp && Native.isClientThread()) {
                int[] v = Native.textures();
                if (v != null) info = v;
                infoStamp = Host.cycleStamp;
            }
            return info;
        }

        int field(int id, int k) {
            int[] v = info();
            return id >= 0 && id * 8 + k < v.length ? v[id * 8 + k] : 0;
        }

        double brightness() {
            if (Native.isClientThread()) {
                double b = Native.textureBrightness();
                if (!Double.isNaN(b)) brightness = b;
            }
            return brightness;
        }

        @Override public double getBrightness() { return brightness(); }

        /** Writes the game's brightness; textures it already expanded keep theirs until evicted. */
        @Override public void setBrightness(double b) {
            if (Native.isClientThread() && Native.setTextureBrightness(b)) brightness = b;
        }

        @Override public net.runelite.api.Texture[] getTextures() {
            int n = info().length / 8;
            if (textures.length != n) textures = java.util.Arrays.copyOf(textures, n);
            net.runelite.api.Texture[] out = new net.runelite.api.Texture[n];
            for (int id = 0; id < n; id++) {
                if (field(id, 0) == 0) continue;
                if (textures[id] == null) textures[id] = new HostTexture(id);
                out[id] = textures[id];
            }
            return out;
        }

        @Override public int[] load(int id) { return Native.isClientThread() ? Native.texturePixels(id, true) : null; }
        @Override public int getDefaultColor(int id) { return field(id, 1); }

        /** Desktop's Rasterizer3D HSL palette for the current brightness. */
        int[] palette() {
            double b = brightness();
            if (b != paletteBrightness) {
                buildPalette(palette, b);
                paletteBrightness = b;
            }
            return palette;
        }

        static void buildPalette(int[] out, double brightness) {
            int i = 0;
            for (int hs = 0; hs < 512; hs++) {
                double hue = (hs >> 3) / 64.0 + 0.0078125, sat = (hs & 7) / 8.0 + 0.0625;
                for (int l = 0; l < 128; l++) {
                    double lum = l / 128.0, r = lum, g = lum, bl = lum;
                    if (sat != 0) {
                        double q = lum < 0.5 ? lum * (1 + sat) : lum + sat - lum * sat, p = 2 * lum - q;
                        r = hue2rgb(p, q, hue + 1 / 3.0);
                        g = hue2rgb(p, q, hue);
                        bl = hue2rgb(p, q, hue - 1 / 3.0);
                    }
                    int rgb = (int) (Math.pow(r, brightness) * 256) << 16 | (int) (Math.pow(g, brightness) * 256) << 8 | (int) (Math.pow(bl, brightness) * 256);
                    out[i++] = rgb == 0 ? 1 : rgb;
                }
            }
        }

        private static double hue2rgb(double p, double q, double t) {
            if (t < 0) t++;
            if (t > 1) t--;
            if (6 * t < 1) return p + (q - p) * 6 * t;
            if (2 * t < 1) return q;
            if (3 * t < 2) return p + (q - p) * (2 / 3.0 - t) * 6;
            return p;
        }
    }

    /** One of the game's textures; u/v are the GPU animation offsets desktop keeps, stored host-side (the mobile renderer animates on the GPU). */
    static final class HostTexture implements net.runelite.api.Texture {
        private final int id;
        private float u, v;

        HostTexture(int id) { this.id = id; }

        /** The loaded pixels; loads them through the game's provider first, as TextureProvider.load. */
        @Override public int[] getPixels() {
            if (!Native.isClientThread()) return null;
            int[] px = Native.texturePixels(id, false);
            return px != null ? px : Native.texturePixels(id, true);
        }
        @Override public int getAnimationDirection() { return TEXTURES.field(id, 4); }
        @Override public int getAnimationSpeed() { return TEXTURES.field(id, 5); }
        @Override public boolean isLoaded() { return TEXTURES.field(id, 6) != 0; }
        @Override public float getU() { return u; }
        @Override public void setU(float x) { u = x; }
        @Override public float getV() { return v; }
        @Override public void setV(float x) { v = x; }
        @Override public net.runelite.api.Node getNext() { return null; }
        @Override public net.runelite.api.Node getPrevious() { return null; }
        @Override public long getHash() { return id; }
        @Override public String toString() { return "Texture " + id + " (sprite " + TEXTURES.field(id, 3) + ", average colour " + TEXTURES.field(id, 1) + ")"; }
    }
}
