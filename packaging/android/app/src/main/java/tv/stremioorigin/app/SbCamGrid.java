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

/* The camera grid (src/screens/camgrid.c, src/platform/android/camgrid_android.c): several cameras at once, one
   ExoPlayer and one SurfaceView per cell. Each cell is loaded exactly as SbCam loads the corner camera (the same
   RtspMediaSource over TCP, the same timeout), with the sound always off. SbCam itself is untouched: it still serves the
   corner window and this class never shares a player with it. Same threading as SbCam: ExoPlayer on the main thread,
   commands posted, a snapshot polled by the C side. */
@androidx.annotation.OptIn(markerClass = androidx.media3.common.util.UnstableApi.class)
public final class SbCamGrid {
    static final String TAG = "StremioOrigin";
    static final int MAX = 12;
    static final Handler ui = new Handler(Looper.getMainLooper());
    static Context ctx;
    static ViewGroup box;
    static FrameLayout holder;
    static final Cell[] cells = new Cell[MAX];
    static final double[] snap = new double[MAX * 2];   // per cell: state (0 idle, 1 loading, 2 playing, 3 paused, 4 buffering, 5 ended, 6 error), first frame shown

    static final class Cell {
        FrameLayout clip; SurfaceView surface; ExoPlayer player;
        boolean firstFrame, hasError; int vw, vh;
        float[] win = { 0, 0, 0, 0 };
    }

    static void attach(Context c, ViewGroup b) {
        ctx = c; box = b;
        holder = new FrameLayout(c) {
            @Override protected void onLayout(boolean ch, int l, int t, int r, int bt) { super.onLayout(ch, l, t, r, bt); for (int i = 0; i < MAX; i++) place(i); }
        };
        b.addView(holder, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        holder.setVisibility(View.GONE);
        ui.post(tick);
    }
    static void release() { ui.removeCallbacksAndMessages(null); for (int i = 0; i < MAX; i++) if (cells[i] != null && cells[i].player != null) { cells[i].player.release(); cells[i].player = null; } }

    static Cell cell(int i) {
        if (cells[i] == null) {
            Cell k = new Cell();
            k.clip = new FrameLayout(ctx); k.clip.setClipChildren(true); k.clip.setBackgroundColor(Color.BLACK);
            k.surface = new SurfaceView(ctx);
            k.clip.addView(k.surface, new FrameLayout.LayoutParams(0, 0));
            holder.addView(k.clip, new FrameLayout.LayoutParams(0, 0));
            cells[i] = k;
        }
        return cells[i];
    }
    static ExoPlayer make(final int i, final Cell k) {
        ExoPlayer p = new ExoPlayer.Builder(ctx).build();
        p.setVideoSurfaceView(k.surface);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon()
            .setTrackTypeDisabled(C.TRACK_TYPE_AUDIO, true).setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());
        p.setVolume(0f);
        p.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) { refresh(); }
            @Override public void onRenderedFirstFrame() { k.firstFrame = true; refresh(); }
            @Override public void onVideoSizeChanged(@NonNull VideoSize v) { k.vw = v.width; k.vh = v.height; place(i); refresh(); }
            @Override public void onPlayerError(@NonNull PlaybackException e) {
                k.hasError = true;   // (the message can carry the address and the account: only the code is logged)
                Log.w(TAG, "camera grid player error " + e.getErrorCodeName()); refresh(); }
        });
        return p;
    }

    // ---- commands from C ----
    public static void load(final int i, final String url) {
        ui.post(() -> {
            if (ctx == null || i < 0 || i >= MAX) return;
            Cell k = cell(i);
            if (k.player == null) k.player = make(i, k);
            k.firstFrame = k.hasError = false; k.vw = k.vh = 0;
            holder.setVisibility(View.VISIBLE); k.clip.setVisibility(View.VISIBLE); place(i);
            MediaItem item = new MediaItem.Builder().setUri(Uri.parse(url)).build();
            k.player.setMediaSource(new RtspMediaSource.Factory().setForceUseRtpTcp(true).setTimeoutMs(10000).createMediaSource(item));
            k.player.prepare(); k.player.setPlayWhenReady(true);
            refresh();
        });
    }
    /* every cell is let go: the connections and the decoders (the viewer and the rest of the app need them) */
    public static void unloadAll() {
        ui.post(() -> {
            for (int i = 0; i < MAX; i++) {
                Cell k = cells[i]; if (k == null) continue;
                if (k.player != null) { k.player.stop(); k.player.clearMediaItems(); k.player.release(); k.player = null; }
                k.firstFrame = k.hasError = false; k.vw = k.vh = 0; k.clip.setVisibility(View.GONE);
            }
            if (holder != null) holder.setVisibility(View.GONE);
            refresh();
        });
    }
    public static void setCell(final int i, final float x, final float y, final float w, final float h) {
        ui.post(() -> { if (i < 0 || i >= MAX) return; cell(i).win = new float[] { x, y, w, h }; place(i); });
    }
    public static void poll(double[] out) { synchronized (SbCamGrid.class) { System.arraycopy(snap, 0, out, 0, snap.length); } }

    static final Runnable tick = new Runnable() { @Override public void run() { refresh(); ui.postDelayed(this, 100); } };
    static void refresh() {
        synchronized (SbCamGrid.class) {
            for (int i = 0; i < MAX; i++) {
                Cell k = cells[i]; double st = 0;
                if (k != null && k.player != null) {
                    int s = k.player.getPlaybackState();
                    if (k.hasError) st = 6;
                    else if (s == Player.STATE_IDLE) st = 0;
                    else if (s == Player.STATE_BUFFERING) st = k.firstFrame ? 4 : 1;
                    else if (s == Player.STATE_READY) st = k.player.getPlayWhenReady() ? 2 : 3;
                    else if (s == Player.STATE_ENDED) st = 5;
                }
                snap[i * 2] = st; snap[i * 2 + 1] = (k != null && k.firstFrame) ? 1 : 0;
            }
        }
    }

    /* the clip at the cell, the picture fitted inside it (the rest is black) */
    static void place(int i) {
        Cell k = cells[i];
        if (k == null || holder == null) return;
        int W = holder.getWidth(), H = holder.getHeight(); if (W <= 0 || H <= 0) return;
        int cx = Math.round(k.win[0] * W), cy = Math.round(k.win[1] * H), cw = Math.round(k.win[2] * W), ch = Math.round(k.win[3] * H);
        int sw = cw, sh = ch, sx = 0, sy = 0;
        if (k.vw > 0 && k.vh > 0 && cw > 0 && ch > 0) { double ar = (double) k.vw / k.vh;
            if ((double) cw / ch > ar) { sw = (int) Math.round(ch * ar); sx = (cw - sw) / 2; } else { sh = (int) Math.round(cw / ar); sy = (ch - sh) / 2; } }
        FrameLayout.LayoutParams lc = (FrameLayout.LayoutParams) k.clip.getLayoutParams();
        if (lc.leftMargin != cx || lc.topMargin != cy || lc.width != cw || lc.height != ch) { lc.leftMargin = cx; lc.topMargin = cy; lc.width = Math.max(1, cw); lc.height = Math.max(1, ch); k.clip.setLayoutParams(lc); }
        FrameLayout.LayoutParams ls = (FrameLayout.LayoutParams) k.surface.getLayoutParams();
        if (ls.leftMargin != sx || ls.topMargin != sy || ls.width != sw || ls.height != sh) { ls.leftMargin = sx; ls.topMargin = sy; ls.width = Math.max(1, sw); ls.height = Math.max(1, sh); k.surface.setLayoutParams(ls); }
    }
}
