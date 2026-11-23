#include "screens/campip.h"
#include "core/app.h"
#include "core/camvid.h"
#include "core/form.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/store.h"
#include "screens/cameras.h"
#include "screens/phone.h"
#include "screens/player.h"
#include "core/video.h"
#include "ui/widgets.h"
#include <stdlib.h>
#include "ui/gfx.h"
#include "ui/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct {
  int open, hidden, paused, err; /* hidden: kept alive (connected, silent) while the film has the corner */
  double at, liveAt;
  Camera cam;
  char url[360];
  double volAt; /* the sound level was changed: its chip shows for a moment */
} C;
static int F; /* the window the remote's focus is on: 0 none, 1 the film's, 2 the camera's (pip_focus_*) */
static int lastAud = -1, duck = 100, duckHm;

#define PIP_W 480.0f
#define PIP_H 270.0f
#define PIP_M 48.0f
/* the film's corner is the bottom right; while a film has the screen its buttons are there (and the clock top right,
   the title bottom left), so the camera takes the top left then */
static void pip_rect(float *x, float *y, float *w, float *h) {
  if (form_touch()) { /* a phone: the film's corner above the tab bar; over a film, the top (its buttons are under) */
    if (!player_open()) {
      player_mini_rect(x, y, w, h);
      return;
    }
    *w = fminf(220.0f, SB_W * 0.4f);
    *h = *w * PIP_H / PIP_W;
    *x = app_rtl() ? SB_W - 12 - *w : 12;
    *y = phone_top_inset() + 12;
    return;
  }
  *w = fminf(PIP_W, SB_W * 0.4f);
  *h = *w * PIP_H / PIP_W;
  float m = fminf(PIP_M, SB_W * 0.04f);
  int film = player_open();
  int right = film ? app_rtl() : !app_rtl();
  *x = right ? SB_W - m - *w : m;
  *y = film ? m : SB_H - m - *h;
}
int campip_available(void) {
  return camvid_available();
}
int campip_active(void) {
  return C.open;
}
int campip_visible(void) {
  return C.open && !C.hidden;
}
static int sound_on(void) {
  return strcmp(store_get("camSound", "true"), "false") != 0;
}
static int cam_vol(void) {
  int v = atoi(store_get("camVol", "100"));
  return v < 0 ? 0 : v > 100 ? 100 : v;
}
/* the sound the camera plays now: only the visible window's, and only when sound is on */
static int aud_pct(void) {
  return C.open && !C.hidden && !C.paused && sound_on() ? cam_vol() : 0;
}
static void aud_apply(void) {
  int a = aud_pct();
  if (a == lastAud)
    return;
  lastAud = a;
  camvid_set_audio(a);
}
/* a camera with sound over a full-screen film: the film comes down to the mixing level (camFilmMix) so the camera is
   heard over it; back to its own level when the camera goes. Each film load starts at full volume: set again then. */
static void duck_tick(void) {
  int want = 100;
  if (aud_pct() > 0 && player_open()) {
    want = atoi(store_get("camFilmMix", "40"));
    want = want < 0 ? 0 : want > 100 ? 100 : want;
  }
  int hm = video_has_media();
  if (want != duck || (hm && !duckHm && want != 100)) {
    int base = atoi(store_get("playerVol", "100"));
    video_set_volume((base < 0 ? 0 : base > 100 ? 100 : base) * want / 100, 400);
    duck = want;
  }
  duckHm = hm;
}
static void load(void) {
  C.err = 0;
  C.paused = 0;
  C.at = app_now();
  C.liveAt = 0;
  lastAud = -1;
  camvid_load(C.url);
  aud_apply();
}
void campip_close(void) {
  if (!C.open)
    return;
  C.open = C.hidden = 0;
  if (F == 2)
    F = 0;
  lastAud = -1;
  camvid_unload();
  memset(&C.cam, 0, sizeof C.cam); /* (the password does not stay in memory twice) */
  C.url[0] = 0;
  fprintf(stderr, "[camera] corner closed\n");
}
int campip_open(const Camera *c) {
  if (!campip_available()) {
    toast(T("A camera in the corner is not possible on this device"), 2600, NULL);
    return 0;
  }
  if (camview_covers())
    return 0;
  char url[360];
  if (!cam_url(c, url, sizeof url)) {
    toast(T("Add the stream address first"), 2200, NULL);
    return 0;
  }
  Camera keep = *c; /* (c may be the one open now) */
  if (C.open)
    campip_close();
  player_mini_hide(1); /* a film in the corner and a camera in the corner never show together: the film is kept, hidden */
  C.cam = keep;
  snprintf(C.url, sizeof C.url, "%s", url);
  C.open = 1;
  store_set("pipCam", C.cam.name);
  fprintf(stderr, "[camera] floating %s\n", C.cam.name[0] ? C.cam.name : "camera");
  load();
  return 1;
}
void campip_toggle(void) {
  if (C.open) {
    campip_close();
    return;
  }
  campip_float_last();
}
/* floats the last camera watched or floated (the first when there is none) */
void campip_float_last(void) {
  Camera c;
  if (!cam_get(0, &c)) {
    toast(T("Add a camera in Settings first"), 2400, NULL);
    return;
  }
  const char *last = store_get("pipCam", "");
  for (int i = 0; last[0] && i < CAM_MAX; i++) {
    Camera t;
    if (!cam_get(i, &t))
      break;
    if (!strcmp(t.name, last)) {
      c = t;
      break;
    }
  }
  campip_open(&c);
}
/* the camera next to this one in the list (dir 1 or -1, round the end), in the same window; the one open is not
   reopened when it is the only one */
void campip_step(int dir) {
  if (!C.open)
    return;
  int n = cam_count(), cur = -1;
  Camera t;
  for (int i = 0; i < n && cam_get(i, &t); i++)
    if (!strcmp(t.name, C.cam.name) && !strcmp(t.addr, C.cam.addr))
      cur = i;
  if (n < 2) {
    toast(T("There is no other camera"), 1800, NULL);
    return;
  }
  for (int st = 1; st <= n; st++) {
    int j = (((cur < 0 ? 0 : cur) + dir * st) % n + n) % n;
    if (cam_get(j, &t) && t.addr[0]) {
      int f = F; /* (the focus stays on the window: the new camera takes its place) */
      campip_open(&t);
      F = f;
      return;
    }
  }
}
/* a film takes the corner (hide 1): the camera is kept, hidden; back (hide 0) */
void campip_hide(int hide) {
  if (!C.open || C.hidden == !!hide)
    return;
  C.hidden = !!hide;
  if (hide && F == 2)
    F = 0;
  aud_apply();
}
/* one window shows at a time and both stay alive: the visible one is hidden (its camera or film kept), the other comes */
void campip_swap(void) {
  int film = player_mini();
  if (C.open && !C.hidden && film) {
    campip_hide(1);
    player_mini_hide(0);
  } else if (C.open && C.hidden) {
    campip_hide(0);
    player_mini_hide(1);
  } else if (!C.open && film) {
    campip_float_last(); /* the film steps aside for the camera */
  } else if (C.open && !film) {
    toast(T("There is no film in the corner"), 2000, NULL);
    return;
  } else
    return;
  if (F)
    F = campip_visible() ? 2 : 1;
}
void campip_tick(void) {
  duck_tick();
  if (!C.open)
    return;
  if (C.hidden && !player_mini())
    campip_hide(0); /* the film that had the corner is gone: the camera is back */
  aud_apply();
  camvid_pump();
  if (C.paused || C.err)
    return;
  VidState s = camvid_state();
  if (!C.liveAt && camvid_live() && (s == VID_PLAYING || s == VID_BUFFERING))
    C.liveAt = app_now();
  if (s == VID_ERROR || s == VID_ENDED || (!C.liveAt && app_now() - C.at > 20)) {
    C.err = 1;
    fprintf(stderr, "[camera] corner: no picture\n"); /* (the player's own words are not logged: the log is shared) */
    camvid_unload();
  }
}
static void expand(int a, int b) {
  (void)a;
  (void)b;
  if (!C.open)
    return;
  Camera c = C.cam; /* the viewer closes the corner (and clears it) once it is sure to open */
  cameras_view(&c);
  memset(&c, 0, sizeof c);
}
void campip_draw(void) {
  if (!C.open)
    return;
  if (C.hidden) {
    camvid_set_window(SB_W, SB_H, 0, 0); /* out of sight, still connected */
    return;
  }
  float x, y, w, h;
  pip_rect(&x, &y, &w, &h);
  camvid_set_window(x, y, w, h);
  int live = C.liveAt && !C.err && !C.paused;
  gfx_shadow(x, y + 10, w, h, 18, 36, BLACK_A(170));
  if (live) {
    gfx_hole(x, y, w, h, 16, 1);
    if (app_now() - C.liveAt < 4.0) {
      float k = 1 - (float)fmax(0, (app_now() - C.liveAt - 3.4) / 0.6);
      const char *nm = C.cam.name[0] ? C.cam.name : T("Camera");
      float pw = text_width(nm, 22, 700, 0) + 36;
      gfx_opacity_push(k);
      gfx_rrect(x + 14, y + 14, pw, 40, 20, BLACK_A(150));
      text_draw(nm, x + 32, y + 34 - text_line_height(22) / 2.0f, 22, 700, WHITE_A(240), 0, pw, ALIGN_LEFT);
      gfx_opacity_pop();
    }
  } else {
    gfx_rrect(x, y, w, h, 16, RGBA(12, 12, 16, 245));
    float cx = x + w / 2, cy = y + h / 2;
    if (C.paused) {
      float bw = w / 48, bh = w / 12;
      gfx_rrect(cx - bw * 1.6f, cy - bh / 2, bw, bh, 3, WHITE_A(220));
      gfx_rrect(cx + bw * 0.6f, cy - bh / 2, bw, bh, 3, WHITE_A(220));
    } else if (C.err) {
      text_draw(T("No picture"), cx, cy - text_line_height(26) / 2.0f - 14, 26, 700, WHITE_A(240), 0, w - 32, ALIGN_CENTER);
      text_draw(T("Play to try again"), cx, cy + 14, 20, 400, WHITE_A(170), 0, w - 32, ALIGN_CENTER);
    } else {
      text_draw(T("Connecting\xe2\x80\xa6"), cx, cy - text_line_height(24) / 2.0f, 24, 600, WHITE_A(220), 0, w - 32,
                ALIGN_CENTER);
    }
  }
  gfx_ring(x, y, w, h, 16, 2, WHITE_A(70));
  if (app_now() - C.volAt < 1.6 && live) {
    char b[64];
    if (sound_on() && cam_vol() > 0)
      snprintf(b, sizeof b, "%s %d%%", T("Camera sound"), cam_vol());
    else
      snprintf(b, sizeof b, "%s", T("Camera sound off"));
    float pw = text_width(b, 22, 700, 0) + 36;
    gfx_rrect(x + (w - pw) / 2, y + h - 54, pw, 40, 20, BLACK_A(170));
    text_draw(b, x + (w - pw) / 2 + 18, y + h - 34 - text_line_height(22) / 2.0f, 22, 700, WHITE_A(240), 0, pw, ALIGN_LEFT);
  }
  hit_act(x, y, w, h, expand, 0, 0);
  campip_focus_draw(2, x, y, w, h);
}
/* Play/Pause freezes the camera (its stream is let go, the window keeps its place; Play connects again, live: a camera's
   stream is not paused, it would come back stale); Stop closes it. Only while browsing: a film has the keys otherwise. */
int campip_key(int k) {
  if (!C.open || C.hidden)
    return 0;
  if (k == K_PLAYPAUSE || k == K_PLAY || k == K_PAUSE) {
    int freeze = k == K_PAUSE || (k == K_PLAYPAUSE && !C.paused && !C.err);
    if (k == K_PLAY && !C.paused && !C.err)
      return 1; /* already running */
    if (freeze) {
      C.paused = 1;
      C.liveAt = 0;
      camvid_unload();
    } else
      load();
    return 1;
  }
  if (k == K_STOP) {
    campip_close();
    return 1;
  }
  return 0;
}

/* ---- the remote's focus on a corner window (TV): moving down or sideways off the page's last element lands on it;
   OK expands it, Back or Up gives the focus back to the page. The window's own keys while it has the focus:
   Left/Right the previous and next camera, Down the film or the camera (the other window comes, this one is kept),
   Channel keys / FF / Rew the camera's volume, Yellow its sound on and off. ---- */
static int vis_kind(void) {
  return player_mini_visible() ? 1 : campip_visible() ? 2 : 0;
}
static void vis_rect(int which, float *x, float *y, float *w, float *h) {
  if (which == 1)
    player_mini_rect(x, y, w, h);
  else
    pip_rect(x, y, w, h);
}
int pip_focused(void) {
  return F && F == vis_kind();
}
int pip_focus_try(int kv) {
  if (form_touch() || F || player_open() || (kv != K_DOWN && kv != K_LEFT && kv != K_RIGHT))
    return 0;
  int w = vis_kind();
  if (!w)
    return 0;
  float x, y, ww, h;
  vis_rect(w, &x, &y, &ww, &h);
  float cx = x + ww / 2;
  if (kv == K_DOWN || (kv == K_RIGHT && cx > SB_W / 2) || (kv == K_LEFT && cx < SB_W / 2)) {
    F = w;
    return 1;
  }
  return 0;
}
static void vol_step(int d) {
  int v = cam_vol() + d;
  v = v < 0 ? 0 : v > 100 ? 100 : v;
  char b[8];
  snprintf(b, sizeof b, "%d", v);
  store_set("camVol", b);
  if (d > 0)
    store_set("camSound", "true");
  C.volAt = app_now();
  aud_apply();
}
int pip_focus_key(int k) {
  if (!F)
    return 0;
  if (k == K_CURSOR_SHOW || k == K_CURSOR_HIDE)
    return 0;
  if (F != vis_kind()) { /* the window went (closed, or the other one took the corner) */
    F = 0;
    return 0;
  }
  switch (k) {
  case K_OK:
    if (F == 1) {
      F = 0;
      player_mini_expand();
    } else {
      F = 0;
      expand(0, 0);
    }
    return 1;
  case K_BACK:
  case K_UP:
    F = 0;
    return 1;
  case K_DOWN:
    campip_swap();
    return 1;
  case K_LEFT:
  case K_RIGHT:
    if (F == 2)
      campip_step(k == K_RIGHT ? 1 : -1);
    else
      F = 0;
    return 1;
  case K_CH_UP:
  case K_FF:
    if (F == 2)
      vol_step(10);
    return 1;
  case K_CH_DOWN:
  case K_REW:
    if (F == 2)
      vol_step(-10);
    return 1;
  case K_YELLOW:
    if (F == 2) {
      store_set("camSound", sound_on() ? "false" : "true");
      C.volAt = app_now();
      aud_apply();
    }
    return 1;
  case K_BLUE:
    campip_swap();
    return 1;
  case K_PLAY:
  case K_PAUSE:
  case K_PLAYPAUSE:
  case K_STOP:
    return 0; /* the window's own media keys (the focus stays) */
  default:
    F = 0;
    return 0;
  }
}
/* ---- the buttons a finger has on a corner window (a phone): close, camera, sound, the other window ---- */
static void tb_close(int a, int b) {
  (void)b;
  if (a == 1)
    player_mini_close();
  else
    campip_close();
}
static void tb_step(int a, int b) {
  (void)b;
  campip_step(a);
}
static void tb_sound(int a, int b) {
  (void)a;
  (void)b;
  store_set("camSound", sound_on() ? "false" : "true");
  C.volAt = app_now();
  aud_apply();
}
static void tb_swap(int a, int b) {
  (void)a;
  (void)b;
  campip_swap();
}
static float pill(float x, float y, float h, const char *label, HitFn fn, int a) {
  float w = text_width(label, 15, 700, 0) + 22;
  if (w < h)
    w = h;
  gfx_rrect(x, y, w, h, h / 2, BLACK_A(185));
  text_draw(label, x + w / 2, y + h / 2 - text_line_height(15) / 2.0f, 15, 700, WHITE_A(245), 0, w, ALIGN_CENTER);
  hit_act(x - 3, y - 3, w + 6, h + 6, fn, a, 0);
  return w;
}
void campip_focus_draw(int which, float x, float y, float w, float h) {
  if (form_touch()) {
    float bh = 30, m = 6;
    pill(x + w - m - bh, y + m, bh, "x", tb_close, which); /* (the film's: closes it; the camera's: lets it go) */
    float bx = x + m, by = y + h - bh - m;
    if (which == 2) {
      if (cam_count() > 1) {
        bx += pill(bx, by, bh, "<", tb_step, -1) + 4;
        bx += pill(bx, by, bh, ">", tb_step, 1) + 4;
      }
      bx += pill(bx, by, bh, sound_on() && cam_vol() > 0 ? T("Sound") : T("Muted"), tb_sound, 0) + 4;
    }
    if (which == 2 ? player_mini() : (campip_active() || cam_count() > 0))
      pill(x + w - m - 60, by, bh, which == 2 ? T("Film") : T("Camera"), tb_swap, 0);
    return;
  }
  if (F != which)
    return;
  gfx_ring(x - 5, y - 5, w + 10, h + 10, 20, 4, acc_a(1.0f));
  int other = which == 2 ? player_mini() : (C.open || cam_count() > 0);
  int rows = 2 + (which == 2 && cam_count() > 1) + 0;
  float ph = rows * 48 + 16, py = y - 12 - ph;
  gfx_rrect(x, py, w, ph, 18, BLACK_A(185));
  float ry = py + 32;
  key_hint(x + 18, ry, K_OK, T("Full screen"), WHITE_A(240), 0);
  ry += 48;
  if (which == 2 && cam_count() > 1) {
    float kw = key_hint(x + 18, ry, K_LEFT, "", WHITE_A(240), 0);
    key_hint(x + 18 + kw + 8, ry, K_RIGHT, T("Camera"), WHITE_A(240), 0);
    ry += 48;
  }
  float kw = key_hint(x + 18, ry, K_UP, T("Back"), WHITE_A(240), 0);
  if (other)
    key_hint(x + 18 + kw + 28, ry, K_DOWN, which == 2 ? T("Film") : T("Camera"), WHITE_A(240), 0);
}
