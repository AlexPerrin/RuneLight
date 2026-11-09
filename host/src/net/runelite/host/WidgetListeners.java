package net.runelite.host;

import java.util.HashMap;
import java.util.Map;
import net.runelite.api.widgets.JavaScriptCallback;
import net.runelite.api.widgets.Widget;

/**
 * Java onOp listeners that plugins set on widgets (Widget.setOnOpListener with a
 * JavaScriptCallback, e.g. Quest List's search button). The game's menu only lists op i of a
 * widget when its event flags (+0x2b0, bit i+1) allow it or it has a CS2 onOp listener, so while a
 * Java listener is set the flags get one bit per non-empty op (the widget's own bits are restored
 * when it is cleared). When such an entry is selected and no MenuOptionClicked subscriber consumed
 * it, the listener runs; the click is consumed (no script, no IF_BUTTON) only for an op a plugin
 * wrote with setAction (such as Quest List's "Open Search"). The game's own ops still go to the game, as on desktop: Better Teleport Menu sets an
 * empty listener on the teleport menu's options, and consuming their "Continue" stopped the
 * RESUME_PAUSEBUTTON (gen 22). Entries are keyed by (component, CC_CREATE index) and the game's Widget* so a slot
 * the game re-creates doesn't inherit a stale listener. Game thread.
 */
final class WidgetListeners {
    private WidgetListeners() {}

    private static final class Entry {
        Object[] onOp;
        long ptr;
        int ownFlags;
        int opBits;  // op bits syncFlags last added
    }

    private static final Map<Long, Entry> entries = new HashMap<>();

    private static long key(int id, int index) { return (long) id << 32 | (index & 0xffffffffL); }

    static synchronized Object[] onOp(int id, int index) {
        Entry e = entries.get(key(id, index));
        return e != null && e.ptr == Native.widgetPtr(id, index) ? e.onOp : null;
    }

    static synchronized void setOnOp(int id, int index, Object[] listener) {
        long k = key(id, index), ptr = Native.widgetPtr(id, index);
        Entry e = entries.get(k);
        if (e != null && e.ptr != ptr) {
            entries.remove(k);
            e = null;
        }
        boolean set = listener != null && listener.length > 0 && listener[0] != null;
        if (!set) {
            if (e != null) {
                entries.remove(k);
                Native.widgetSetFlags(id, index, e.ownFlags);
            }
            return;
        }
        if (ptr == 0) return;
        if (e == null) {
            e = new Entry();
            e.ptr = ptr;
            e.ownFlags = Native.widgetFlags(id, index);
            entries.put(k, e);
        }
        e.onOp = listener;
        syncFlags(id, index);
    }

    /** A plugin's setClickMask: kept as the widget's own flags while a Java onOp listener adds op bits. */
    static synchronized void setFlags(int id, int index, int flags) {
        Entry e = entries.get(key(id, index));
        if (e != null && e.ptr == Native.widgetPtr(id, index)) {
            e.ownFlags = flags & ~(e.opBits & ~e.ownFlags);  // not the op bits the host added
            syncFlags(id, index);
        } else {
            Native.widgetSetFlags(id, index, flags);
        }
    }

    /** After an op's text changed: the shown-op bits follow the non-empty ops while a listener is set. */
    static synchronized void syncFlags(int id, int index) {
        Entry e = entries.get(key(id, index));
        if (e == null || e.ptr != Native.widgetPtr(id, index)) return;
        String[] s = Native.widgetStrings(id, index);
        int bits = 0;
        for (int i = 2; s != null && i < s.length && i - 2 < 10; i++)
            if (s[i] != null && !s[i].isEmpty()) bits |= 1 << (i - 2 + 1);
        e.opBits = bits;
        Native.widgetSetFlags(id, index, e.ownFlags | bits);
    }

    /** Ops (0-based) that plugins wrote with setAction, per widget slot and the game's Widget*. */
    private static final Map<Long, long[]> pluginOps = new HashMap<>();  // {ptr, bitmask}

    static synchronized void notePluginOp(int id, int index, int op, boolean set) {
        long k = key(id, index), ptr = Native.widgetPtr(id, index);
        long[] v = pluginOps.get(k);
        if (v == null || v[0] != ptr) pluginOps.put(k, v = new long[] {ptr, 0});
        if (set) v[1] |= 1L << op; else v[1] &= ~(1L << op);
    }

    private static synchronized boolean isPluginOp(int id, int index, int op) {
        long[] v = pluginOps.get(key(id, index));
        return v != null && v[0] == Native.widgetPtr(id, index) && op >= 0 && op < 64 && (v[1] & 1L << op) != 0;
    }

    /**
     * Java onDragComplete listeners (Widget.setOnDragCompleteListener with a JavaScriptCallback: Bank
     * Tags' layouts, Bank Tag Layouts), keyed as onOp entries by slot and the game's Widget*. Run on
     * the release cycle of a widget drag (Host's WIDGET_DRAG, touch state 4), before the game
     * processes the release, so a listener's setDraggedOnWidget(null) still cancels the game's swap.
     * The event's source is the dragged widget, its target the dragged-on widget (as desktop's
     * onDragComplete event). Verified 2026-10-17 gen 25 (Bank Tags layouts: swap, move to an empty slot, the
     * real bank order unchanged).
     */
    private static final Map<Long, Object[]> dragComplete = new HashMap<>();  // {Long ptr, Object[] listener}

    static synchronized void setOnDragComplete(int id, int index, Object[] listener) {
        long k = key(id, index);
        boolean set = listener != null && listener.length > 0 && listener[0] != null;
        if (!set) { dragComplete.remove(k); return; }
        long ptr = Native.widgetPtr(id, index);
        if (ptr == 0) return;
        dragComplete.put(k, new Object[] {ptr, listener});
    }

    static synchronized Object[] onDragComplete(int id, int index) {
        Object[] v = dragComplete.get(key(id, index));
        return v != null && (Long) v[0] == Native.widgetPtr(id, index) ? (Object[]) v[1] : null;
    }

    /** Drops the Java onDragComplete listeners of `id`'s created children. */
    static synchronized void forgetDragComplete(int id) {
        dragComplete.keySet().removeIf(k -> (int) (k >>> 32) == id && (int) (long) k >= 0);
    }

    static synchronized boolean hasDragComplete() { return !dragComplete.isEmpty(); }

    /** Release cycle of a drag of `src` (component, index) onto `tgt` (component < 0: none). Game thread. */
    static void runDragComplete(net.runelite.api.Client client, int srcId, int srcIndex, int tgtId, int tgtIndex) {
        Object[] l = onDragComplete(srcId, srcIndex);
        if (l == null || !(l[0] instanceof JavaScriptCallback)) return;
        AndroidScriptEvent ev = new AndroidScriptEvent(client, l);
        ev.setSource(AndroidWidget.of(srcId, srcIndex));
        ev.setTarget(tgtId >= 0 ? AndroidWidget.of(tgtId, tgtIndex) : null);
        if ("1".equals(Native.property("debug.osrsrl.draglog")))
            android.util.Log.i(Host.TAG, "onDragComplete listener: " + (srcId >>> 16) + ":" + (srcId & 0xffff) + "[" + srcIndex + "] on "
                    + (tgtId >= 0 ? (tgtId >>> 16) + ":" + (tgtId & 0xffff) + "[" + tgtIndex + "]" : "none"));
        ((JavaScriptCallback) l[0]).run(ev);
    }

    /** Created children of `id` were deleted (or one is re-created at `index`; -1 = all). */
    static synchronized void forget(int id, int index) {
        if (index >= 0) {
            entries.remove(key(id, index));
            dragComplete.remove(key(id, index));
            for (Map<Long, Object[]> m : javaListeners.values()) m.remove(key(id, index));
            return;
        }
        entries.keySet().removeIf(k -> (int) (k >>> 32) == id && (int) (long) k >= 0);
        dragComplete.keySet().removeIf(k -> (int) (k >>> 32) == id && (int) (long) k >= 0);
        for (Map<Long, Object[]> m : javaListeners.values())
            m.keySet().removeIf(k -> (int) (k >>> 32) == id && (int) (long) k >= 0);
    }

    // --- Other Java listeners (gen 28), keyed like onOp by slot and the game's Widget* ---
    // Fired by the host: TIMER every client cycle while the widget exists and isn't hidden (as the
    // game's onTimer runs for open interfaces); DRAG each cycle the widget is being dragged, before
    // the release cycle (which runs onDragComplete); VAR_TRANSMIT at the next client cycle after a
    // varp transmit, when the widget's host-side trigger list is null or contains a changed varp.
    // TARGET_ENTER/LEAVE follow the game's target mode in runTargetMode. CLICK..DIALOG_ABORT are run by the
    // game's own dispatch: their slot holds the host's marker (Native.widgetSetJavaListener), the game queues
    // the event as for a CS2 listener and Host.onScript phase 5 runs the Java listener (runGameEvent). STORED
    // (onKey) is kept only: set/clear works, nothing fires.
    static final int TIMER = 0, DRAG = 1, VAR_TRANSMIT = 2, STORED = 3, TARGET_ENTER = 4, TARGET_LEAVE = 5,
        CLICK = 6, HOLD = 7, RELEASE = 8, MOUSE_OVER = 9, MOUSE_LEAVE = 10, MOUSE_REPEAT = 11, SCROLL_WHEEL = 12, DIALOG_ABORT = 13;
    /** The game's slot of each kind from CLICK (docs/offsets.md "Widget drag": CC_SETON* setter 0x4e4fe0). */
    private static final int[] GAME_SLOTS = {0x380, 0x410, 0x3e0, 0x440, 0x4a0, 0x470, 0x6f8, 0x8d8};

    /** Host.onScript phase 5: the game ran its event for a slot holding the Java listener of `kind`. */
    static void runGameEvent(net.runelite.api.Client client, int kind) {
        int[] src = Native.scriptEventSource();
        if (src != null) runFor(client, kind, src[0], src[1]);
    }
    private static final Map<Integer, Map<Long, Object[]>> javaListeners = new HashMap<>();  // kind -> key -> {Long ptr, Object[] listener}
    private static final Map<Long, Object[]> varTriggers = new HashMap<>();  // key -> {Long ptr, int[] triggers}
    private static final java.util.Set<Integer> changedVarps = new java.util.HashSet<>();

    static synchronized void setListener(int kind, int id, int index, Object[] listener) {
        Map<Long, Object[]> m = javaListeners.get(kind);
        if (m == null) javaListeners.put(kind, m = new HashMap<>());
        long k = key(id, index);
        // Desktop has one slot per listener: a Java callback (or null) replaces the game's CS2 script
        // there, so the host clears the game's slot. A CS2 listener {scriptId, args...} (Bank Tags'
        // and QH's onDrag 284 on layout items) isn't written to the game; its own script stays.
        int slot = kind == TIMER ? 0x668 : kind == DRAG ? 0x4d0 : kind == VAR_TRANSMIT ? 0x590 : -1;
        boolean cs2 = listener != null && listener.length > 0 && listener[0] instanceof Integer;
        if (slot >= 0 && !cs2) Native.widgetClearListener(id, index, slot);
        boolean none = listener == null || listener.length == 0 || listener[0] == null;
        if (kind >= CLICK && !cs2) Native.widgetSetJavaListener(id, index, GAME_SLOTS[kind - CLICK], none ? -1 : kind);
        if (none) { m.remove(k); return; }
        long ptr = Native.widgetPtr(id, index);
        if (ptr != 0) m.put(k, new Object[] {ptr, listener});
    }

    static synchronized void setVarTransmitTrigger(int id, int index, int[] triggers) {
        long ptr = Native.widgetPtr(id, index);
        if (ptr != 0) varTriggers.put(key(id, index), new Object[] {ptr, triggers});
    }

    static synchronized void varpTransmitted(int varp) {
        Map<Long, Object[]> m = javaListeners.get(VAR_TRANSMIT);
        if (m != null && !m.isEmpty()) changedVarps.add(varp);
    }

    /** {id, index, listener} of live entries of `kind`; stale ones (the game freed the widget) dropped. */
    private static synchronized java.util.List<Object[]> live(int kind) {
        Map<Long, Object[]> m = javaListeners.get(kind);
        java.util.List<Object[]> out = new java.util.ArrayList<>();
        if (m == null || m.isEmpty()) return out;
        for (java.util.Iterator<Map.Entry<Long, Object[]>> it = m.entrySet().iterator(); it.hasNext(); ) {
            Map.Entry<Long, Object[]> e = it.next();
            int id = (int) (e.getKey() >>> 32), index = (int) (long) e.getKey();
            if ((Long) e.getValue()[0] != Native.widgetPtr(id, index)) { it.remove(); continue; }
            out.add(new Object[] {id, index, e.getValue()[1]});
        }
        return out;
    }

    private static void run(net.runelite.api.Client client, Object[] l, int id, int index) {
        Object[] args = (Object[]) l[2];
        if (!(args[0] instanceof JavaScriptCallback)) return;
        AndroidScriptEvent ev = new AndroidScriptEvent(client, args);
        ev.setSource(AndroidWidget.of(id, index));
        try {
            ((JavaScriptCallback) args[0]).run(ev);
        } catch (RuntimeException ex) {
            android.util.Log.w(Host.TAG, "widget listener " + (id >>> 16) + ":" + (id & 0xffff) + "[" + index + "]", ex);
        }
    }

    /** Each client cycle (game thread): onTimer listeners, then onVarTransmit for varps sent since. */
    static void runCycle(net.runelite.api.Client client) {
        for (Object[] l : live(TIMER)) {
            int id = (Integer) l[0], index = (Integer) l[1];
            Widget w = AndroidWidget.of(id, index);
            if (w != null && !w.isHidden()) run(client, l, id, index);
        }
        java.util.Set<Integer> changed;
        synchronized (WidgetListeners.class) {
            if (changedVarps.isEmpty()) return;
            changed = new java.util.HashSet<>(changedVarps);
            changedVarps.clear();
        }
        for (Object[] l : live(VAR_TRANSMIT)) {
            int id = (Integer) l[0], index = (Integer) l[1];
            Object[] t;
            synchronized (WidgetListeners.class) { t = varTriggers.get(key(id, index)); }
            int[] triggers = t != null && (Long) t[0] == Native.widgetPtr(id, index) ? (int[]) t[1] : null;
            boolean fire = triggers == null;
            for (int i = 0; triggers != null && i < triggers.length && !fire; i++) fire = changed.contains(triggers[i]);
            if (fire) run(client, l, id, index);
        }
    }

    private static int[] lastTarget;  // {active, component, index} at the previous cycle

    /**
     * Each client cycle (game thread), while any Java onTargetEnter/onTargetLeave listener is set: follows the
     * game's target mode (Native.targetMode) and runs the selected widget's onTargetEnter when the mode starts
     * and its onTargetLeave when it ends or another widget is selected, as desktop runs them (Wiki's orb
     * "Lookup"). Verified 2026-10-18 gen 33 with Wiki (see docs/plugin-host.md).
     */
    static void runTargetMode(net.runelite.api.Client client) {
        if (!hasListeners(TARGET_ENTER) && !hasListeners(TARGET_LEAVE)) { lastTarget = null; return; }
        int[] t = Native.targetMode();
        if (t == null) return;
        int[] prev = lastTarget;
        lastTarget = t;
        boolean wasOn = prev != null && prev[0] != 0, on = t[0] != 0;
        boolean same = wasOn && on && prev[1] == t[1] && prev[2] == t[2];
        if (wasOn && !same) runFor(client, TARGET_LEAVE, prev[1], prev[2]);
        if (on && !same && prev != null) runFor(client, TARGET_ENTER, t[1], t[2]);
    }

    private static void runFor(net.runelite.api.Client client, int kind, int id, int index) {
        for (Object[] l : live(kind))
            if ((Integer) l[0] == id && (Integer) l[1] == index) run(client, l, id, index);
    }

    /** A drag cycle of `src` before release (game thread): its Java onDrag listener. */
    static void runDrag(net.runelite.api.Client client, int srcId, int srcIndex) {
        for (Object[] l : live(DRAG))
            if ((Integer) l[0] == srcId && (Integer) l[1] == srcIndex) run(client, l, srcId, srcIndex);
    }

    static synchronized boolean hasListeners(int kind) {
        Map<Long, Object[]> m = javaListeners.get(kind);
        return m != null && !m.isEmpty();
    }

    /**
     * After MenuOptionClicked was posted for a widget entry and not consumed: runs the widget's Java
     * onOp listener, if any. Returns true when the caller should consume the click: the op is one
     * the game's widget didn't have (only the plugin's listener can act on it).
     */
    static boolean dispatch(net.runelite.api.MenuEntry entry, net.runelite.api.Client client) {
        int id = entry.getParam1(), index = entry.getParam0();
        Entry e;
        synchronized (WidgetListeners.class) {
            e = entries.get(key(id, index));
            if ((e == null || e.ptr != Native.widgetPtr(id, index)) && index != -1) {
                index = -1;
                e = entries.get(key(id, -1));
            }
            if (e != null && e.ptr != Native.widgetPtr(id, index)) e = null;
        }
        Object[] l = e != null ? e.onOp : null;
        if (l == null || !(l[0] instanceof JavaScriptCallback)) return false;
        Widget source = AndroidWidget.of(id, index);
        AndroidScriptEvent ev = new AndroidScriptEvent(client, l);
        ev.setSource(source);
        ev.setOp(entry.getIdentifier());
        int op = entry.getIdentifier();  // 1-based for CC_OP entries (the pause-button "Continue" is 0x80000000)
        boolean pluginOnly = op >= 1 && op <= 32 && isPluginOp(id, index, op - 1);
        android.util.Log.i(Host.TAG, "widget onOp listener: " + (id >>> 16) + ":" + (id & 0xffff) + "[" + index + "] op " + op + (pluginOnly ? " (plugin op, consumed)" : " (game op, passed on)"));
        ((JavaScriptCallback) l[0]).run(ev);
        return pluginOnly;
    }
}
