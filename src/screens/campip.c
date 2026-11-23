#include "screens/campip.h"
#include "core/app.h"
#include "core/camvid.h"
#include "core/form.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/store.h"
#include "screens/cameras.h"
#include "screens/player.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct {
  int open, paused, err;
  double at, liveAt;
  Camera cam;
  char url[360];
} C;

#define PIP_W 480.0f
#define PIP_H 270.0f
#define PIP_M 48.0f
/* the film's corner is the bottom right; while a film has the screen its buttons are there (and the clock top right,
   the title bottom left), so the camera takes the top left then */
static void pip_rect(float *x, float *y, float *w, float *h) {
  *w = fminf(PIP_W, SB_W * 0.4f);
  *h = *w * PIP_H / PIP_W;
  float m = fminf(PIP_M, SB_W * 0.04f);
  int film = player_open();
  int right = film ? app_rtl() : !app_rtl();
  *x = right ? SB_W - m - *w : m;
  *y = film ? m : SB_H - m - *h;
}
int campip_available(void) {
  return !form_touch() && camvid_available();
}
int campip_active(void) {
  return C.open;
}
static void load(void) {
  C.err = 0;
  C.paused = 0;
  C.at = app_now();
  C.liveAt = 0;
  camvid_load(C.url);
}
void campip_close(void) {
  if (!C.open)
    return;
  C.open = 0;
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
  player_mini_close(); /* a film in the corner and a camera in the corner never show together: the film goes */
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
void campip_tick(void) {
  if (!C.open)
    return;
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
  hit_act(x, y, w, h, expand, 0, 0);
}
/* Play/Pause freezes the camera (its stream is let go, the window keeps its place; Play connects again, live: a camera's
   stream is not paused, it would come back stale); Stop closes it. Only while browsing: a film has the keys otherwise. */
int campip_key(int k) {
  if (!C.open)
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
