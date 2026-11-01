package net.runelite.host;

import android.app.Activity;
import android.app.Application;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * The browser's part of the AppAuth sign-in, for a login a tool asked for (Client.setGameState(LOGGING_IN)): when
 * AppAuth's AuthorizationManagementActivity is about to start its browser on a local authorization endpoint, the
 * agent requests that URL itself and hands the app the redirect, as the browser would (the app's own state check
 * and token exchange run unchanged). Any other endpoint is left to the browser: nothing here contacts a remote host.
 */
final class LoginAgent implements Application.ActivityLifecycleCallbacks {
    private static final String TAG = "osrsrl-login";
    private static boolean armed;

    /** Registers the agent once; the next AppAuth sign-in on a local endpoint goes through it. */
    static synchronized void arm() {
        if (armed) return;
        try {
            Activity a = Sidebar.currentActivity();
            if (a == null) return;
            a.getApplication().registerActivityLifecycleCallbacks(new LoginAgent());
            armed = true;
        } catch (Throwable t) {
            Log.e(TAG, "login agent not armed", t);
        }
    }

    static boolean local(Uri u) {
        String h = u == null ? null : u.getHost();
        return u != null && ("http".equals(u.getScheme()) || "https".equals(u.getScheme()))
            && ("10.0.2.2".equals(h) || "127.0.0.1".equals(h) || "localhost".equals(h));
    }

    @Override public void onActivityPreCreated(Activity activity, Bundle saved) {
        Intent i = activity.getIntent();
        if (saved != null || i == null || !activity.getClass().getName().equals("net.openid.appauth.AuthorizationManagementActivity")) return;
        Intent auth = i.getParcelableExtra("authIntent");
        Uri url = auth == null ? null : auth.getData();
        if (!local(url) || i.getBooleanExtra("authStarted", false)) return;
        String[] location = new String[1];
        Thread t = new Thread(() -> {  // the main thread may not open a socket; it waits on this one request
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(url.toString()).openConnection();
                c.setInstanceFollowRedirects(false);
                c.getResponseCode();
                location[0] = c.getHeaderField("Location");
                c.disconnect();
            } catch (Throwable e) {
                Log.e(TAG, "authorization request failed", e);
            }
        });
        t.start();
        try { t.join(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        if (location[0] == null) { Log.i(TAG, "authorization endpoint gave no redirect; the browser opens"); return; }
        // AuthorizationManagementActivity reads authStarted from its extras: it then handles the redirect at onResume
        i.putExtra("authStarted", true);
        i.setData(Uri.parse(location[0]));
        Log.i(TAG, "authorization redirect handed to the app");
    }

    @Override public void onActivityCreated(Activity a, Bundle b) {}
    @Override public void onActivityStarted(Activity a) {}
    @Override public void onActivityResumed(Activity a) {}
    @Override public void onActivityPaused(Activity a) {}
    @Override public void onActivityStopped(Activity a) {}
    @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
    @Override public void onActivityDestroyed(Activity a) {}
}
