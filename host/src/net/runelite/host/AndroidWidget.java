package net.runelite.host;

import java.awt.Rectangle;
import java.util.ArrayList;
import java.util.List;
import net.runelite.api.Point;
import net.runelite.api.widgets.Widget;

/**
 * A widget by component id; every getter reads the game's current state. Backed so far:
 * text, visibility, geometry (relative position, size, parent), item id/quantity, static and
 * CC_CREATE children.
 */
public class AndroidWidget extends WidgetBag {
    final int id;

    AndroidWidget(int id) {
        this.id = id;
    }

    /**
     * One instance per component (and per CC_CREATE child) for what getWidget and the drag getters
     * return: plugins compare widgets with == (Spellbook's findSpellIdxForComponent matches
     * getWidget(...) == getDraggedWidget()), as desktop's widgets are long-lived objects.
     */
    private static final java.util.concurrent.ConcurrentHashMap<Long, AndroidWidget> CANONICAL = new java.util.concurrent.ConcurrentHashMap<>();

    static AndroidWidget of(int id) {
        return CANONICAL.computeIfAbsent((long) id << 32 | 0xffffffffL, k -> new AndroidWidget(id));
    }

    static AndroidWidget of(int id, int index) {
        if (index < 0) return of(id);
        return CANONICAL.computeIfAbsent((long) id << 32 | index, k -> new Dynamic(id, index));
    }

    /** A child the game just (re)created at `index`: a new canonical instance, as desktop's new Widget. */
    static AndroidWidget fresh(int id, int index) {
        AndroidWidget w = new Dynamic(id, index);
        CANONICAL.put((long) id << 32 | index, w);
        return w;
    }

    /** Raw geometry {x, y, width, height, parentId} or null. */
    int[] rawGeo() { return geometry(id); }

    /**
     * Static widget geometry, cached while an overlay frame renders (OverlayRenderer brackets it):
     * WorldMapOverlay asks for 595:7's bounds once per map point (1800+), each walking the parents
     * over JNI. Geometry writes (field setters, revalidate) drop the cache.
     */
    private static volatile java.util.HashMap<Integer, int[]> frameGeometry;
    private static volatile Thread frameThread;
    static void beginFrame() { frameThread = Thread.currentThread(); frameGeometry = new java.util.HashMap<>(); }
    static void endFrame() { frameGeometry = null; frameThread = null; }
    private static void geometryChanged() { java.util.HashMap<Integer, int[]> c = frameGeometry; if (c != null && Thread.currentThread() == frameThread) c.clear(); }
    static int[] geometry(int id) {
        java.util.HashMap<Integer, int[]> cache = frameGeometry;
        if (cache == null || Thread.currentThread() != frameThread) return Native.widgetGeometry(id);
        if (cache.containsKey(id)) { int[] g = cache.get(id); return g == null ? null : g.clone(); }
        int[] g = Native.widgetGeometry(id);
        cache.put(id, g == null ? null : g.clone());
        return g;
    }

    private int[] geo() {
        int[] g = rawGeo();
        return g != null ? g : new int[] {0, 0, 0, 0, -1};
    }

    @Override public int getId() { return id; }
    @Override public int getType() { return prop(34, 0); }
    @Override public String getText() { String t = Native.widgetText(id); return t != null ? t : ""; }
    @Override public boolean isSelfHidden() { return Native.widgetHidden(id); }

    /** CC_CREATE child index for {@link Native#widgetItem}; -1 for a component. */
    int childIndex() { return -1; }

    /** Item set by IF_SETOBJECT / CC_SETOBJECT (inventory slots, bank items): -1 if none. */
    @Override public int getItemId() { int[] it = Native.widgetItem(id, childIndex()); return it != null ? it[0] : -1; }
    /** Text colour (IF_SETCOLOUR), written through to the game widget. */
    @Override public int getTextColor() { return Native.widgetColor(id, childIndex()); }
    @Override public Widget setHidden(boolean hidden) { Native.widgetSetHidden(id, childIndex(), hidden); return this; }
    @Override public Widget setText(String text) { Native.widgetSetText(id, childIndex(), text); return this; }
    @Override public Widget setTextColor(int rgb) { Native.widgetSetColor(id, childIndex(), rgb); return this; }
    // Field setters written into the game widget (CC_SET* offsets, docs/offsets.md); revalidate()
    // re-runs the game's layout as CC_SETPOSITION/CC_SETSIZE do.
    private Widget set(int offset, int value) { Native.widgetSetField(id, childIndex(), offset, value); geometryChanged(); return this; }
    @Override public Widget setXPositionMode(int v) { return set(0x3c, v); }
    @Override public Widget setYPositionMode(int v) { return set(0x40, v); }
    @Override public Widget setWidthMode(int v) { return set(0x44, v); }
    @Override public Widget setHeightMode(int v) { return set(0x48, v); }
    @Override public Widget setOriginalX(int v) { return set(0x4c, v); }
    @Override public Widget setOriginalY(int v) { return set(0x50, v + childYShift()); }
    @Override public Widget setOriginalWidth(int v) { return set(0x54, v); }
    @Override public Widget setOriginalHeight(int v) { return set(0x58, v); }
    @Override public Widget setFilled(boolean v) { return set(0xa8, v ? 1 : 0); }
    @Override public Widget setOpacity(int v) { return set(0xb0, v); }
    @Override public Widget setSpriteId(int v) { CustomSpriteWidgets.note(this, v); return set(0xc8, v); }
    @Override public Widget setSpriteTiling(boolean v) { return set(0xec, v ? 1 : 0); }
    // Widget +0xb38/+0xb3c (what getItemId/getItemQuantity read). // unverified: whether the game
    // then draws the item on a graphic widget; the host redraws items over custom sprites anyway.
    @Override public Widget setItemId(int v) { return set(0xb38, v); }
    @Override public Widget setItemQuantity(int v) { return set(0xb3c, v); }
    /**
     * Item quantity mode, Widget +0x13c. The mobile client's values (CC_SETOBJECT handler 0x4e06dc):
     * 1205 setobject_nonum stores 0, 1212 setobject_alwaysnum 3, 1200 setobject 3 for stackable items
     * and 1 otherwise; so RuneLite's NEVER 0 / ALWAYS 1 / STACKABLE 2 map to 0 / 3 / 1.
     * Verified on the device (2026-10-17 gen 27, probe `wqty 15:3 <slot> <mode>`): NEVER hid coins'
     * "100K", ALWAYS drew "1" on a single log, STACKABLE drew "100K" and hid the log's "1".
     */
    @Override public Widget setItemQuantityMode(int mode) { return set(0x13c, mode == 1 ? 3 : mode == 2 ? 1 : 0); }
    private int field(int offset, int dflt) { int v = Native.widgetGetField(id, childIndex(), offset); return v == Integer.MIN_VALUE ? dflt : v; }
    /** Inverse of setItemQuantityMode's mapping (game 0 → NEVER, 3 → ALWAYS, 1 → STACKABLE). */
    @Override public int getItemQuantityMode() { int m = field(0x13c, 0); return m == 3 ? 1 : m == 1 ? 2 : 0; }
    // Drag dead zone / dead time (+0x320/+0x324, ints the drag block compares; static RE gen 28).
    // Dead time verified 2026-10-17 gen 29 with Anti Drag on 149:0's children: the game's own value
    // is 6; at 100 a 1 s inventory drag did nothing, at 5 and at 30 it moved the item. Dead zone unverified.
    @Override public int getDragDeadZone() { return field(0x320, 0); }
    @Override public void setDragDeadZone(int v) { set(0x320, v); }
    @Override public int getDragDeadTime() { return field(0x324, 0); }
    @Override public void setDragDeadTime(int v) { set(0x324, v); }
    // The game sets +0x348 once any CC_SETON or IF_SETON listener is stored; Java onOp listeners count too.
    // Type +0x28 (not into or out of 12, refused natively), content type +0x30 (if1 client code),
    // target verb op index +0x308 (default 4; CC 1312), drag parent +0x318 (CC_SETDRAGGABLE).
    @Override public void setType(int v) { set(0x28, v); }
    @Override public int getContentType() { return field(0x30, 0); }
    @Override public Widget setContentType(int v) { return set(0x30, v); }
    @Override public int getTargetPriority() { return field(0x308, 4); }
    @Override public void setTargetPriority(int v) { set(0x308, v); }
    @Override public Widget getDragParent() { int[] p = Native.widgetDragParent(id, childIndex()); return p == null ? null : of(p[0], p[1]); }
    @Override public Widget setDragParent(Widget w) {
        AndroidWidget p = w instanceof AndroidWidget ? (AndroidWidget) w : null;
        Native.widgetSetDragParent(id, childIndex(), p != null ? p.id : -1, p != null ? p.childIndex() : -1);
        return this;
    }
    @Override public boolean hasListener() { return field(0x348, 0) != 0 || WidgetListeners.onOp(id, childIndex()) != null; }
    // Model / animation fields (CC_SETMODEL 1108 type+id, CC_SETMODELANIM 1110; getModelType/Id/
    // getAnimationId read the same). // unverified: whether the game redraws the model from them
    @Override public Widget setModelType(int v) { return set(0xfc, v); }
    @Override public Widget setModelId(int v) { return set(0x100, v); }
    @Override public Widget setAnimationId(int v) { return set(0x10c, v); }
    /** Line height +0x188 (type 12 keeps it elsewhere; not written there). */
    @Override public Widget setLineHeight(int v) { return getType() == 12 ? this : set(0x188, v); }
    // Laid-out position/size (+0x5c..+0x68, what getRelativeX/Y/getWidth/getHeight read); the next
    // layout pass recomputes them, as on desktop.
    @Override public void setRelativeX(int v) { set(0x5c, v); }
    @Override public void setRelativeY(int v) { set(0x60, v + childYShift()); }
    @Override public void setWidth(int v) { set(0x64, v); }
    @Override public void setHeight(int v) { set(0x68, v); }
    /** As desktop: the layout pass keeps relative x/y here (-1: its own; -1, -1 clears); the next revalidate applies it. */
    @Override public void setForcedPosition(int x, int y) {
        int fy = y == -1 ? -1 : y + childYShift();
        if (Native.isClientThread()) Native.widgetForcePosition(id, childIndex(), x, fy);
        else Host.invoke(() -> Native.widgetForcePosition(id, childIndex(), x, fy));
    }

    // CC_SETPOSITION / CC_SETSIZE fields without the layout pass (as desktop's setters; revalidate() runs it).
    @Override public Widget setPos(int x, int y) { setOriginalX(x); return setOriginalY(y); }
    @Override public Widget setPos(int x, int y, int xMode, int yMode) { setXPositionMode(xMode); setYPositionMode(yMode); return setPos(x, y); }
    @Override public Widget setSize(int w, int h) { setOriginalWidth(w); return setOriginalHeight(h); }
    @Override public Widget setSize(int w, int h, int wMode, int hMode) { setWidthMode(wMode); setHeightMode(hMode); return setSize(w, h); }
    // Scroll and model view fields (the ones getScroll*/getModelZoom/getRotation* read; widget_props).
    // Written raw as desktop's setters do (no clamping); Bank Tags sets the bank's scroll height/position
    // for its layouts. // unverified: on the device.
    @Override public Widget setScrollX(int v) { return set(0x84, v); }
    @Override public Widget setScrollY(int v) { return set(0x88, v); }
    @Override public Widget setScrollWidth(int v) { return set(0x8c, v); }
    @Override public Widget setScrollHeight(int v) { return set(0x90, v); }
    @Override public Widget setModelZoom(int v) { return set(0x128, v); }
    @Override public Widget setRotationX(int v) { return set(0x11c, v); }
    @Override public Widget setRotationY(int v) { return set(0x120, v); }
    @Override public Widget setRotationZ(int v) { return set(0x124, v); }
    // Click mask = the game widget's flags (+0x2b0; CC_SETFLAGS/IF_SETEVENTS bits, desktop layout:
    // ops at bits 1..10, drag 17, drag target 20). Spellbook's reorder mode sets 0x120000.
    @Override public int getClickMask() { return Native.widgetFlags(id, childIndex()); }
    @Override public Widget setClickMask(int mask) { WidgetListeners.setFlags(id, childIndex(), mask); return this; }
    @Override public void setBorderType(int v) { set(0xf0, v); }
    @Override public void setFlippedVertically(boolean v) { set(0xf8, v ? 1 : 0); }
    @Override public void setFlippedHorizontally(boolean v) { set(0xf9, v ? 1 : 0); }
    @Override public Widget setFontId(int v) { return set(0x150, v); }
    @Override public Widget setXTextAlignment(int v) { return set(0x18c, v); }
    @Override public Widget setYTextAlignment(int v) { return set(0x190, v); }
    @Override public Widget setTextShadowed(boolean v) { return set(0x194, v ? 1 : 0); }
    @Override public void setNoClickThrough(boolean v) { set(0xb98, v ? 1 : 0); }
    @Override public void setNoScrollThrough(boolean v) { set(0xb99, v ? 1 : 0); }
    @Override public void revalidate() { Native.widgetRevalidate(id, childIndex()); geometryChanged(); }
    // Desktop re-lays out the children and the scroll bounds; here the same layout pass as
    // revalidate() (0x45ebf4 walks a layer's children). // unverified: scroll bounds on the device
    @Override public void revalidateScroll() { Native.widgetRevalidate(id, childIndex()); geometryChanged(); }
    @Override public int getIndex() { return childIndex(); }
    // Script-visible properties read from the game widget (Native.widgetProps).
    private int prop(int i, int dflt) { int[] p = Native.widgetProps(id, childIndex()); return p != null ? p[i] : dflt; }
    @Override public int getScrollX() { return prop(0, 0); }
    @Override public int getScrollY() { return prop(1, 0); }
    @Override public int getScrollWidth() { return prop(2, 0); }
    @Override public int getScrollHeight() { return prop(3, 0); }
    @Override public int getModelZoom() { return prop(4, 0); }
    @Override public int getRotationX() { return prop(5, 0); }
    @Override public int getRotationY() { return prop(6, 0); }
    @Override public int getRotationZ() { return prop(7, 0); }
    @Override public int getOpacity() { return prop(8, 0); }
    @Override public int getModelType() { return prop(9, 0); }
    @Override public int getModelId() { return prop(10, -1); }
    @Override public int getAnimationId() { return prop(11, -1); }
    private String[] strings() { return Native.widgetStrings(id, childIndex()); }
    @Override public String getName() { String[] s = strings(); return s != null && s[0] != null ? s[0] : ""; }
    @Override public String getTargetVerb() { String[] s = strings(); return s != null && s[1] != null ? s[1] : ""; }
    // Name/verb/ops written into the game widget (CC_SETOPBASE/CC_SETTARGETVERB/CC_SETOP), so the
    // game's menu lists them; a Java onOp listener is run by WidgetListeners when one is selected.
    @Override public Widget setName(String name) { Native.widgetSetString(id, childIndex(), -1, name != null ? name : ""); return this; }
    @Override public void setTargetVerb(String verb) { Native.widgetSetString(id, childIndex(), -2, verb != null ? verb : ""); }
    @Override public void setAction(int index, String action) {
        if (index < 0 || index > 31) return;
        Native.widgetSetString(id, childIndex(), index, action != null ? action : "");
        WidgetListeners.notePluginOp(id, childIndex(), index, action != null && !action.isEmpty());
        WidgetListeners.syncFlags(id, childIndex());
    }
    // Sub-ops (desktop's op sub-menus, e.g. Bank Tags' "1 Dose".."4 Dose" under op 10) are stored
    // host-side only: the mobile menu's sub-menu support isn't mapped, so the game doesn't show them.
    // unverified: whether mobile 241 has op sub-menus at all
    private static final java.util.Map<Long, String[][]> SUB_OPS = new java.util.HashMap<>();
    private long subOpKey() { return ((long) id << 32) | (childIndex() & 0xffffffffL); }
    @Override public void setSubOp(int op, int sub, String text) {
        if (op < 0 || op > 31 || sub < 0 || sub > 31) return;
        synchronized (SUB_OPS) {
            String[][] t = SUB_OPS.computeIfAbsent(subOpKey(), k -> new String[32][]);
            if (t[op] == null || t[op].length <= sub) t[op] = java.util.Arrays.copyOf(t[op] != null ? t[op] : new String[0], sub + 1);
            t[op][sub] = text;
        }
    }
    @Override public String[][] getSubOps() {
        synchronized (SUB_OPS) { return SUB_OPS.get(subOpKey()); }
    }
    // unverified: default (desktop's flag is true for the IF3 interfaces plugins touch; Inventory
    // Grid only asserts it)
    @Override public boolean isIf3() { return true; }
    @Override public void clearActions() {
        String[] a = getActions();
        for (int i = 0; a != null && i < a.length; i++) {
            if (a[i] != null) Native.widgetSetString(id, childIndex(), i, "");
            WidgetListeners.notePluginOp(id, childIndex(), i, false);
        }
        WidgetListeners.syncFlags(id, childIndex());
    }
    @Override public void setOnOpListener(Object... args) { WidgetListeners.setOnOp(id, childIndex(), args); }
    @Override public Object[] getOnOpListener() {
        Object[] java = WidgetListeners.onOp(id, childIndex());
        return java != null ? java : Native.widgetListener(id, childIndex(), 0x698);
    }
    // The game's CS2 listeners (CC_SETON*/IF_SETON* slots; magic args such as event_opbase come
    // back as stored, as on desktop). Quest List reruns 399:2's onVarTransmit to redraw the list.
    // onDragComplete: Java listeners run on the drag's release (WidgetListeners.runDragComplete).
    // onDrag: CS2 listeners from plugins are not written to the game (Bank Tags sets 284, the
    // bank's drag-scroll script); the game's own stays (+0x4d0, `set 'wlisten <g:c> [index]'`).
    @Override public void setOnDragCompleteListener(Object... args) { WidgetListeners.setOnDragComplete(id, childIndex(), args); }
    // Java listeners fired by the host (WidgetListeners: timer, drag, var transmit) or stored only.
    @Override public void setOnTimerListener(Object... a) { WidgetListeners.setListener(WidgetListeners.TIMER, id, childIndex(), a); }
    @Override public void setOnDragListener(Object... a) { WidgetListeners.setListener(WidgetListeners.DRAG, id, childIndex(), a); }
    @Override public void setOnVarTransmitListener(Object... a) { WidgetListeners.setListener(WidgetListeners.VAR_TRANSMIT, id, childIndex(), a); }
    @Override public void setVarTransmitTrigger(int... t) { WidgetListeners.setVarTransmitTrigger(id, childIndex(), t); }
    private void listen(int kind, Object[] a) { WidgetListeners.setListener(kind, id, childIndex(), a); }
    @Override public void setOnClickListener(Object... a) { listen(WidgetListeners.CLICK, a); }
    @Override public void setOnHoldListener(Object... a) { listen(WidgetListeners.HOLD, a); }
    @Override public void setOnReleaseListener(Object... a) { listen(WidgetListeners.RELEASE, a); }
    @Override public void setOnMouseOverListener(Object... a) { listen(WidgetListeners.MOUSE_OVER, a); }
    @Override public void setOnMouseLeaveListener(Object... a) { listen(WidgetListeners.MOUSE_LEAVE, a); }
    @Override public void setOnMouseRepeatListener(Object... a) { listen(WidgetListeners.MOUSE_REPEAT, a); }
    @Override public void setOnScrollWheelListener(Object... a) { listen(WidgetListeners.SCROLL_WHEEL, a); }
    @Override public void setOnKeyListener(Object... a) { listen(WidgetListeners.STORED, a); }
    @Override public void setOnDialogAbortListener(Object... a) { listen(WidgetListeners.DIALOG_ABORT, a); }
    @Override public void setOnTargetEnterListener(Object... a) { WidgetListeners.setListener(WidgetListeners.TARGET_ENTER, id, childIndex(), a); }
    @Override public void setOnTargetLeaveListener(Object... a) { WidgetListeners.setListener(WidgetListeners.TARGET_LEAVE, id, childIndex(), a); }
    @Override public Object[] getOnVarTransmitListener() { return Native.widgetListener(id, childIndex(), 0x590); }
    @Override public Object[] getOnInvTransmitListener() { return Native.widgetListener(id, childIndex(), 0x5d8); }
    // Read from the game; an empty array (not null) when unreadable, since plugins stream it
    // unchecked (Banked Experience, Bank Tags' PotionStorage on the potion store widget).
    @Override public int[] getVarTransmitTrigger() { return triggers(0x5c0); }
    private int[] triggers(int offset) {
        int[] t = Native.widgetTriggers(id, childIndex(), offset);
        return t != null ? t : new int[0];
    }
    @Override public Object[] getOnLoadListener() { return Native.widgetListener(id, childIndex(), 0x350); }
    @Override public Object[] getOnKeyListener() { return Native.widgetListener(id, childIndex(), 0x758); }
    @Override public Widget setHasListener(boolean v) { return this; }
    @Override public String[] getActions() {
        String[] s = strings();
        return s == null || s.length <= 2 ? null : java.util.Arrays.copyOfRange(s, 2, s.length);
    }
    @Override public int getSpriteId() { return prop(12, -1); }
    @Override public boolean getSpriteTiling() { return prop(13, 0) != 0; }
    @Override public int getBorderType() { return prop(14, 0); }
    @Override public boolean isFlippedVertically() { return prop(16, 0) != 0; }
    @Override public boolean isFlippedHorizontally() { return prop(17, 0) != 0; }
    @Override public boolean isFilled() { return prop(18, 0) != 0; }
    @Override public boolean getTextShadowed() { return prop(19, 0) != 0; }
    @Override public int getXTextAlignment() { return prop(20, 0); }
    @Override public int getYTextAlignment() { return prop(21, 0); }
    @Override public int getLineHeight() { return prop(22, 0); }
    @Override public int getFontId() { return prop(23, -1); }
    /** The widget's game font: getTextWidth measures with the game's own font (PARAWIDTH's), off-thread on the game thread. */
    @Override public net.runelite.api.FontTypeFace getFont() {
        int id = getFontId();
        return id < 0 ? null : GameFont.of(id);
    }
    @Override public int getOriginalX() { return prop(24, 0); }
    @Override public int getOriginalY() { return prop(25, 0) - childYShift(); }
    @Override public int getXPositionMode() { return prop(26, 0); }
    @Override public int getYPositionMode() { return prop(27, 0); }
    @Override public int getOriginalWidth() { return prop(28, 0); }
    @Override public int getOriginalHeight() { return prop(29, 0); }
    @Override public int getWidthMode() { return prop(30, 0); }
    @Override public int getHeightMode() { return prop(31, 0); }
    @Override public boolean getNoClickThrough() { return prop(32, 0) != 0; }
    @Override public boolean getNoScrollThrough() { return prop(33, 0) != 0; }
    @Override public int getItemQuantity() { int[] it = Native.widgetItem(id, childIndex()); return it != null ? it[1] : 0; }  // the raw field, as desktop's

    @Override
    public boolean isHidden() {
        if (isSelfHidden()) return true;
        int parent = getParentId();
        if (parent == -1) {  // a group's root layer that isn't mounted (closed interface) is hidden
            int root = Native.rootInterface();
            return root >= 0 && (id >>> 16) != root;
        }
        return ancestorsHidden(parent);
    }

    /** Hidden if it or any ancestor is hidden, as in RuneLite. */
    static boolean ancestorsHidden(int start) {
        int cur = start, last = start;
        for (int depth = 0; cur != -1 && depth < 64; depth++) {
            if (Native.widgetHidden(cur)) return true;
            int[] g = geometry(cur);
            last = cur;
            cur = g != null ? g[4] : -1;
        }
        // As desktop: a chain that ends outside the root interface belongs to a group that isn't
        // mounted (a closed interface stays loaded on mobile, e.g. the bank after closing it).
        if (last != -1 && cur == -1) {
            int root = Native.rootInterface();
            if (root >= 0 && (last >>> 16) != root) return true;
        }
        return false;
    }

    @Override public int getRelativeX() { return geo()[0]; }
    @Override public int getRelativeY() { return geo()[1] - childYShift(); }
    @Override public int getWidth() { return geo()[2]; }
    @Override public int getHeight() { return geo()[3] - (this.getClass() == AndroidWidget.class && id == BANK_ITEMS_CONTAINER ? bankColumnShift() : 0); }

    /**
     * Mobile bank layout vs desktop: desktop's 12:9 (Bankmain.ITEMS_CONTAINER, where Bank Tags builds
     * its tag-tab column) starts below the tab bar 12:10; the mobile 12:9 also spans the tab bar.
     * Plugins see 12:9 as desktop does: its children's original y is shifted down by the overlap
     * (bottom of 12:10 minus top of 12:9, from live geometry) and its height is reduced by it.
     * // unverified: on the device.
     */
    static final int BANK_ITEMS_CONTAINER = 12 << 16 | 9, BANK_TABS = 12 << 16 | 10;
    private static int shiftStamp = -1, shiftCache;

    static int bankColumnShift() {
        if (shiftStamp == Host.cycleStamp) return shiftCache;
        shiftStamp = Host.cycleStamp;
        shiftCache = 0;
        if (!Native.widgetExists(BANK_ITEMS_CONTAINER) || !Native.widgetExists(BANK_TABS)) return 0;
        AndroidWidget c = new AndroidWidget(BANK_ITEMS_CONTAINER), t = new AndroidWidget(BANK_TABS);
        if (c.isHidden() || t.isHidden()) return 0;
        Rectangle cb = c.getBoundsRaw(), tb = t.getBoundsRaw();
        boolean overlapX = tb.x < cb.x + cb.width && cb.x < tb.x + tb.width;
        int shift = tb.y + tb.height - cb.y;
        if (overlapX && shift > 0 && shift < cb.height / 2) shiftCache = shift;
        return shiftCache;
    }

    /** Children of the mobile bank's 12:9 (static, e.g. the potion-store button 12:52, or dynamic) get the desktop y shift. */
    int childYShift() { return (id >>> 16) == 12 && id != BANK_ITEMS_CONTAINER && getParentIdRaw() == BANK_ITEMS_CONTAINER ? bankColumnShift() : 0; }
    int getParentIdRaw() { return geo()[4]; }

    Rectangle getBoundsRaw() {
        Point p = getCanvasLocation();
        int[] g = geo();
        return new Rectangle(p.getX(), p.getY(), g[2], g[3]);
    }
    @Override public int getParentId() { return geo()[4]; }

    @Override
    public Widget getParent() {
        int p = getParentId();
        return p == -1 ? null : of(p);
    }

    /** Position on the game's UI canvas, which is also the overlay canvas. */
    @Override
    public Point getCanvasLocation() {
        int[] own = rawGeo();
        if (own == null) return new Point(0, 0);
        int x = own[0], y = own[1], cur = own[4];
        for (int depth = 0; cur != -1 && depth < 64; depth++) {
            int[] g = geometry(cur);
            if (g == null) break;
            x += g[0];
            y += g[1];
            cur = g[4];
        }
        return new Point(x, y);
    }

    @Override
    public Rectangle getBounds() {
        Point p = getCanvasLocation();
        return new Rectangle(p.getX(), p.getY(), getWidth(), getHeight());
    }

    @Override
    public boolean contains(Point point) {
        return getBounds().contains(point.getX(), point.getY());
    }

    /** Components of the same group whose parent is this widget. */
    @Override
    public Widget[] getStaticChildren() {
        int group = id >>> 16, n = Native.widgetChildCount(group);
        List<Widget> out = new ArrayList<>();
        for (int child = 0; child < n; child++) {
            int cid = group << 16 | child;
            int[] g = geometry(cid);
            if (g != null && g[4] == id) out.add(of(cid));
        }
        return out.toArray(new Widget[0]);
    }

    // Children created by plugins: in the game's tree through CC_CREATE (static layer parents, on
    // the game thread), else Java-side only (DetachedWidget, not drawn).
    private final List<Widget> created = new ArrayList<>();
    /**
     * Plugin children are created in the game's tree (CC_CREATE); `setprop debug.osrsrl.createchild 0`
     * keeps them Java-side instead. On by default since 2026-10-16 (Skills Tab Progress Bars' bars
     * draw and update, no duplicates after reopening the tab, clean login).
     */
    static final boolean CREATE_IN_GAME = !"0".equals(Native.property("debug.osrsrl.createchild"));

    @Override
    public Widget createChild(int index, int type) {
        if (CREATE_IN_GAME && childIndex() < 0 && type != 12 && Native.isClientThread()) {
            int at = Native.widgetCreateChild(id, index, type);
            if (at >= 0) { WidgetListeners.forget(id, at); return fresh(id, at); }
        }
        DetachedWidget w = new DetachedWidget(this, type);
        if (index >= 0 && index < created.size()) created.set(index, w); else created.add(w);
        return w;
    }

    @Override public Widget createChild(int type) { return createChild(-1, type); }
    @Override public void deleteAllChildren() {
        created.clear();
        if (CREATE_IN_GAME && childIndex() < 0 && Native.isClientThread()) {
            Native.widgetDeleteChildren(id);
            WidgetListeners.forget(id, -1);
        }
    }
    /** The game's CC_CREATE children (null slots stay null), then plugin-created ones. */
    @Override public Widget[] getDynamicChildren() {
        Widget[] game = gameChildren();
        if (created.isEmpty()) return game;
        List<Widget> all = new ArrayList<>(java.util.Arrays.asList(game));
        all.addAll(created);
        return all.toArray(new Widget[0]);
    }
    /**
     * As desktop's children array assignment, for the common use: truncation of the game's
     * CC_CREATE children (Bank Tags' rebuildTabs keeps its first 4). Slots set to null are hidden
     * (as applyChildEdits does); other rearrangements are not modelled.
     */
    @Override public void setChildren(Widget[] children) {
        if (childIndex() >= 0 || children == null) return;
        Widget[] cur = gameChildren();
        int n = Math.min(children.length, cur.length);
        for (int i = 0; i < n; i++)
            if (children[i] == null && cur[i] instanceof Dynamic) cur[i].setHidden(true);
        if (children.length < cur.length && Native.isClientThread()) {
            Native.widgetTruncateChildren(id, children.length);
            for (int i = children.length; i < cur.length; i++) WidgetListeners.forget(id, i);
        }
    }
    @Override public Widget[] getChildren() {
        Widget[] d = getDynamicChildren();
        if (d.length == 0) return null;
        synchronized (issued) { if (issued.size() < 4096) issued.add(new Widget[][] {d, d.clone()}); }
        return d;
    }

    // On desktop getChildren() is the game's own array: a plugin that nulls a slot (XP Drop's
    // hideSkillIcons) removes that child. Here the array is a copy, so the arrays handed out are
    // remembered and, after each event dispatch (applyChildEdits), a game child whose slot was
    // nulled is hidden in the game instead.
    private static final List<Widget[][]> issued = new ArrayList<>();

    static void applyChildEdits() {
        List<Widget[][]> list;
        synchronized (issued) {
            if (issued.isEmpty()) return;
            list = new ArrayList<>(issued);
            issued.clear();
        }
        for (Widget[][] pair : list) {
            Widget[] now = pair[0], was = pair[1];
            for (int i = 0; i < now.length; i++)
                if (now[i] == null && was[i] instanceof Dynamic) {
                    was[i].setHidden(true);
                    synchronized (suppressed) { suppressed.put((Dynamic) was[i], Host.cycleStamp + SUPPRESS_CYCLES); }
                }
        }
    }

    // The game's scripts can unhide a child after the event (XP drops are shown by the script that
    // ScriptPreFired precedes), so a nulled child is re-hidden every cycle for a while.
    private static final int SUPPRESS_CYCLES = 150;
    private static int rehideLogs;
    private static final java.util.Map<Dynamic, Integer> suppressed = new java.util.HashMap<>();

    /** Game thread, once per cycle. */
    static void reapplyHidden() {
        synchronized (suppressed) {
            if (suppressed.isEmpty()) return;
            for (java.util.Iterator<java.util.Map.Entry<Dynamic, Integer>> it = suppressed.entrySet().iterator(); it.hasNext(); ) {
                java.util.Map.Entry<Dynamic, Integer> e = it.next();
                if (e.getValue() - Host.cycleStamp < 0) { it.remove(); continue; }
                if (!e.getKey().isSelfHidden()) {
                    e.getKey().setHidden(true);
                    if (rehideLogs++ < 5) android.util.Log.i(Host.TAG, "re-hid nulled child " + e.getKey().id + "[" + e.getKey().index + "]");
                }
            }
        }
    }
    @Override public Widget getChild(int index) {
        Widget[] d = getDynamicChildren();
        return index >= 0 && index < d.length ? d[index] : null;
    }

    Widget[] gameChildren() {
        int n = Native.widgetDynamicCount(id);
        Widget[] out = new Widget[n];
        for (int i = 0; i < n; i++)
            if (Native.widgetDynamicGeometry(id, i) != null) out[i] = of(id, i);
        return out;
    }

    /** A child created by the game's CC_CREATE under component `parent`. */
    static final class Dynamic extends AndroidWidget {
        final int index;

        Dynamic(int parent, int index) {
            super(parent);
            this.index = index;
        }

        @Override int[] rawGeo() { return Native.widgetDynamicGeometry(id, index); }
        @Override public int getIndex() { return index; }
        @Override int childYShift() { return id == BANK_ITEMS_CONTAINER ? bankColumnShift() : 0; }
        @Override int childIndex() { return index; }
        @Override public String getText() { String t = Native.widgetDynamicText(id, index); return t != null ? t : ""; }
        @Override public boolean isSelfHidden() { return Native.widgetDynamicHidden(id, index); }
        @Override public boolean isHidden() { return isSelfHidden() || ancestorsHidden(id); }
        @Override public Widget getParent() { return of(id); }
        @Override public Widget[] getStaticChildren() { return new Widget[0]; }
        @Override Widget[] gameChildren() { return new Widget[0]; }
        @Override public boolean equals(Object o) { return o instanceof Dynamic && ((Dynamic) o).id == id && ((Dynamic) o).index == index; }
        @Override public int hashCode() { return id * 31 + index; }
    }
    @Override public Widget[] getNestedChildren() { return new Widget[0]; }

    @Override
    public boolean equals(Object o) {
        return o instanceof AndroidWidget && !(o instanceof Dynamic) && ((AndroidWidget) o).id == id;
    }

    @Override
    public int hashCode() {
        return id;
    }

    @Override
    public String toString() {
        int[] g = geo();
        return "Widget[" + (id >>> 16) + ":" + (id & 0xffff) + " '" + getText() + "' " + g[2] + "x" + g[3] + " parent " + g[4] + "]";
    }
}
