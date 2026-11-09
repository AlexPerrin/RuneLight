package net.runelite.host;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MotionEvent;
import android.view.Window;
import java.awt.Component;
import java.awt.Container;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.swing.AbstractButton;
import javax.swing.JCheckBox;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JScrollPane;
import javax.swing.JToggleButton;
import javax.swing.text.JTextComponent;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;

/**
 * The plugin sidebar: RuneLite's navigation buttons and the selected plugin panel (headless Swing)
 * shown by the native overlay (overlay.cpp) as an ImGui window.
 *
 * The panel's component tree is flattened into lines "kind\tdepth\tcolor\ttext" (kinds: L label, E text field,
 * B button, C checkbox (text prefixed with 0/1), T read-only text, A editable text area, X combo box,
 * H clickable panel, I icon-only clickable "<icon id>:<selected>:<background argb hex>")
 * after a header line "S\tselected\ttooltip...". The overlay sends back taps as (kind, index)
 * pairs, which are dispatched to the Swing listeners on the EDT (Edt).
 */
final class Sidebar {
    /** Last touch/key event seen by the touch router (uptime ms), for the idle-tick getters. */
    static volatile long lastInputMillis = android.os.SystemClock.uptimeMillis();

    // The "mouse" for plugins on a touch screen: where the finger is (or last lifted), in window
    // pixels; it counts as hovering while down and for HOVER_LINGER_MS after it lifts.
    static final long HOVER_LINGER_MS = 3000;
    static volatile float touchX = -1, touchY = -1;
    static volatile boolean touchDown;
    static volatile long touchUpMillis;

    static void noteTouch(int action, float x, float y) {
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
                touchX = x;
                touchY = y;
                touchDown = true;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                touchUpMillis = android.os.SystemClock.uptimeMillis();
                touchDown = false;
                break;
            default:
                break;
        }
    }

    /** The hover point in window pixels, or null when nothing is touched (or it lifted a while ago). */
    static float[] hoverPoint() {
        if (touchX < 0) return null;
        if (!touchDown && android.os.SystemClock.uptimeMillis() - touchUpMillis > HOVER_LINGER_MS) return null;
        return new float[] {touchX, touchY};
    }

    private static final String TAG = Host.TAG;
    private static ClientToolbar toolbar;
    private static int selected = 0;
    private static final List<Component> nodes = new ArrayList<>();
    private static String lastModel = "";
    private static int cycles;
    private static Field scrollView;

    private Sidebar() {}

    /** Debug: the current model is written here (files/sidebar-model.txt) when it changes. */
    static java.io.File dumpFile;

    static void start(ClientToolbar tb, java.io.File dir) {
        toolbar = tb;
        if ("1".equals(Native.property("debug.osrsrl.sbdump"))) dumpFile = new java.io.File(dir, "sidebar-model.txt");
        installTouchRouter(0);
    }

    /** A model build is queued on the EDT (not queued twice while the EDT is busy or in a dialog). */
    private static final java.util.concurrent.atomic.AtomicBoolean buildQueued = new java.util.concurrent.atomic.AtomicBoolean();

    /**
     * Game thread, every client cycle: takes the overlay's taps and queues them (and the model
     * build) for the EDT, then lets the EDT run (Edt.yieldToEdt). Long-press menu entries (kind 3)
     * are game events and stay on the game thread.
     */
    static void onCycle() {
        if (toolbar != null) {
            int[] ev = Native.sidebarPollEvents();
            boolean dirty = false;
            if (ev != null) {
                for (int i = 0; i + 1 < ev.length; i += 2) {
                    final int kind = ev[i], index = ev[i + 1];
                    if (kind == 3) {
                        try {
                            AndroidMenu.tapped(index, Host.eventBus());
                        } catch (Throwable t) {
                            Log.e(TAG, "sidebar event failed", t);
                        }
                    } else {
                        javax.swing.SwingUtilities.invokeLater(() -> handle(kind, index));
                    }
                    dirty = true;
                }
            }
            final NavigationButton req = toolbar.takeRequested();
            if (req != null) {
                javax.swing.SwingUtilities.invokeLater(() -> {
                    int i = toolbar.getButtons().indexOf(req);
                    if (i >= 0) {
                        selected = i;
                        Native.sidebarSetOpen(true);
                    }
                });
                dirty = true;
            }
            boolean due = dirty || (++cycles % 10 == 0 && (Native.sidebarOpen() || cycles % 100 == 0));
            if (due && buildQueued.compareAndSet(false, true)) javax.swing.SwingUtilities.invokeLater(Sidebar::rebuild);
        }
        Edt.yieldToEdt();
    }

    /** EDT: rebuilds the model and sends it to the overlay if it changed. */
    private static void rebuild() {
        buildQueued.set(false);
        try {
            String model = build();
            autoClick();
            autoText();
            if (!model.equals(lastModel)) {
                lastModel = model;
                Native.sidebarSetModel(model);
                if (dumpFile != null) {
                    try (java.io.FileWriter w = new java.io.FileWriter(dumpFile)) {
                        w.write(model);
                    } catch (java.io.IOException ignored) {
                    }
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "sidebar model failed", t);
        }
    }

    private static String autoClickDone;

    /**
     * Test hook: `setprop debug.osrsrl.sbclick "<text>"` taps the first button whose text, or
     * whose preceding label's text, equals <text> (once per value; e.g. a quest name in Quest Helper).
     */
    private static String autoTextDone = "";

    /** Test hook: `setprop debug.osrsrl.sbtext "<text>"` types into the first text field (`-` clears it). */
    private static void autoText() {
        String want = Native.property("debug.osrsrl.sbtext");
        if (want.isEmpty() || want.equals(autoTextDone)) return;
        for (Component c : nodes)
            if (c instanceof javax.swing.JTextField) {
                autoTextDone = want;
                setFieldText((JTextComponent) c, want.equals("-") ? "" : want, true);
                return;
            }
    }

    private static void autoClick() {
        String want = Native.property("debug.osrsrl.sbclick");
        if (want.isEmpty() || want.equals(autoClickDone)) return;
        for (int i = 0; i < nodes.size(); i++) {
            Component c = nodes.get(i);
            if (!(c instanceof AbstractButton)) continue;
            boolean match = want.equals(text(c)) || (i > 0 && nodes.get(i - 1) instanceof JLabel && want.equals(text(nodes.get(i - 1))));
            if (match) {
                autoClickDone = want;
                Log.i(TAG, "sidebar auto-click '" + want + "'");
                handle(2, i);
                return;
            }
        }
    }

    /**
     * Text input: the sidebar has no keyboard of its own, so a tap on a text field opens an
     * Android dialog (soft keyboard included); OK applies the text on the EDT.
     */
    private static void promptText(JTextComponent field) {
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Activity a = currentActivity();
                if (a == null) return;
                final android.widget.EditText edit = new android.widget.EditText(a);
                final boolean area = !(field instanceof javax.swing.JTextField);
                if (area) {
                    edit.setSingleLine(false);
                    edit.setMinLines(4);
                    edit.setMaxLines(12);
                    edit.setGravity(android.view.Gravity.TOP | android.view.Gravity.START);
                } else {
                    edit.setSingleLine(true);
                }
                edit.setText(field.getText());
                edit.setSelection(edit.getText().length());
                android.app.AlertDialog d = new android.app.AlertDialog.Builder(a)
                    .setView(edit)
                    .setPositiveButton("OK", (dlg, w) -> {
                        String text = edit.getText().toString();
                        javax.swing.SwingUtilities.invokeLater(() -> {
                            setFieldText(field, text, true);
                            // Text areas save on focus loss (Notes); the prompt closing is that moment.
                            if (area) focusLost(field);
                        });
                    })
                    .setNegativeButton("Cancel", null)
                    .create();
                d.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
                d.show();
                edit.requestFocus();
            } catch (Throwable t) {
                Log.e(TAG, "text prompt failed", t);
            }
        });
    }

    private static void focusLost(Component c) {
        for (java.awt.event.FocusListener l : c.getFocusListeners()) {
            try {
                l.focusLost(new java.awt.event.FocusEvent());
            } catch (Throwable t) {
                Log.e(TAG, "FocusListener threw", t);
            }
        }
    }

    /**
     * The text a combo box shows for an item: its renderer's label text when the renderer is a
     * JLabel-like component (plugins render items with names), else the item's toString.
     */
    @SuppressWarnings({"unchecked", "rawtypes"})
    static String comboLabel(JComboBox box, Object item, int index) {
        try {
            javax.swing.ListCellRenderer r = box.getRenderer();
            if (r != null) {
                java.awt.Component c = r.getListCellRendererComponent(new javax.swing.JList(), item, index, false, false);
                if (c instanceof javax.swing.JLabel) {
                    String t = ((javax.swing.JLabel) c).getText();
                    if (t != null && !t.isEmpty()) return clean(t);
                }
            }
        } catch (Throwable ignored) { }
        return clean(String.valueOf(item));
    }

    /** Combo box: a tap opens an Android list of its items; the pick is applied on the EDT. */
    private static void promptChoice(JComboBox box) {
        final java.util.List<Object> items = new java.util.ArrayList<Object>(box.__items);
        if (items.isEmpty()) return;
        final String[] labels = new String[items.size()];
        for (int i = 0; i < labels.length; i++) labels[i] = comboLabel(box, items.get(i), i);
        final int current = items.indexOf(box.getSelectedItem());
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Activity a = currentActivity();
                if (a == null) return;
                new android.app.AlertDialog.Builder(a)
                    .setSingleChoiceItems(labels, current, (dlg, which) -> {
                        dlg.dismiss();
                        javax.swing.SwingUtilities.invokeLater(() -> select(box, items.get(which)));
                    })
                    .setNegativeButton("Cancel", null)
                    .show();
            } catch (Throwable t) {
                Log.e(TAG, "choice prompt failed", t);
            }
        });
    }

    /** Selects a combo box item and fires ItemListeners as Swing does (DESELECTED, then SELECTED). EDT only. */
    static void select(JComboBox box, Object item) {
        Object old = box.getSelectedItem();
        if (item == old) return;
        box.setSelectedItem(item);
        Log.i(TAG, "combo box '" + old + "' -> '" + item + "'");
        for (java.awt.event.ItemListener l : box.getItemListeners()) {
            try {
                if (old != null) {
                    java.awt.event.ItemEvent e = new java.awt.event.ItemEvent();
                    e.__item = old;
                    e.__state = 2;  // ItemEvent.DESELECTED
                    l.itemStateChanged(e);
                }
                java.awt.event.ItemEvent e = new java.awt.event.ItemEvent();
                e.__item = item;
                e.__state = 1;  // ItemEvent.SELECTED
                l.itemStateChanged(e);
            } catch (Throwable t) {
                Log.e(TAG, "ItemListener threw", t);
            }
        }
    }

    /**
     * Sets a text field's text and tells its listeners, as typing would: DocumentListeners
     * (removeUpdate then insertUpdate) and, when `enter`, ActionListeners. EDT only.
     */
    static void setFieldText(JTextComponent field, String text, boolean enter) {
        String old = field.getText();
        field.setText(text);
        Log.i(TAG, "text field '" + old + "' -> '" + text + "'");
        javax.swing.text.Document doc = field.getDocument();
        List<javax.swing.event.DocumentListener> listeners = documentListeners(doc);
        javax.swing.event.DocumentEvent ev = (javax.swing.event.DocumentEvent) java.lang.reflect.Proxy.newProxyInstance(
            Sidebar.class.getClassLoader(), new Class<?>[] {javax.swing.event.DocumentEvent.class}, (proxy, m, args) -> {
                switch (m.getName()) {
                    case "getDocument": return doc;
                    case "getLength": return text.length();
                    case "getOffset": return 0;
                    case "hashCode": return System.identityHashCode(proxy);
                    case "equals": return proxy == args[0];
                    case "toString": return "DocumentEvent";
                    default: return null;
                }
            });
        for (javax.swing.event.DocumentListener l : listeners) {
            try {
                if (!old.isEmpty()) l.removeUpdate(ev);
                if (!text.isEmpty()) l.insertUpdate(ev);
            } catch (Throwable t) {
                Log.e(TAG, "DocumentListener threw", t);
            }
        }
        if (enter && field instanceof javax.swing.JTextField)
            for (java.awt.event.ActionListener l : ((javax.swing.JTextField) field).getActionListeners())
                l.actionPerformed(new java.awt.event.ActionEvent());
    }

    @SuppressWarnings("unchecked")
    private static List<javax.swing.event.DocumentListener> documentListeners(javax.swing.text.Document doc) {
        for (Class<?> k = doc.getClass(); k != null; k = k.getSuperclass()) {
            try {
                java.lang.reflect.Field f = k.getDeclaredField("__DocumentListeners");
                f.setAccessible(true);
                return new java.util.ArrayList<>((List<javax.swing.event.DocumentListener>) f.get(doc));
            } catch (NoSuchFieldException e) {
                // keep looking
            } catch (Throwable t) {
                break;
            }
        }
        return java.util.Collections.emptyList();
    }

    /** EDT: a tap on the sidebar (1 = navigation button, 2 = model line). */
    private static void handle(int kind, int index) {
        if (kind == 1) {
            selected = index;
            return;
        }
        if (kind != 2 || index < 0 || index >= nodes.size()) return;
        Component c = nodes.get(index);
        Log.i(TAG, "sidebar tap on " + c.getClass().getName() + " '" + text(c) + "'");
        if (c instanceof TabHead) {
            TabHead h = (TabHead) c;
            h.pane.setSelectedIndex(h.index);
            return;
        }
        if (c instanceof ListRow) {  // select the row, then the list's own mouse listeners (locationToIndex = it)
            ListRow row = (ListRow) c;
            row.list.setSelectedIndex(row.index);
            c = row.list;
        }
        if (c instanceof AbstractButton) {
            AbstractButton b = (AbstractButton) c;
            if (b instanceof JToggleButton) b.setSelected(!b.isSelected());
            java.awt.event.ActionListener[] al = b.getActionListeners();
            for (java.awt.event.ActionListener l : al) l.actionPerformed(new java.awt.event.ActionEvent());
            // Buttons driven by mouse listeners only (ConfigPanel's colour button) fall through.
            if (al.length > 0 || b.getMouseListeners().length == 0) return;
        }
        if (c instanceof JTextComponent) {
            promptText((JTextComponent) c);
            return;
        }
        if (c instanceof JComboBox) {
            promptChoice((JComboBox) c);
            return;
        }
        long now = System.currentTimeMillis();
        javax.swing.JPopupMenu.__shown = null;
        for (java.awt.event.MouseListener l : c.getMouseListeners()) {
            try {
                l.mousePressed(new java.awt.event.MouseEvent(c, 501, now, 0, 1, 1, 1, false, 1));
                l.mouseReleased(new java.awt.event.MouseEvent(c, 502, now, 0, 1, 1, 1, false, 1));
                l.mouseClicked(new java.awt.event.MouseEvent(c, 500, now, 0, 1, 1, 1, false, 1));
            } catch (Throwable t) {
                Log.e(TAG, "MouseListener threw", t);
            }
        }
        // A listener that opened a JPopupMenu (Inventory Setups' "+"): offer its items in a picker.
        javax.swing.JPopupMenu popup = javax.swing.JPopupMenu.__shown;
        javax.swing.JPopupMenu.__shown = null;
        if (popup != null) promptPopup(popup);
    }

    /** A popup menu's enabled JMenuItems in an Android list; the pick fires the item's ActionListeners on the EDT. */
    private static void promptPopup(javax.swing.JPopupMenu popup) {
        final java.util.List<javax.swing.JMenuItem> items = new java.util.ArrayList<>();
        for (Component k : popup.getComponents())
            if (k instanceof javax.swing.JMenuItem && k.isVisible()) items.add((javax.swing.JMenuItem) k);
        Log.i(TAG, "popup menu shown: " + items.size() + " of " + popup.getComponentCount() + " children are menu items");
        if (items.isEmpty()) return;
        final String[] labels = new String[items.size()];
        for (int i = 0; i < labels.length; i++) labels[i] = clean(items.get(i).getText());
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Activity a = currentActivity();
                if (a == null) return;
                new android.app.AlertDialog.Builder(a)
                    .setItems(labels, (dlg, which) -> javax.swing.SwingUtilities.invokeLater(() -> {
                        javax.swing.JMenuItem item = items.get(which);
                        Log.i(TAG, "popup menu item '" + labels[which] + "'");
                        for (java.awt.event.ActionListener l : item.getActionListeners()) {
                            try {
                                l.actionPerformed(new java.awt.event.ActionEvent());
                            } catch (Throwable t) {
                                Log.e(TAG, "popup ActionListener threw", t);
                            }
                        }
                    }))
                    .setNegativeButton("Cancel", null)
                    .show();
            } catch (Throwable t) {
                Log.e(TAG, "popup prompt failed", t);
            }
        });
    }

    private static String build() {
        nodes.clear();
        StringBuilder sb = new StringBuilder();
        List<NavigationButton> buttons = toolbar.getButtons();
        if (selected >= buttons.size()) selected = 0;
        sb.append("S\t").append(selected);
        for (NavigationButton b : buttons) sb.append('\t').append(clean(b.getTooltip()));
        sb.append('\n');
        activate(buttons.isEmpty() ? null : buttons.get(selected).getPanel());
        if (!buttons.isEmpty() && buttons.get(selected).getPanel() != null) walk(buttons.get(selected).getPanel(), 0, sb);
        return sb.toString();
    }

    private static net.runelite.client.ui.PluginPanel active;

    /**
     * Desktop's ClientUI calls onActivate/onDeactivate when a panel is shown/hidden; panels gate their
     * updates on it (Time Tracking's update() is a no-op while inactive).
     */
    private static void activate(net.runelite.client.ui.PluginPanel p) {
        if (p == active) return;
        try {
            if (active != null) active.onDeactivate();
        } catch (Throwable t) {
            Log.e(TAG, "panel onDeactivate threw", t);
        }
        active = p;
        try {
            if (p != null) p.onActivate();
        } catch (Throwable t) {
            Log.e(TAG, "panel onActivate threw", t);
        }
    }

    private static void walk(Component c, int depth, StringBuilder sb) {
        if (c == null || !c.isVisible() || nodes.size() > 3000) return;
        if (c instanceof JLabel) {
            String t = clean(((JLabel) c).getText());
            if (!t.isEmpty()) add(sb, 'L', depth, c, t);
            else addIcon(sb, depth, c, ((JLabel) c).getIcon());  // icon tab (Skill Calculator), item grids (Supplies Tracker)
            return;
        }
        if (c instanceof JCheckBox) {
            add(sb, 'C', depth, c, (((JCheckBox) c).isSelected() ? "1" : "0") + text(c));
            return;
        }
        if (c instanceof AbstractButton) {
            if (text(c).equals(">") && addIcon(sb, depth, c, ((AbstractButton) c).getIcon())) return;
            add(sb, 'B', depth, c, text(c));
            return;
        }
        if (c instanceof javax.swing.JTextField) {  // editable: tap opens a text prompt
            add(sb, 'E', depth, c, clean(((JTextComponent) c).getText()));
            return;
        }
        if (c instanceof JTextComponent && ((JTextComponent) c).isEditable()) {  // editable text area (Notes): multi-line prompt
            add(sb, 'A', depth, c, clean(((JTextComponent) c).getText()));
            return;
        }
        if (c instanceof JTextComponent) {  // read-only text: one row per paragraph (JTextPane/JTextArea "\n")
            String t = ((JTextComponent) c).getText();
            int rows = 0;
            if (t != null && t.indexOf('<') < 0) {
                for (String line : t.split("\n")) {
                    String s = clean(line);
                    if (s.isEmpty()) continue;
                    add(sb, 'T', depth, c, s);
                    if (++rows >= MAX_LIST_ROWS) break;
                }
            }
            if (rows == 0) add(sb, 'T', depth, c, clean(t));
            return;
        }
        if (c instanceof JComboBox) {
            Object sel = ((JComboBox) c).getSelectedItem();
            add(sb, 'X', depth, c, sel != null ? comboLabel((JComboBox) c, sel, ((JComboBox) c).getSelectedIndex()) : "");
            return;
        }
        if (c instanceof JScrollPane) {
            walk(scrollPaneView((JScrollPane) c), depth, sb);
            return;
        }
        if (c instanceof javax.swing.JList) {
            walkList((javax.swing.JList) c, depth, sb);
            return;
        }
        if (c instanceof javax.swing.JTable) {
            walkTable((javax.swing.JTable) c, depth, sb);
            return;
        }
        if (c instanceof javax.swing.JTabbedPane) {
            walkTabs((javax.swing.JTabbedPane) c, depth, sb);
            return;
        }
        if (c.getMouseListeners().length > 0) {
            String t = labelText(c, new StringBuilder(), 0).toString().trim();
            if (!t.isEmpty()) add(sb, 'H', depth, c, t);
        }
        if (c instanceof Container) {
            Container k = (Container) c;
            if (k.getLayout() instanceof java.awt.CardLayout) {
                walk(((java.awt.CardLayout) k.getLayout()).__visibleCard(k), depth + 1, sb);
                return;
            }
            for (Component child : k.getComponents()) walk(child, depth + 1, sb);
        }
    }

    /** A JList row in the model: a tap selects it (Sidebar.handle). Text and colour from the cell renderer. */
    static final class ListRow extends JLabel {
        final javax.swing.JList list;
        final int index;

        ListRow(javax.swing.JList list, int index) {
            this.list = list;
            this.index = index;
        }
    }

    private static final int MAX_LIST_ROWS = 400;

    /**
     * JList: one selectable line per model element, the text the list's cell renderer produces
     * (desktop paints the renderer component per row; without a renderer, the element's toString).
     */
    @SuppressWarnings({"rawtypes", "unchecked"})
    private static void walkList(javax.swing.JList list, int depth, StringBuilder sb) {
        javax.swing.ListModel m = list.getModel();
        javax.swing.ListCellRenderer r = list.getCellRenderer();
        int n = m == null ? 0 : m.getSize(), sel = list.getSelectedIndex();
        for (int i = 0; i < n && i < MAX_LIST_ROWS && nodes.size() <= 3000; i++) {
            Object v = m.getElementAt(i);
            String t = "";
            java.awt.Color fg = null;
            int icon = -1;
            if (r != null) {
                try {
                    Component rc = r.getListCellRendererComponent(list, v, i, i == sel, false);
                    if (rc != null) {
                        t = labelText(rc, new StringBuilder(), 0).toString().trim();
                        if (rc instanceof JLabel) {
                            fg = rc.getForeground();
                            icon = iconId(((JLabel) rc).getIcon());  // Bank Memory's item images
                        }
                    }
                } catch (Throwable e) {
                    Log.w(TAG, "list cell renderer threw: " + e);
                }
            }
            if (t.isEmpty()) t = clean(String.valueOf(v));
            ListRow row = new ListRow(list, i);
            if (fg != null) row.setForeground(fg);
            String label = (i == sel ? "> " : "") + t;
            // R: a row with an icon before its text, "<icon id>:<text>"
            if (icon >= 0) add(sb, 'R', depth, row, icon + ":" + label);
            else add(sb, 'H', depth, row, label);
        }
        if (n > MAX_LIST_ROWS) add(sb, 'L', depth, list, "(" + (n - MAX_LIST_ROWS) + " more)");
    }

    /** A JTabbedPane tab header in the model: a tap selects the tab. */
    public static final class TabHead extends JLabel {
        final javax.swing.JTabbedPane pane;
        final int index;

        TabHead(javax.swing.JTabbedPane pane, int index) {
            this.pane = pane;
            this.index = index;
        }

        public boolean isSelected() { return pane.getSelectedIndex() == index; }
    }

    /** JTabbedPane: one header per tab (icon, else title; tooltip as a fallback), then the selected tab's content. */
    private static void walkTabs(javax.swing.JTabbedPane pane, int depth, StringBuilder sb) {
        int n = pane.getTabCount(), sel = pane.getSelectedIndex();
        for (int i = 0; i < n; i++) {
            TabHead h = new TabHead(pane, i);
            javax.swing.Icon icon = (javax.swing.Icon) pane.__tabIcons.get(i);  // headless JTabbedPane's tab lists
            if (icon != null && addIcon(sb, depth, h, icon)) continue;
            String t = clean((String) pane.__tabTitles.get(i));
            if (t.isEmpty()) t = clean((String) pane.__tabTips.get(i));
            if (t.isEmpty()) t = "Tab " + (i + 1);
            h.setText(t);
            add(sb, 'B', depth, h, (i == sel ? "> " : "") + t);
        }
        if (sel >= 0 && sel < n) walk((Component) pane.__tabComps.get(sel), depth + 1, sb);
    }

    /** JTable: a header line of column names, then one text line per model row (cells joined). */
    private static void walkTable(javax.swing.JTable table, int depth, StringBuilder sb) {
        javax.swing.table.TableModel m = table.getModel();
        if (m == null) return;
        int cols = m.getColumnCount(), rows = m.getRowCount();
        StringBuilder h = new StringBuilder();
        for (int j = 0; j < cols; j++) h.append(j > 0 ? "  |  " : "").append(clean(m.getColumnName(j)));
        if (h.length() > 0) add(sb, 'L', depth, table, h.toString());
        for (int i = 0; i < rows && i < MAX_LIST_ROWS && nodes.size() <= 3000; i++) {
            StringBuilder r = new StringBuilder();
            for (int j = 0; j < cols; j++) {
                Object v = m.getValueAt(i, j);
                r.append(j > 0 ? "  |  " : "").append(v == null ? "" : clean(String.valueOf(v)));
            }
            JLabel row = new JLabel();
            add(sb, 'L', depth, row, r.toString());
        }
    }

    private static StringBuilder labelText(Component c, StringBuilder out, int found) {
        if (out.length() > 80 || c == null || !c.isVisible()) return out;
        if (c instanceof JLabel) {
            String t = clean(((JLabel) c).getText());
            if (!t.isEmpty()) out.append(out.length() > 0 ? "  " : "").append(t);
        } else if (c instanceof Container) {
            for (Component child : ((Container) c).getComponents()) labelText(child, out, found);
        }
        return out;
    }

    // Icons sent to the overlay (id per image; pixels are pushed once).
    private static final Map<java.awt.Image, Integer> iconIds = new java.util.WeakHashMap<>();
    private static int nextIconId = 1;
    private static final Map<Integer, Integer> iconHashes = new java.util.HashMap<>();

    /**
     * Icon-only clickable (kind I): text is "<icon id>:<selected 0/1>". Pixels go to the overlay
     * the first time an image is seen. Returns false when the icon has no image.
     */
    private static boolean addIcon(StringBuilder sb, int depth, Component c, javax.swing.Icon icon) {
        int id = iconId(icon);
        if (id < 0) return false;
        boolean sel = false;
        try {
            Method m = c.getClass().getMethod("isSelected");  // MaterialTab, toggle buttons
            sel = Boolean.TRUE.equals(m.invoke(c));
        } catch (Throwable ignored) {
        }
        add(sb, 'I', depth, c, id + ":" + (sel ? 1 : 0) + ":" + Integer.toHexString(iconBackground(c)));
        return true;
    }

    /** The overlay's id for an ImageIcon's image (pushing its pixels when new or changed), or -1. */
    private static int iconId(javax.swing.Icon icon) {
        if (!(icon instanceof javax.swing.ImageIcon)) return -1;
        java.awt.Image img = ((javax.swing.ImageIcon) icon).getImage();
        if (!(img instanceof java.awt.image.BufferedImage)) return -1;
        java.awt.image.BufferedImage b = (java.awt.image.BufferedImage) img;
        int w = b.getWidth(), h = b.getHeight();
        if (w <= 0 || h <= 0 || w > 256 || h > 256) return -1;
        // Pixels are re-sent when they change: item images (AsyncBufferedImage) are blank until
        // the game renders them, and the first push used to stick (Inventory Setups' empty slots).
        int[] px = b.getRGB(0, 0, w, h, null, 0, w);
        int hash = java.util.Arrays.hashCode(px);
        Integer id = iconIds.get(img);
        Integer seen = id != null ? iconHashes.get(id) : null;
        if (id == null) {
            id = nextIconId++;
            iconIds.put(img, id);
        }
        if (seen == null || seen != hash) {
            iconHashes.put(id, hash);
            Native.sidebarIcon(id, w, h, px);
        }
        return id;
    }

    /**
     * ARGB the icon button is drawn on: the background of the icon component or, if it has none
     * of its own, its parent (Inventory Setups paints a slot's panel in the highlight colour when
     * the slot differs from the setup). 0 = the overlay's default.
     */
    private static int iconBackground(Component c) {
        for (Component p : new Component[] {c, c.getParent()}) {
            if (p == null) continue;
            try {
                if (!backgroundSet(p)) continue;  // the headless getBackground() is BLACK when unset
                java.awt.Color bg = p.getBackground();
                if (bg != null && bg.getAlpha() > 0) return bg.getRGB();
            } catch (Throwable ignored) {
            }
        }
        return 0;
    }

    private static java.lang.reflect.Field bgSetField;

    /** AWT's isBackgroundSet(): the generated headless Component keeps it in __BackgroundSet. */
    private static boolean backgroundSet(Component p) {
        try {
            if (bgSetField == null) {
                bgSetField = Component.class.getDeclaredField("__BackgroundSet");
                bgSetField.setAccessible(true);
            }
            return bgSetField.getBoolean(p);
        } catch (Throwable t) {
            return false;
        }
    }

    private static void add(StringBuilder sb, char kind, int depth, Component c, String text) {
        nodes.add(c);
        int color = 0;
        try {
            java.awt.Color fg = c.getForeground();
            if (fg != null && fg != java.awt.Color.BLACK) color = fg.getRGB();
        } catch (Throwable ignored) {
        }
        sb.append(kind).append('\t').append(depth).append('\t').append(Integer.toHexString(color)).append('\t').append(text).append('\n');
    }

    private static String text(Component c) {
        if (c instanceof AbstractButton) {
            String t = clean(((AbstractButton) c).getText());
            if (t.isEmpty() && c instanceof javax.swing.JComponent) t = clean(((javax.swing.JComponent) c).getToolTipText());
            return t.isEmpty() ? ">" : t;
        }
        if (c instanceof JLabel) return clean(((JLabel) c).getText());
        return c.getClass().getSimpleName();
    }

    private static Component scrollPaneView(JScrollPane p) {
        try {
            if (scrollView == null) {
                scrollView = JScrollPane.class.getDeclaredField("__ViewportView");
                scrollView.setAccessible(true);
            }
            Object v = scrollView.get(p);
            if (v instanceof Component) return (Component) v;
        } catch (Throwable ignored) {
        }
        Component[] cs = p.getViewport().getComponents();
        return cs.length > 0 ? cs[0] : null;
    }

    /** Strips HTML and the separators the model format uses. */
    static String clean(String s) {
        if (s == null) return "";
        if (s.indexOf('<') >= 0) {
            s = s.replaceAll("(?i)<br\\s*/?>", " ").replaceAll("<[^>]*>", "").replace("&nbsp;", " ")
                    .replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
        }
        return s.replace('\t', ' ').replace('\n', ' ').trim();
    }

    // --- touch routing: taps on the sidebar go to the overlay instead of the game ---

    private static void installTouchRouter(final int attempt) {
        final Handler main = new Handler(Looper.getMainLooper());
        main.postDelayed(() -> {
            try {
                Activity a = currentActivity();
                if (a == null || a.getWindow() == null) {
                    if (attempt < 60) installTouchRouter(attempt + 1);
                    return;
                }
                final Window w = a.getWindow();
                final Window.Callback orig = w.getCallback();
                InvocationHandler h = (proxy, m, args) -> {
                    if (m.getName().startsWith("dispatch") && m.getName().endsWith("Event")) lastInputMillis = android.os.SystemClock.uptimeMillis();
                    if (m.getName().equals("dispatchTouchEvent") && args != null && args[0] instanceof MotionEvent) {
                        MotionEvent e = (MotionEvent) args[0];
                        if (Native.overlayTouch(e.getActionMasked(), e.getX(), e.getY())) return true;
                        noteTouch(e.getActionMasked(), e.getX(), e.getY());
                        if (TouchMouse.onTouch(e)) return true;
                    }
                    if (m.getName().equals("dispatchGenericMotionEvent") && args != null && args[0] instanceof MotionEvent
                            && ((MotionEvent) args[0]).getActionMasked() == MotionEvent.ACTION_SCROLL) {
                        // the game reads no wheel from ACTION_SCROLL: its notches go to the game's wheel field (desktop's sign: down is positive)
                        float v = ((MotionEvent) args[0]).getAxisValue(MotionEvent.AXIS_VSCROLL);
                        if (v != 0) Native.addWheel(v > 0 ? -Math.max(1, Math.round(v)) : Math.max(1, Math.round(-v)));
                    }
                    if (m.getName().equals("dispatchKeyEvent") && args != null && args[0] instanceof android.view.KeyEvent
                            && KeyRouter.onKey((android.view.KeyEvent) args[0], orig)) return true;
                    try {
                        return m.invoke(orig, args);
                    } catch (InvocationTargetException ex) {
                        throw ex.getCause();
                    }
                };
                w.setCallback((Window.Callback) Proxy.newProxyInstance(Sidebar.class.getClassLoader(), new Class<?>[] {Window.Callback.class}, h));
                Log.i(TAG, "sidebar touch router installed on " + a.getClass().getName());
                try {
                    PromptEnter.install(a);
                } catch (Throwable t) {
                    Log.e(TAG, "prompt enter hook failed", t);
                }
            } catch (Throwable t) {
                Log.e(TAG, "sidebar touch router failed", t);
            }
        }, attempt == 0 ? 500 : 2000);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    static Activity currentActivity() throws Exception {
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("currentActivityThread").invoke(null);
        Field f = at.getDeclaredField("mActivities");
        f.setAccessible(true);
        Map records = (Map) f.get(thread);
        for (Object r : records.values()) {
            Field af = r.getClass().getDeclaredField("activity");
            af.setAccessible(true);
            Activity a = (Activity) af.get(r);
            if (a != null && !a.isFinishing()) return a;
        }
        return null;
    }
}
