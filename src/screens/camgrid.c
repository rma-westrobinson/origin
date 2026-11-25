#include "screens/camgrid.h"
#include "core/app.h"
#include "core/camgrid.h"
#include "core/cameras.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "screens/btrailer.h"
#include "screens/campip.h"
#include "screens/cameras.h"
#include "screens/player.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/widgets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MARGIN 48.0f
#define GAP 20.0f
#define GIVEUP 20.0 /* seconds without a first picture */

static struct {
  int open, n, cols, rows, f, loaded;
  double at[CAM_MAX], liveAt[CAM_MAX];
  int err[CAM_MAX];
  char name[CAM_MAX][48];
  unsigned gen;
} G;
static Screen gridScr;

int camgrid_supported(void) {
  return camgrid_available();
}
/* columns and rows for n cameras: 1 -> 1x1, 2 -> 2x1, 3-4 -> 2x2, 5-6 -> 3x2, 7-9 -> 3x3, 10-12 -> 4x3 */
static void dims(int n, int *cols, int *rows) {
  int c = n <= 1 ? 1 : n <= 4 ? 2 : n <= 9 ? 3 : 4;
  *cols = c;
  *rows = (n + c - 1) / c;
  if (n == 2) {
    *cols = 2;
    *rows = 1;
  }
}
static void cell_rect(int i, float *x, float *y, float *w, float *h) {
  float cw = (SB_W - 2 * MARGIN - GAP * (G.cols - 1)) / G.cols, ch = (SB_H - 2 * MARGIN - GAP * (G.rows - 1)) / G.rows;
  int c = i % G.cols, r = i / G.cols;
  *x = MARGIN + c * (cw + GAP);
  *y = MARGIN + r * (ch + GAP);
  *w = cw;
  *h = ch;
  if (G.n == 2 && G.cols == 2 && G.rows == 1) { /* two cameras side by side: not stretched to the full height */
    float hh = cw * 9 / 16;
    if (hh < ch) {
      *y += (ch - hh) / 2;
      *h = hh;
    }
  }
}
static void load_all(void) {
  G.n = 0;
  Camera c;
  char url[360];
  camgrid_unload_all();
  for (int i = 0; i < CAM_MAX && cam_get(i, &c); i++) {
    snprintf(G.name[i], sizeof G.name[i], "%s", c.name[0] ? c.name : T("Camera"));
    G.at[i] = app_now();
    G.liveAt[i] = 0;
    G.err[i] = !cam_url(&c, url, sizeof url);
    if (!G.err[i])
      camgrid_load(i, url);
    G.n++;
  }
  memset(&c, 0, sizeof c);
  memset(url, 0, sizeof url);
  dims(G.n, &G.cols, &G.rows);
  if (G.f >= G.n)
    G.f = G.n - 1;
  if (G.f < 0)
    G.f = 0;
  G.gen = cam_generation();
  G.loaded = 1;
}
static void release(void) {
  if (G.loaded)
    camgrid_unload_all();
  G.loaded = 0;
}
void camgrid_close(void) {
  if (!G.open)
    return;
  G.open = 0;
  release();
  if (app_overlay == &gridScr)
    app_overlay = NULL;
}
int camgrid_covers(void) {
  return G.open && app_overlay == &gridScr;
}
void camgrid_open(void) {
  if (!camgrid_available()) {
    toast(T("This device cannot show several cameras at once"), 2500, NULL);
    return;
  }
  player_mini_close(); /* decoders are few: a film kept in the corner goes first */
  btr_release();
  if (player_busy()) {
    toast(T("Close the film first"), 2200, NULL);
    return;
  }
  campip_close();
  G.f = 0;
  G.open = 1;
  G.loaded = 0;
  app_overlay = &gridScr;
}
/* a cell is chosen: the viewer takes the screen (every grid player is let go first: one camera, full screen) */
static void choose(int i) {
  Camera c;
  if (i < 0 || i >= G.n || !cam_get(i, &c))
    return;
  G.f = i;
  release();
  cameras_view(&c); /* the viewer's Back lands on this grid again (it is the overlay under it) */
  memset(&c, 0, sizeof c);
  if (app_overlay == &gridScr && G.open)
    G.loaded = 0; /* the viewer did not open (a toast said why): the grid comes back */
}
static void cell_click(int a, int b) {
  (void)b;
  if (a < 0) { /* the empty page: a tap goes to the add flow */
    camgrid_close();
    cameras_edit(-1);
    return;
  }
  choose(a);
}
static int grid_key(int k) {
  if (!G.open)
    return 0;
  if (k == K_BACK || k == K_EXIT || k == K_STOP) {
    camgrid_close();
    return 1;
  }
  if (!G.n) {
    if (k == K_OK) {
      camgrid_close();
      cameras_edit(-1); /* the add flow */
    }
    return 1;
  }
  int c = G.f % G.cols, r = G.f / G.cols, nf = G.f;
  if (k == K_LEFT && c > 0)
    nf = G.f - 1;
  else if (k == K_RIGHT && c < G.cols - 1 && G.f + 1 < G.n)
    nf = G.f + 1;
  else if (k == K_UP && r > 0)
    nf = G.f - G.cols;
  else if (k == K_DOWN && G.f + G.cols < G.n)
    nf = G.f + G.cols;
  else if (k == K_DOWN && r < G.rows - 1)
    nf = G.n - 1; /* (the last row is short: down lands on its last cell) */
  else if (k == K_OK) {
    if (G.err[G.f]) { /* a cell that failed: OK tries it again */
      load_all();
      return 1;
    }
    choose(G.f);
    return 1;
  }
  G.f = nf;
  return 1;
}
static void grid_tick(float dt) {
  (void)dt;
  if (!G.open || app_overlay != &gridScr)
    return; /* (an overlay keeps ticking after it lets go: while the viewer has the screen, nothing is loaded here) */
  if (!G.loaded || G.gen != cam_generation())
    load_all();
  camgrid_pump();
  app_touch(); /* cameras left on are not an idle TV: no screensaver over them */
  for (int i = 0; i < G.n; i++) {
    if (G.err[i])
      continue;
    VidState s = camgrid_state(i);
    if (!G.liveAt[i] && camgrid_live(i) && (s == VID_PLAYING || s == VID_BUFFERING))
      G.liveAt[i] = app_now();
    if (s == VID_ERROR || s == VID_ENDED || (!G.liveAt[i] && app_now() - G.at[i] > GIVEUP)) {
      G.err[i] = 1;
      G.liveAt[i] = 0;
      fprintf(stderr, "[camgrid] cell %d: no picture\n", i); /* (the player's own words are not logged) */
    }
  }
}
static void grid_draw(void) {
  if (!G.open)
    return;
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A(255)); /* the page; each live cell is cut out of it */
  if (!G.n) {
    float cx = SB_W / 2;
    text_draw(T("No cameras yet"), cx, SB_H / 2 - 90, 52, 700, WHITE_A(255), -0.5f, SB_W - 200, ALIGN_CENTER);
    text_draw_wrapped(T("Add a camera in Settings > Cameras: from your phone, or on the TV with the remote. Then it shows here."),
                      cx - 560, SB_H / 2 - 10, 1120, 3, 30, 400, WHITE_A(210), 0, ALIGN_CENTER, 42);
    key_hint(cx - 160, SB_H / 2 + 150, K_OK, T("Add a camera"), WHITE_A(230), 0);
    key_hint(cx + 120, SB_H / 2 + 150, K_BACK, T("Close"), WHITE_A(230), 0);
    hit_act(0, 0, SB_W, SB_H, cell_click, -1, 0);
    return;
  }
  for (int i = 0; i < G.n; i++) {
    float x, y, w, h;
    cell_rect(i, &x, &y, &w, &h);
    camgrid_set_cell(i, x, y, w, h);
    int live = !G.err[i] && G.liveAt[i] > 0;
    if (live)
      gfx_hole(x, y, w, h, 14, 1);
    else {
      gfx_rrect(x, y, w, h, 14, WHITE_A(18));
      float cx = x + w / 2, cy = y + h / 2;
      if (G.err[i]) {
        text_draw(T("Could not open the camera"), cx, cy - 22, 30, 600, WHITE_A(230), 0, w - 40, ALIGN_CENTER);
        if (i == G.f)
          text_draw(T("Press OK to try again"), cx, cy + 22, 24, 400, WHITE_A(170), 0, w - 40, ALIGN_CENTER);
      } else {
        text_draw(T("Connecting\xe2\x80\xa6"), cx, cy - 14, 28, 500, WHITE_A(200), 0, w - 40, ALIGN_CENTER);
        float ph = (float)fmod(app_now() / 0.9, 1.0) * 6.2832f;
        for (int q = 0; q < 8; q++)
          gfx_circle(cx + cosf(ph + q * 0.2f) * 16, cy + 44 + sinf(ph + q * 0.2f) * 16, 2.5f, WHITE_A(190));
      }
    }
    /* the name, on a dark pill over the picture's lower left */
    const char *nm = G.name[i];
    float pw = text_width(nm, 24, 700, 0) + 36;
    if (pw > w - 28)
      pw = w - 28;
    gfx_rrect(x + 14, y + h - 14 - 44, pw, 44, 22, BLACK_A(160));
    text_draw(nm, x + 32, y + h - 14 - 22 - text_line_height(24) / 2.0f, 24, 700, WHITE_A(245), 0, pw - 24, ALIGN_LEFT);
    if (i == G.f) { /* the focused cell: a thick bright ring outside it, and a glow */
      gfx_ring_out(x, y, w, h, 16, 6, col_alpha(ACC, 1.0f));
      gfx_ring_out(x - 6, y - 6, w + 12, h + 12, 20, 3, WHITE_A(230));
    }
    hit_act(x, y, w, h, cell_click, i, 0);
  }
  key_hint(MARGIN, SB_H - 30, K_BACK, T("Close"), WHITE_A(120), 0);
}
static Screen gridScr = { NULL, NULL, grid_key, grid_tick, grid_draw, NULL, NULL, NULL };
