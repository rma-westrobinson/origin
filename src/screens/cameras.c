#include "screens/cameras.h"
#include "core/app.h"
#include "core/hit.h"
#include "core/httpd.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/store.h"
#include "core/video.h"
#include "core/form.h"
#include "platform/plat.h"
#include "screens/btrailer.h"
#include "screens/campip.h"
#include "screens/player.h"
#include "ui/glass.h"
#include "ui/gfx.h"
#include "ui/osk.h"
#include "ui/qrcard.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ==== the editor ==== */
enum { F_NAME, F_ADDR, F_USER, F_PASS, NFIELD };
#define CW 1100.0f
#define ROW_H 78.0f
#define ROW_GAP 12.0f
static struct {
  int open, idx, f, typing; /* f: the row with the focus: the four fields, then the buttons */
  Camera c;
  double outAt;
  Osk osk;
  float tvH;
} E;
static SGlass glass;
static Screen editScr;
static char *field(int i) {
  return i == F_NAME ? E.c.name : i == F_ADDR ? E.c.addr : i == F_USER ? E.c.user : E.c.pass;
}
static size_t field_cap(int i) {
  return i == F_NAME ? sizeof E.c.name : i == F_ADDR ? sizeof E.c.addr : i == F_USER ? sizeof E.c.user : sizeof E.c.pass;
}
/* the rows under the fields: Save, Watch (once there is an address), Remove (a camera already in the list) */
enum { B_SAVE, B_WATCH, B_REMOVE };
static int buttons(int *out) {
  int n = 0;
  out[n++] = B_SAVE;
  if (E.c.addr[0])
    out[n++] = B_WATCH;
  if (E.idx >= 0)
    out[n++] = B_REMOVE;
  return n;
}
static void close_edit(void) {
  if (!E.open)
    return;
  E.open = 0;
  E.outAt = app_now();
  osk_close(&E.osk);
}
static void do_button(int b) {
  if (b == B_WATCH) {
    cameras_view(&E.c);
    return;
  }
  if (b == B_REMOVE) {
    cam_remove(E.idx);
    toast(T("Camera removed"), 2000, NULL);
    close_edit();
    return;
  }
  if (!E.c.addr[0]) {
    toast(T("Add the stream address first"), 2200, NULL);
    E.f = F_ADDR;
    return;
  }
  if (cam_save(E.idx, &E.c) < 0) {
    toast(T("The list of cameras is full"), 2500, NULL);
    return;
  }
  toast(T("Camera saved"), 2000, NULL);
  close_edit();
}
static int edit_key(int k) {
  if (!E.open) {
    app_overlay = NULL;
    app_key(app_mirror_key(k), 1, 0);
    return 1;
  } /* on its way out: the key is the page's */
  if (E.typing) {
    int r = osk_key(&E.osk, k, field(E.f), field_cap(E.f));
    if (r == 2) { /* Done: on to the next row */
      osk_close(&E.osk);
      E.typing = 0;
      if (E.f < NFIELD)
        E.f++;
    } else if (!r && k == K_UP) { /* off the top row of the keyboard: back to the field's row */
      osk_close(&E.osk);
      E.typing = 0;
    }
    return 1;
  }
  int bt[3], nb = buttons(bt), rows = NFIELD + nb;
  if (k == K_BACK || k == K_EXIT) {
    close_edit();
    return 1;
  }
  if (k == K_UP && E.f > 0)
    E.f--;
  else if (k == K_DOWN && E.f < rows - 1)
    E.f++;
  else if (k == K_OK) {
    if (E.f < NFIELD) {
      E.typing = 1;
      osk_open(&E.osk);
    } else
      do_button(bt[E.f - NFIELD]);
  }
  return 1;
}
static void edit_tick(float dt) {
  sglass_tick(&glass, dt);
  osk_tick(&E.osk, dt);
  if (!E.open && app_overlay == &editScr && app_now() - E.outAt >= 0.25)
    app_overlay = NULL;
}
static void row_focus(int a, int b) {
  (void)b;
  if (!E.typing)
    E.f = a;
}
static void row_act(int a, int b) {
  (void)b;
  if (E.typing)
    return;
  E.f = a;
  edit_key(K_OK);
}
static const char *label_of(int i) {
  static const char *L[NFIELD] = { "Name", "Stream address", "Camera account user", "Camera account password" };
  return T(L[i]);
}
static void edit_draw(void) {
  float a = E.open ? 1 : 1 - (float)((app_now() - E.outAt) / 0.25);
  if (a <= 0)
    return;
  int rtl = app_rtl(), al = rtl ? ALIGN_RIGHT : ALIGN_LEFT;
  gfx_opacity_push(a);
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A(150));
  int bt[3], nb = buttons(bt);
  int typing = E.typing;
  float kb = typing ? osk_height() + 24 : 0;
  float rows = typing ? 1 : NFIELD + nb;
  float H = 56 + text_line_height(40) + 24 + rows * (ROW_H + ROW_GAP) + kb + 40;
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - H / 2;
  if (y < 24)
    y = 24;
  gfx_shadow(x, y + 30, CW, H, 32, 80, BLACK_A(153));
  sglass_draw(&glass, x, y, CW, H, 32);
  gfx_ring(x, y, CW, H, 32, 1, WHITE_A(20));
  float tx = x + 48, tw = CW - 96, yy = y + 40;
  text_draw(T(E.idx >= 0 ? "Camera" : "Add a camera"), rtl ? tx + tw : tx, yy, 40, 700, WHITE_A(255), -0.5f, tw, al);
  yy += text_line_height(40) + 24;
  int first = typing ? E.f : 0, last = typing ? E.f + 1 : NFIELD;
  for (int i = first; i < last; i++) {
    int foc = E.f == i;
    const char *v = field(i);
    char shown[300];
    if (i == F_PASS) { /* the password as dots, kept apart from the camera's list text */
      size_t n = strlen(v);
      if (n > 40)
        n = 40;
      for (size_t k = 0; k < n; k++)
        memcpy(shown + k * 3, "\xe2\x80\xa2", 3);
      shown[n * 3] = 0;
    } else
      snprintf(shown, sizeof shown, "%s", v);
    hit_add(tx, yy, tw, ROW_H, row_focus, row_act, i, 0);
    gfx_rrect(tx, yy, tw, ROW_H, 18, foc ? (uint32_t)(ACC | 0xff000000u) : WHITE_A(18));
    text_draw(label_of(i), rtl ? tx + tw - 24 : tx + 24, yy + 10, 20, 600, foc ? col_alpha(ACC_FG, 0.75f) : WHITE_A(150),
              0.3f, tw / 2, al);
    if (shown[0])
      text_draw(shown, rtl ? tx + tw - 24 : tx + 24, yy + 36, 28, foc ? 600 : 500, foc ? ACC_FG : WHITE_A(255), 0,
                tw - 48, al);
    else
      text_draw(i == F_ADDR ? T("An address such as 192.168.1.50") : T("Empty"), rtl ? tx + tw - 24 : tx + 24, yy + 36, 28,
                400, foc ? col_alpha(ACC_FG, 0.55f) : WHITE_A(90), 0, tw - 48, al);
    if (foc && typing && ((int)(app_now() * 2) & 1)) {
      float cw = text_width(shown, 28, 600, 0);
      gfx_rect(rtl ? tx + tw - 24 - cw - 3 : tx + 24 + cw + 2, yy + 38, 3, text_line_height(28) - 4, ACC_FG);
    }
    yy += ROW_H + ROW_GAP;
  }
  if (!typing) {
    for (int i = 0; i < nb; i++) {
      int foc = E.f == NFIELD + i, b = bt[i];
      const char *lb = b == B_SAVE ? T("Save") : b == B_WATCH ? T("Watch live") : T("Remove this camera");
      hit_add(tx, yy, tw, ROW_H, row_focus, row_act, NFIELD + i, 0);
      uint32_t fill = foc ? (b == B_REMOVE ? (uint32_t)RGB(0xef, 0x8f, 0x8a) : (uint32_t)(ACC | 0xff000000u))
                          : (uint32_t)WHITE_A(18);
      gfx_rrect(tx, yy, tw, ROW_H, 18, fill);
      text_draw(lb, tx + tw / 2, yy + ROW_H / 2 - text_line_height(28) / 2.0f, 28, 700,
                foc ? (b == B_REMOVE ? (uint32_t)RGB(0x72, 0x2a, 0x25) : (uint32_t)ACC_FG) : (uint32_t)WHITE_A(235), 0, tw - 48,
                ALIGN_CENTER);
      yy += ROW_H + ROW_GAP;
    }
  } else if (E.osk.a.v > 0.001f) {
    float kw = 10 * 76 + 9 * 8;
    osk_draw(&E.osk, x + CW / 2 - kw / 2, yy + 12, kw);
  }
  key_hint(rtl ? x + CW - 48 : x + 48, y + H - 40, K_BACK, T(typing ? "Done" : "Close"), WHITE_A(200), rtl);
  gfx_opacity_pop();
}
static Screen editScr = { NULL, NULL, edit_key, edit_tick, edit_draw, NULL, NULL, NULL };
void cameras_edit(int idx) {
  memset(&E.c, 0, sizeof E.c);
  E.idx = -1;
  if (idx >= 0 && cam_get(idx, &E.c))
    E.idx = idx;
  else if (cam_count() >= CAM_MAX) {
    toast(T("The list of cameras is full"), 2500, NULL);
    return;
  }
  E.f = E.idx >= 0 ? NFIELD : F_NAME; /* an existing camera opens on Save, a new one on its name */
  E.typing = 0;
  E.open = 1;
  memset(&E.osk, 0, sizeof E.osk);
  sglass_bake(&glass, SB_W / 2 - CW / 2, 24, CW, SB_H - 48);
  app_overlay = &editScr;
}

/* ==== the phone's page ==== */
static unsigned phoneGen;
static int phone_check(void *ud) {
  (void)ud;
  cam_poll();
  return cam_generation() != phoneGen;
}
void cameras_add_phone(void) {
  if (cam_count() >= CAM_MAX) {
    toast(T("The list of cameras is full"), 2500, NULL);
    return;
  }
  char url[600];
  httpd_url("/camera", url, sizeof url);
  phoneGen = cam_generation();
  QrCardOpts o;
  memset(&o, 0, sizeof o);
  o.title = T("Add a camera");
  o.sub = form_touch() ? T("Open the page and type the camera's stream address and its camera account")
                       : T("Scan with your phone and type the camera's stream address and its camera account");
  o.url = url;
  o.check = phone_check;
  o.doneTitle = T("Camera saved");
  o.doneSub = T("It is in Settings \xe2\x80\xba Cameras");
  o.doneHold = 1.6f;
  qrcard_show(&o);
}

/* ==== the live view ==== */
static struct {
  int open, retry;
  double at, liveAt, hintAt;
  char name[48], url[360], err[300];
  Screen *under;
  double soundAt; /* the sound level was changed: its chip shows */
} V;
static Screen viewScr;
static int viewHm; /* the player had a stream last frame (each load starts at full volume: the camera's level is set again) */
/* the camera's sound in the viewer: the same level as in the corner (Settings > Cameras), 0 when its sound is off */
static int view_level(void) {
  if (!strcmp(store_get("camSound", "true"), "false"))
    return 0;
  int v = atoi(store_get("camVol", "100"));
  return v < 0 ? 0 : v > 100 ? 100 : v;
}
static void view_load(void) {
  V.err[0] = 0;
  V.at = app_now();
  V.liveAt = 0;
  fprintf(stderr, "[camera] opening %s\n", V.name);
  video_load(V.url);
}
static void view_close(void) {
  if (!V.open)
    return;
  V.open = 0;
  video_unload();
  video_set_volume(100, 0); /* (the camera's level was the player's: the next film starts at its own) */
  if (app_overlay == &viewScr)
    app_overlay = V.under;
  V.under = NULL;
}
int camview_covers(void) {
  return V.open && app_overlay == &viewScr;
}
int camview_live(void) {
  return camview_covers() && !V.err[0] && V.liveAt > 0;
}
void camview_close(void) {
  view_close();
}
/* the next (1) or previous (-1) camera in the list takes this view: same screen, the connection made again */
static void view_step(int dir) {
  int n = cam_count(), cur = -1;
  Camera t;
  char u[360];
  for (int i = 0; i < n && cam_get(i, &t); i++)
    if (cam_url(&t, u, sizeof u) && !strcmp(u, V.url))
      cur = i;
  if (n < 2) {
    toast(T("There is no other camera"), 1800, NULL);
    return;
  }
  for (int st = 1; st <= n; st++) {
    int j = (((cur < 0 ? 0 : cur) + dir * st) % n + n) % n;
    if (cam_get(j, &t) && cam_url(&t, u, sizeof u)) {
      snprintf(V.name, sizeof V.name, "%s", t.name[0] ? t.name : T("Camera"));
      snprintf(V.url, sizeof V.url, "%s", u);
      store_set("pipCam", t.name);
      view_load();
      V.hintAt = app_now();
      memset(&t, 0, sizeof t);
      memset(u, 0, sizeof u);
      return;
    }
  }
}
static int view_key(int k) {
  if (k == K_OK && V.err[0]) {
    view_load();
    return 1;
  }
  if (k == K_BACK || k == K_EXIT || k == K_STOP) {
    view_close();
    return 1;
  }
  if (k == K_LEFT || k == K_RIGHT || k == K_CH_UP || k == K_CH_DOWN) { /* another camera, in this same view */
    view_step(k == K_RIGHT || k == K_CH_UP ? 1 : -1);
    return 1;
  }
  if (k == K_UP || k == K_DOWN || k == K_FF || k == K_REW || k == K_YELLOW) { /* the camera's sound: louder, quieter, on/off */
    int v = atoi(store_get("camVol", "100")), d = k == K_UP || k == K_FF ? 10 : k == K_DOWN || k == K_REW ? -10 : 0;
    if (!d) {
      store_set("camSound", view_level() ? "false" : "true");
    } else {
      v = v + d < 0 ? 0 : v + d > 100 ? 100 : v + d;
      char b[8];
      snprintf(b, sizeof b, "%d", v);
      store_set("camVol", b);
      if (d > 0)
        store_set("camSound", "true");
    }
    video_set_volume(view_level(), 0);
    V.soundAt = V.hintAt = app_now();
    return 1;
  }
  V.hintAt = app_now(); /* any other key shows the name again */
  return 1;
}
static void view_tick(float dt) {
  (void)dt;
  if (!V.open)
    return;
  video_pump();
  int hm = video_has_media();
  if (hm && !viewHm)
    video_set_volume(view_level(), 0);
  viewHm = hm;
  VidState s = video_state();
  if (s == VID_ERROR && !V.err[0]) {
    const char *e = video_error();
    snprintf(V.err, sizeof V.err, "%s", e && *e ? e : "");
    fprintf(stderr, "[camera] %s: no picture\n", V.name); /* (the player's own words are not logged: the log is shared) */
  }
  if (!V.err[0] && video_live()) {
    if (!V.liveAt) {
      V.liveAt = app_now();
      V.hintAt = V.liveAt;
    }
    video_set_window(0, 0, SB_W, SB_H);
    app_touch(); /* a camera left on is not an idle TV: no screensaver over it */
  }
  if (!V.err[0] && !V.liveAt && app_now() - V.at > 20) {
    snprintf(V.err, sizeof V.err, "no picture after 20 s");
    video_unload();
  }
}
static void view_click(int a, int b) {
  (void)a;
  (void)b;
  if (V.err[0])
    view_load();
  else
    V.hintAt = app_now();
}
static void view_draw(void) {
  if (!V.open)
    return;
  int live = !V.err[0] && V.liveAt;
  hit_act(0, 0, SB_W, SB_H, view_click, 0, 0);
  if (!live)
    gfx_rect(0, 0, SB_W, SB_H, BLACK_A(255)); /* the picture is not up: a black page; once it is, the window is see-through */
  int rtl = app_rtl();
  if (!live) {
    float cx = SB_W / 2;
    if (V.err[0]) {
      text_draw(T("Could not open the camera"), cx, SB_H / 2 - 90, 44, 700, WHITE_A(255), -0.5f, SB_W - 200, ALIGN_CENTER);
      text_draw_wrapped(T("Check the address, the camera account and that the camera is on the same network as this TV."),
                        cx - 560, SB_H / 2 - 20, 1120, 3, 28, 400, WHITE_A(200), 0, ALIGN_CENTER, 40);
      key_hint(cx - 120, SB_H / 2 + 140, K_OK, T("Try again"), WHITE_A(230), 0);
    } else {
      text_draw(T1("Connecting to {name}\xe2\x80\xa6", "name", V.name), cx, SB_H / 2 - 24, 36, 600, WHITE_A(230), -0.3f,
                SB_W - 200, ALIGN_CENTER);
      float ph = (float)fmod(app_now() / 0.9, 1.0) * 6.2832f;
      for (int q = 0; q < 12; q++) {
        float an = ph + q * 0.14f;
        gfx_circle(cx + cosf(an) * 24, SB_H / 2 + 56 + sinf(an) * 24, 3, WHITE_A(200));
      }
    }
  } else if (app_now() - V.hintAt < 4.0) {
    float k = 1 - (float)fmax(0, (app_now() - V.hintAt - 3.4) / 0.6);
    float w = text_width(V.name, 30, 700, 0) + 64;
    float x = rtl ? SB_W - 72 - w : 72;
    gfx_opacity_push(k);
    gfx_rrect(x, 56, w, 64, 32, BLACK_A(150));
    text_draw(V.name, x + 32, 56 + 32 - text_line_height(30) / 2.0f, 30, 700, WHITE_A(240), 0, w, ALIGN_LEFT);
    key_hint(rtl ? SB_W - 72 : 72, SB_H - 72, K_BACK, T("Close"), WHITE_A(230), rtl);
    float hx = rtl ? SB_W - 72 - 220 : 72 + 220;
    if (cam_count() > 1) {
      float kw = key_hint(hx, SB_H - 72, K_LEFT, "", WHITE_A(230), rtl);
      hx += key_hint(hx + kw + 8, SB_H - 72, K_RIGHT, T("Camera"), WHITE_A(230), rtl) + kw + 40;
    }
    float kv = key_hint(hx, SB_H - 72, K_UP, "", WHITE_A(230), rtl);
    key_hint(hx + kv + 8, SB_H - 72, K_DOWN, T("Sound"), WHITE_A(230), rtl);
    gfx_opacity_pop();
    if (app_now() - V.soundAt < 1.6) {
      char b[64];
      if (view_level() > 0)
        snprintf(b, sizeof b, "%s %d%%", T("Camera sound"), view_level());
      else
        snprintf(b, sizeof b, "%s", T("Camera sound off"));
      float pw = text_width(b, 28, 700, 0) + 48;
      gfx_rrect(SB_W / 2 - pw / 2, SB_H - 150, pw, 56, 28, BLACK_A(170));
      text_draw(b, SB_W / 2 - pw / 2 + 24, SB_H - 150 + 28 - text_line_height(28) / 2.0f, 28, 700, WHITE_A(240), 0, pw,
                ALIGN_LEFT);
    }
  }
}
static Screen viewScr = { NULL, NULL, view_key, view_tick, view_draw, NULL, NULL, NULL };
void cameras_view(const Camera *c) {
  char url[360];
  if (!cam_url(c, url, sizeof url)) {
    toast(T("Add the stream address first"), 2200, NULL);
    return;
  }
  player_mini_close(); /* one decoder: a film kept in the corner goes first (a film full screen is not under Settings) */
  btr_release();
  if (player_busy()) {
    toast(T("Close the film first"), 2200, NULL);
    return;
  }
  Camera keep = *c; /* (c may be the camera in the corner, which this closes: one connection to a camera at a time) */
  campip_close();
  c = &keep;
  store_set("pipCam", c->name); /* the last camera watched is the one the corner shows by default */
  snprintf(V.name, sizeof V.name, "%s", c->name[0] ? c->name : T("Camera"));
  snprintf(V.url, sizeof V.url, "%s", url);
  V.under = app_overlay == &viewScr ? NULL : app_overlay;
  V.open = 1;
  V.hintAt = app_now();
  app_overlay = &viewScr;
  view_load();
}
