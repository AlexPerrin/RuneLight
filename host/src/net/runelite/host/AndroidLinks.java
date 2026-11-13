package net.runelite.host;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Opens links for LinkBrowser (net.runelite.client.util) the way the official client opens its own
 * external links: the game's native open-URL function (ELF 0x98a214) calls the Java
 * com.jagex.android.ApplicationContext.openBrowser(String), which shows Jagex's confirm dialog ("You
 * are opening a link to an external site. If you remain inactive in game for too long, you will be
 * logged out.") and then starts an ACTION_VIEW intent. The ApplicationContext is the game
 * MainActivity's field of that type (obfuscated name, so found by type). If that reflection fails,
 * fall back to a plain ACTION_VIEW from the game's Activity. Verified 2026-10-18 gen 33 (Wiki search and lookups:
 * the dialog, then Chrome; Cancel stays in game).
 *
 * While the browser is in front the game reconnects every ~16 s (OpenRune 6ab7d07 accepts the reconnect
 * login), so returning within the server's 60 s grace comes back in place (gen 33).
 */
public final class AndroidLinks {
    private AndroidLinks() {}

    /** http(s) only; false when there is no Activity yet. The browser opens on the main thread. */
    public static boolean view(final String url) {
        if (url == null || !(url.startsWith("https://") || url.startsWith("http://"))) {
            Log.w(Host.TAG, "LinkBrowser: not a web link, ignored: " + url);
            return false;
        }
        final Activity a;
        try {
            a = Sidebar.currentActivity();
        } catch (Exception e) {
            Log.e(Host.TAG, "LinkBrowser: no activity", e);
            return false;
        }
        if (a == null) return false;
        if (gameOpenBrowser(a, url)) return true;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                i.addCategory(Intent.CATEGORY_BROWSABLE);
                a.startActivity(i);
                Log.i(Host.TAG, "LinkBrowser: opened " + url);
            } catch (Throwable t) {
                Log.e(Host.TAG, "LinkBrowser: could not open " + url, t);
            }
        });
        return true;
    }

    /** The game's ApplicationContext.openBrowser(url) (it posts its dialog to the UI thread itself). */
    private static boolean gameOpenBrowser(Activity a, String url) {
        try {
            for (Class<?> c = a.getClass(); c != null && c != Activity.class; c = c.getSuperclass()) {
                for (Field f : c.getDeclaredFields()) {
                    if (!f.getType().getName().equals("com.jagex.android.ApplicationContext")) continue;
                    f.setAccessible(true);
                    Object ctx = f.get(a);
                    if (ctx == null) continue;
                    Method m = ctx.getClass().getMethod("openBrowser", String.class);
                    m.invoke(ctx, url);
                    Log.i(Host.TAG, "LinkBrowser: game openBrowser " + url);
                    return true;
                }
            }
            Log.w(Host.TAG, "LinkBrowser: no ApplicationContext on " + a.getClass().getName() + ", plain ACTION_VIEW");
        } catch (Throwable t) {
            Log.w(Host.TAG, "LinkBrowser: game openBrowser failed, plain ACTION_VIEW", t);
        }
        return false;
    }
}
