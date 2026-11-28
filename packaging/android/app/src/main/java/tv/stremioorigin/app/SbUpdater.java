package tv.stremioorigin.app;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;

/* The in-app update (src/core/update.c on Android): the release's APK is downloaded to the cache, its SHA-256
   checked against the one GitHub lists, and handed to the system's own installer with an ACTION_VIEW intent on a
   FileProvider content:// URI. The installer then owns the whole confirmation (Play Protect, Install/Cancel) as its own
   foreground screen; the app never installs silently. There is no result callback: success shows only when the app
   starts again with the new version. If the user comes back without it (onResume, same version still running), the
   state goes back to "0|" with no message and update.c offers the update again, with no failure shown. The first time,
   Android asks the user to allow installs from this app: the install goes on by itself when they come back (onResume).
   The C side polls the state as "<0|1|->|<message>". */
public final class SbUpdater {
    static final String TAG = "StremioOrigin";
    static volatile String state = "0|";
    static volatile boolean busy;
    static File ready;                 // a checked APK waiting for the permission
    static String readyVersion;
    static boolean waitingPermission;
    static volatile String handedOff;  // the version whose installer screen was shown; null when nothing is out there

public static String poll() { return state; }

    public static boolean start(String url, String sha, String version) {
        if (busy) return true;
        Activity a = MainActivity.self; if (a == null) return false;
        busy = true; state = "0|Downloading…";
        new Thread(() -> {
            try {
                File f = new File(a.getCacheDir(), "update-" + version + ".apk");
                download(url, f);
                String got = sha256(f);
                if (!got.equalsIgnoreCase(sha)) { f.delete(); fail("the download is damaged (checksum " + got.substring(0, 12) + "… instead of " + sha.substring(0, Math.min(12, sha.length())) + "…)"); return; }
                Log.i(TAG, "update: " + f.length() + " bytes, checksum ok");
                a.runOnUiThread(() -> install(a, f, version));
            } catch (Exception e) { Log.w(TAG, "update download", e); fail(e.getMessage() != null ? e.getMessage() : e.toString()); }
        }, "sb-update").start();
        return true;
    }
    static void fail(String why) { state = "-|" + why; busy = false; }

    static void download(String url, File out) throws Exception {
        for (int hop = 0; hop < 8; hop++) {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setInstanceFollowRedirects(false); c.setConnectTimeout(15000); c.setReadTimeout(30000);
            c.setRequestProperty("User-Agent", "StremioOrigin (Android; native)");
            int code = c.getResponseCode();
            if (code >= 300 && code < 400) { String loc = c.getHeaderField("Location"); c.disconnect(); if (loc == null) throw new Exception("redirect without a place"); url = new URL(new URL(url), loc).toString(); continue; }
            if (code != 200) { c.disconnect(); throw new Exception("HTTP " + code); }
            long total = c.getContentLengthLong(), got = 0; int lastPct = -1;
            try (InputStream in = c.getInputStream(); OutputStream o = new FileOutputStream(out)) {
                byte[] buf = new byte[65536]; int r;
                while ((r = in.read(buf)) > 0) { o.write(buf, 0, r); got += r;
                    if (total > 0) { int pct = (int) (got * 100 / total); if (pct != lastPct) { lastPct = pct; state = "0|Downloading… " + pct + "%"; } } }
            }
            c.disconnect(); return;
        }
        throw new Exception("too many redirects");
    }
    static String sha256(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) { byte[] b = new byte[65536]; int r; while ((r = in.read(b)) > 0) md.update(b, 0, r); }
        StringBuilder s = new StringBuilder(); for (byte x : md.digest()) s.append(String.format("%02x", x)); return s.toString();
    }

    static void install(Activity a, File f, String version) {
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            ready = f; readyVersion = version; waitingPermission = true; state = "1|Allow installs from " + a.getString(R.string.app_name) + ", then come back";
            try { a.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()))); }
            catch (Exception e) {
                Log.w(TAG, "update: no install-permission screen", e);
                // (some TV builds have no such screen: the app's own settings page is the next best place to look)
                try { a.startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + a.getPackageName()))); }
                catch (Exception e2) { waitingPermission = false; ready = null; fail("this device does not let apps install updates: install the APK by hand"); }
            }
            return;
        }
        try {
            // The system always asks the user to confirm: no silent update.
            Uri uri = FileProvider.getUriForFile(a, a.getPackageName() + ".fileprovider", f);
            Intent i = new Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            handedOff = version;
            a.startActivity(i);
            state = "1|Confirm the install on screen";   // busy stays true until the user comes back (onResume) or the app restarts
        } catch (Exception e) {
            Log.w(TAG, "update install", e);
            handedOff = null;
            fail("could not start the installer (" + e.getClass().getSimpleName() + (e.getMessage() != null ? ": " + e.getMessage() : "") + ")");
        }
    }
    /* the version of the installed package, "" when it cannot be read */
    static String installedVersion(Activity a) {
        try { String v = a.getPackageManager().getPackageInfo(a.getPackageName(), 0).versionName; return v != null ? v : ""; }
        catch (Exception e) { return ""; }
    }
    static void onResume(Activity a) {
        if (handedOff != null && !waitingPermission) {
            // back from the system installer: a genuine update restarts the process, so still being here with the
            // old version means it was not installed (cancelled, dismissed, blocked). Offer it again, quietly.
            if (!handedOff.equals(installedVersion(a))) {
                Log.i(TAG, "update: back from the installer, " + handedOff + " not installed");
                handedOff = null; state = "0|"; busy = false;
            }
            return;
        }
        if (!waitingPermission || ready == null) return;
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) { fail("installs from this app are not allowed"); waitingPermission = false; return; }
        waitingPermission = false; File f = ready; String v = readyVersion; ready = null; install(a, f, v);
    }
}
