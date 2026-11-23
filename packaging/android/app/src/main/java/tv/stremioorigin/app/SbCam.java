package tv.stremioorigin.app;

import android.content.Context;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.VideoSize;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.rtsp.RtspMediaSource;

/* The second player, behind src/platform/android/camvid_android.c: one security camera (RTSP) in a small window while
   SbPlayer has a film or the page. Its own ExoPlayer and its own SurfaceView, added after the film's so it is drawn
   over it, and under SDL's translucent surface (which cuts a hole where the window is). The camera is silent: its audio
   is switched off in the player, and it takes no audio focus, so it never touches the film's sound. Same threading as
   SbPlayer: ExoPlayer on the main thread, commands posted, a snapshot polled by the C side. */
@androidx.annotation.OptIn(markerClass = androidx.media3.common.util.UnstableApi.class)
public final class SbCam {
    static final String TAG = "StremioOrigin";
    static final Handler ui = new Handler(Looper.getMainLooper());
    static Context ctx;
    static ViewGroup box;
    static FrameLayout holder, clip;
    static SurfaceView surface;
    static ExoPlayer player;

    static final double[] snap = new double[2];   // state (as SbPlayer's: 0 idle, 1 loading, 2 playing, 3 paused, 4 buffering, 5 ended, 6 error), first frame shown
    static int flags;                              // 2: the error changed
    static String err = "";
    // on the main thread
    static boolean firstFrame, hasError;
    static int vw, vh;
    static float[] win = { 0, 0, 0, 0 };

    static void attach(Context c, ViewGroup b) {
        ctx = c; box = b;
        clip = new FrameLayout(c); clip.setClipChildren(true); clip.setBackgroundColor(Color.BLACK);
        surface = new SurfaceView(c);
        clip.addView(surface, new FrameLayout.LayoutParams(0, 0));
        holder = new FrameLayout(c) {
            @Override protected void onLayout(boolean ch, int l, int t, int r, int bt) { super.onLayout(ch, l, t, r, bt); place(); }
        };
        holder.addView(clip, new FrameLayout.LayoutParams(0, 0));
        b.addView(holder, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        holder.setVisibility(View.GONE);   // nothing until a camera is loaded
        ui.post(tick);
    }
    static void release() { ui.removeCallbacksAndMessages(null); if (player != null) { player.release(); player = null; } }

    static ExoPlayer make() {
        ExoPlayer p = new ExoPlayer.Builder(ctx).build();   // (audio focus is not handled: the default is off)
        p.setVideoSurfaceView(surface);
        p.setVolume(0f);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon()
            .setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, true).setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());
        p.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) { refresh(); }
            @Override public void onIsPlayingChanged(boolean playing) { refresh(); }
            @Override public void onRenderedFirstFrame() { firstFrame = true; refresh(); }
            @Override public void onVideoSizeChanged(@NonNull VideoSize v) { vw = v.width; vh = v.height; place(); refresh(); }
            @Override public void onPlayerError(@NonNull PlaybackException e) {
                hasError = true; String m = e.getErrorCodeName();   // (the message can carry the address and the account: it is not kept)
                Log.w(TAG, "camera player error " + e.getErrorCodeName());
                synchronized (SbCam.class) { err = m; flags |= 2; } refresh(); }
        });
        return p;
    }

    // ---- commands from C ----
    public static void load(String url) {
        ui.post(() -> {
            if (ctx == null) return;
            if (player == null) player = make();
            firstFrame = hasError = false; vw = vh = 0;
            synchronized (SbCam.class) { err = ""; flags |= 2; }
            holder.setVisibility(View.VISIBLE); place();
            // over TCP (a camera's UDP is often blocked or lossy on a home network), as the full-screen viewer does
            MediaItem item = new MediaItem.Builder().setUri(Uri.parse(url)).build();
            player.setMediaSource(new RtspMediaSource.Factory().setForceUseRtpTcp(true).setTimeoutMs(10000).createMediaSource(item));
            player.prepare(); player.setPlayWhenReady(true);
            refresh();
        });
    }
    public static void unload() {
        ui.post(() -> {
            if (player != null) { player.stop(); player.clearMediaItems(); }
            if (holder != null) holder.setVisibility(View.GONE);
            firstFrame = hasError = false; vw = vh = 0;
            refresh();
        });
    }
    public static void setWindow(float x, float y, float w, float h) { ui.post(() -> { win = new float[] { x, y, w, h }; place(); }); }
    public static String error() { synchronized (SbCam.class) { return err; } }
    public static int poll(double[] out) { synchronized (SbCam.class) { System.arraycopy(snap, 0, out, 0, snap.length); int f = flags; flags = 0; return f; } }

    static final Runnable tick = new Runnable() { @Override public void run() { refresh(); ui.postDelayed(this, 50); } };
    static void refresh() {
        double st = 0;
        if (player != null) {
            int s = player.getPlaybackState();
            if (hasError) st = 6;
            else if (s == Player.STATE_IDLE) st = 0;
            else if (s == Player.STATE_BUFFERING) st = firstFrame ? 4 : 1;
            else if (s == Player.STATE_READY) st = player.getPlayWhenReady() ? 2 : 3;
            else if (s == Player.STATE_ENDED) st = 5;
        }
        synchronized (SbCam.class) { snap[0] = st; snap[1] = firstFrame ? 1 : 0; }
    }

    /* the clip at the window, the picture fitted inside it (the rest is black) */
    static void place() {
        if (clip == null || holder == null) return;
        int W = holder.getWidth(), H = holder.getHeight(); if (W <= 0 || H <= 0) return;
        int cx = Math.round(win[0] * W), cy = Math.round(win[1] * H), cw = Math.round(win[2] * W), ch = Math.round(win[3] * H);
        int sw = cw, sh = ch, sx = 0, sy = 0;
        if (vw > 0 && vh > 0 && cw > 0 && ch > 0) { double ar = (double) vw / vh;
            if ((double) cw / ch > ar) { sw = (int) Math.round(ch * ar); sx = (cw - sw) / 2; } else { sh = (int) Math.round(cw / ar); sy = (ch - sh) / 2; } }
        FrameLayout.LayoutParams lc = (FrameLayout.LayoutParams) clip.getLayoutParams();
        if (lc.leftMargin != cx || lc.topMargin != cy || lc.width != cw || lc.height != ch) { lc.leftMargin = cx; lc.topMargin = cy; lc.width = Math.max(1, cw); lc.height = Math.max(1, ch); clip.setLayoutParams(lc); }
        FrameLayout.LayoutParams ls = (FrameLayout.LayoutParams) surface.getLayoutParams();
        if (ls.leftMargin != sx || ls.topMargin != sy || ls.width != sw || ls.height != sh) { ls.leftMargin = sx; ls.topMargin = sy; ls.width = Math.max(1, sw); ls.height = Math.max(1, sh); surface.setLayoutParams(ls); }
    }
}
