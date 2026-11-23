#include "core/party.h"
#include "core/app.h"
#include "core/form.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "core/net.h"
#include "core/store.h"
#include "core/stremio.h"
#include "platform/plat.h"
#include "screens/player.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "cJSON.h"
#include "sbcrypto.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The protocol is in server/watchparty/README.md. In short: the inbox (GET inbox/poll, POST inbox/invite) says which of
   this account's devices are online and carries invites; a room (party/<code>/...) carries the play, pause, seek and
   load commands, one event after another with a number, and a snapshot of where the film is. */

#define NMAX 8
typedef struct {
  char dev[48], name[48];
} PDev;
typedef struct {
  char type[16], id[64], vid[80], name[200];
} PMedia;

static struct {
  /* identity */
  char acct[72], dev[24], me[48];
  /* the party */
  int in, gen, hasHost;
  char code[16], host[48];
  long seq;
  PDev mem[NMAX];
  int nmem;
  PMedia room; /* what the party watches */
  int roomOk;
  /* the inbox */
  PDev online[NMAX];
  int nonline;
  double nextInbox, nextPoll;
  int busyInbox, busyPoll, fails;
  long lastInv; /* the last invite shown (or answered) */
  struct {
    int show;
    long id;
    char code[16], from[48];
    PMedia m;
    double at;
    int focus; /* 0 Accept, 1 Decline */
  } inv;
  /* syncing */
  int base, bState, announce, seenInit;
  double bPos, bAt, suppressUntil, sentAt, corrAt, seekSentAt;
  PMedia seen; /* the last title seen playing here */
  int pending, pendSt;
  double pendPos, pendAt, pendUntil;
  /* the menu */
  int menu, menuIdx, creating;
  struct {
    int act, arg;
    char lb[120];
  } row[16];
  int nrow;
  /* where the cards were drawn (layout units), for clicks */
  float rx[16], ry[16], rw[16], rh[16], cardX, cardY, cardW, cardH, ax, ay, aw, ah, dx, dy, dw, dh;
} Pt;

/* ---- the server ---- */
static const char *url_base(void) {
  const char *u = getenv("SB_PARTY_URL");
  if (!u || !*u)
    u = store_get("partyUrl", "");
  return u && *u ? u : PARTY_DEFAULT_URL;
}
static int url_ok(void) {
  const char *u = url_base();
  return !strstr(u, ".example.") && !strncmp(u, "http", 4);
}
static int enabled(void) {
  return strcmp(store_get("partyOn", "true"), "false") != 0;
}
int party_available(void) {
  return enabled() && url_ok() && sb_signed_in() && !acct_is_nuvio();
}
int party_active(void) {
  return Pt.in;
}
/* the account's opaque name on the server: a hash of the Stremio user id, never the sign-in key */
static int ident(void) {
  if (!sb_signed_in() || acct_is_nuvio())
    return 0;
  const char *uid = sb_user_id();
  if (!uid || !*uid)
    uid = sb_user_email();
  if (!uid || !*uid)
    return 0;
  char in[200];
  snprintf(in, sizeof in, "origin-party-v1|%s", uid);
  uint8_t h[32];
  sbc_sha256(in, strlen(in), h);
  for (int i = 0; i < 32; i++)
    snprintf(Pt.acct + i * 2, 3, "%02x", h[i]);
  if (!Pt.dev[0]) {
    const char *d = store_get("partyDev", "");
    if (strlen(d) >= 8)
      snprintf(Pt.dev, sizeof Pt.dev, "%s", d);
    else {
      uint8_t r[8];
      if (!plat_random(r, 8))
        for (int i = 0; i < 8; i++)
          r[i] = (uint8_t)(rand() >> 3);
      for (int i = 0; i < 8; i++)
        snprintf(Pt.dev + i * 2, 3, "%02x", r[i]);
      store_set("partyDev", Pt.dev);
    }
  }
  const char *n = plat_device_name();
  snprintf(Pt.me, sizeof Pt.me, "%s", n && *n ? n : "Device");
  return 1;
}
static void headers(char *h, size_t cap) {
  const char *k = store_get("partyKey", "");
  snprintf(h, cap, "%s%s", *k ? "X-Party-Key: " : "", k);
}
static unsigned post(const char *path, cJSON *body, NetCb cb, void *ud) {
  char url[400], hd[200];
  snprintf(url, sizeof url, "%s/v1/%s", url_base(), path);
  headers(hd, sizeof hd);
  cJSON_AddStringToObject(body, "a", Pt.acct);
  cJSON_AddStringToObject(body, "dev", Pt.dev);
  cJSON_AddStringToObject(body, "name", Pt.me);
  char *js = cJSON_PrintUnformatted(body);
  unsigned id = net_post_bg(url, js, "application/json", hd, 10000, cb, ud); /* (the previews' lane: not behind the addons) */
  free(js);
  cJSON_Delete(body);
  return id;
}
static void jstr(const cJSON *o, const char *k, char *out, size_t cap) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, k);
  snprintf(out, cap, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}
static double jnum(const cJSON *o, const char *k, double d) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, k);
  return cJSON_IsNumber(v) ? v->valuedouble : d;
}
static int jbool(const cJSON *o, const char *k) {
  return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive((cJSON *)o, k));
}
static void media_read(const cJSON *m, PMedia *out) {
  memset(out, 0, sizeof *out);
  if (!cJSON_IsObject(m))
    return;
  jstr(m, "type", out->type, sizeof out->type);
  jstr(m, "id", out->id, sizeof out->id);
  jstr(m, "vid", out->vid, sizeof out->vid);
  jstr(m, "name", out->name, sizeof out->name);
}
static cJSON *media_json(const PMedia *m) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "type", m->type);
  cJSON_AddStringToObject(o, "id", m->id);
  cJSON_AddStringToObject(o, "vid", m->vid);
  cJSON_AddStringToObject(o, "name", m->name);
  return o;
}
static int same_media(const PMedia *a, const PMedia *b) {
  if (!a->id[0] || strcmp(a->id, b->id))
    return 0;
  return !a->vid[0] || !b->vid[0] || !strcmp(a->vid, b->vid);
}
/* what plays here now (its picture up), 0 when nothing does */
static int local_media(PMedia *m) {
  memset(m, 0, sizeof *m);
  return player_now(m->type, m->id, m->vid, m->name, 200) && m->id[0];
}

/* ---- leaving, the room's people ---- */
static void drop(const char *why) {
  Pt.in = 0;
  Pt.gen++;
  Pt.code[0] = 0;
  Pt.pending = Pt.base = 0;
  Pt.nmem = 0;
  Pt.roomOk = 0;
  if (why && *why)
    toast(why, 3000, NULL);
}
static void ignore_cb(int st, const unsigned char *b, size_t n, void *ud) {
  (void)st;
  (void)b;
  (void)n;
  (void)ud;
}
void party_leave(void) {
  if (!Pt.in)
    return;
  char path[64];
  snprintf(path, sizeof path, "party/%s/leave", Pt.code);
  post(path, cJSON_CreateObject(), ignore_cb, NULL);
  drop(T("You left the watch party"));
} /* (the film plays on: only the syncing stops) */
static void members_read(const cJSON *arr) {
  Pt.nmem = 0;
  const cJSON *m;
  cJSON_ArrayForEach(m, arr) {
    if (Pt.nmem >= NMAX)
      break;
    jstr(m, "dev", Pt.mem[Pt.nmem].dev, sizeof Pt.mem[0].dev);
    jstr(m, "name", Pt.mem[Pt.nmem].name, sizeof Pt.mem[0].name);
    Pt.nmem++;
  }
}

/* ---- applying what the others do ---- */
static int is_live(double *pos, double *du, int *st) {
  *st = player_cast_state(pos, du, NULL, 0);
  return *st == 2 || *st == 3;
}
static void apply(int playing, double pos) {
  double p, d;
  int st;
  if (!is_live(&p, &d, &st))
    return;
  double now = app_now();
  if (fabs(p - pos) > 1.5)
    player_cast(3, pos < 0 ? 0 : pos);
  if (playing && st == 3)
    player_cast(2, 0);
  else if (!playing && st == 2)
    player_cast(1, 0);
  Pt.suppressUntil = now + 2.0; /* what this did is not this device's own doing: no echo */
  Pt.base = 0;
}
static void follow_media(const PMedia *m) { /* the party moved on to another title */
  PMedia mine;
  if (!m->id[0])
    return;
  if (local_media(&mine) && same_media(m, &mine))
    return;
  if (player_busy() || player_waiting()) {
    toast(T1("The party is watching {t}", "t", m->name[0] ? m->name : m->id), 5000, NULL);
    return; /* a film is open here: the party does not take it over */
  }
  toast(T1("Joining the party: {t}", "t", m->name[0] ? m->name : m->id), 4000, NULL);
  player_choose_best(m->type, m->id, m->vid);
}
static void on_event(const cJSON *ev) {
  char type[16], who[48];
  jstr(ev, "type", type, sizeof type);
  jstr(ev, "name", who, sizeof who);
  double pos = jnum(ev, "pos", 0);
  if (!strcmp(type, "play") || !strcmp(type, "pause") || !strcmp(type, "seek")) {
    double p, d;
    int st;
    if (!is_live(&p, &d, &st))
      return;
    PMedia mine;
    if (local_media(&mine) && Pt.roomOk && !same_media(&Pt.room, &mine))
      return; /* another title here: not this party's film */
    apply(!strcmp(type, "seek") ? st == 2 : !strcmp(type, "play"), pos);
  } else if (!strcmp(type, "load")) {
    media_read(cJSON_GetObjectItemCaseSensitive((cJSON *)ev, "media"), &Pt.room);
    Pt.roomOk = Pt.room.id[0] != 0;
    follow_media(&Pt.room);
  } else if (!strcmp(type, "join"))
    toast(T1("{n} joined the watch party", "n", who), 2600, NULL);
  else if (!strcmp(type, "leave"))
    toast(T1("{n} left the watch party", "n", who), 2600, NULL);
  else if (!strcmp(type, "decline"))
    toast(T1("{n} can't join right now", "n", who), 3200, NULL);
  else if (!strcmp(type, "chat")) {
    char text[300], line[400];
    jstr(ev, "text", text, sizeof text);
    snprintf(line, sizeof line, "%s: %s", who, text);
    toast(line, 4500, NULL);
  }
}
static void on_poll(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len;
  if ((int)(intptr_t)ud != Pt.gen)
    return;
  Pt.busyPoll = 0;
  if (status == 403 || status == 404) {
    drop(T("The watch party has ended"));
    return;
  }
  if (status != 200) {
    if (++Pt.fails > 20)
      drop(T("Lost the watch party"));
    return;
  }
  Pt.fails = 0;
  cJSON *j = net_json(body);
  if (!j)
    return;
  const cJSON *evs = cJSON_GetObjectItemCaseSensitive(j, "events"), *e;
  cJSON_ArrayForEach(e, evs) {
    long s = (long)jnum(e, "seq", 0);
    if (s <= Pt.seq)
      continue;
    Pt.seq = s;
    char dev[48];
    jstr(e, "dev", dev, sizeof dev);
    if (strcmp(dev, Pt.dev))
      on_event(e);
  }
  members_read(cJSON_GetObjectItemCaseSensitive(j, "members"));
  jstr(j, "host", Pt.host, sizeof Pt.host);
  /* drift: the party's own clock against this film, corrected when it is well off (never right after this device's
     own command, nor while the film is not playing here as it plays there) */
  double p, d, now = app_now();
  int st, playing = jbool(j, "playing");
  double pos = jnum(j, "pos", 0);
  PMedia mine;
  if (is_live(&p, &d, &st) && st == 2 && playing && now > Pt.suppressUntil && now - Pt.sentAt > 3 && now - Pt.corrAt > 8 &&
      local_media(&mine) && same_media(&Pt.room, &mine) && fabs(p - pos) > 3) {
    player_cast(3, pos);
    Pt.corrAt = now;
    Pt.suppressUntil = now + 2.0;
    Pt.base = 0;
  }
  net_json_free(j);
}
static void send_event(const char *type, double pos, const PMedia *m) {
  if (!Pt.in)
    return;
  char path[64];
  cJSON *b = cJSON_CreateObject();
  snprintf(path, sizeof path, "party/%s/event", Pt.code);
  cJSON_AddStringToObject(b, "type", type);
  cJSON_AddNumberToObject(b, "pos", pos);
  if (m)
    cJSON_AddItemToObject(b, "media", media_json(m));
  post(path, b, ignore_cb, NULL);
  Pt.sentAt = app_now();
}

/* ---- the film here, against the party's ---- */
static void local_watch(void) {
  double now = app_now(), p, d;
  PMedia mine;
  if (!local_media(&mine)) {
    Pt.base = 0;
    return;
  }
  if (!same_media(&mine, &Pt.seen)) { /* a title newly up here */
    Pt.seen = mine;
    Pt.base = 0;
    if (Pt.seenInit && !Pt.pending && !(Pt.roomOk && same_media(&Pt.room, &mine))) {
      Pt.room = mine; /* started here after joining: the party goes to it */
      Pt.roomOk = 1;
      Pt.announce = 1;
      send_event("load", 0, &mine);
    }
    Pt.seenInit = 1;
    return;
  }
  if (!Pt.roomOk || !same_media(&Pt.room, &mine))
    return; /* another title here than the party's: this device is not in step */
  int st = player_cast_state(&p, &d, NULL, 0);
  if (st == 0 || st >= 4 || d <= 0) {
    Pt.base = 0;
    return;
  }
  if (!Pt.base) {
    if (st == 1)
      return;
    Pt.base = 1;
    Pt.bState = st;
    Pt.bPos = p;
    Pt.bAt = now;
    if (Pt.announce) { /* this device brought the title: the party is told how it stands */
      send_event(st == 2 ? "play" : "pause", p, NULL);
      Pt.announce = 0;
    }
    return;
  }
  if (now >= Pt.suppressUntil) {
    int eff = st == 1 ? Pt.bState : st; /* buffering is neither */
    if (eff != Pt.bState) {
      send_event(eff == 2 ? "play" : "pause", p, NULL);
      Pt.bState = eff;
    } else if (fabs(p - (Pt.bPos + (st == 2 ? now - Pt.bAt : 0))) > 2.5 && now - Pt.seekSentAt > 0.7) {
      send_event("seek", p, NULL);
      Pt.seekSentAt = now;
    }
  } else if (st != 1)
    Pt.bState = st;
  Pt.bPos = p;
  Pt.bAt = now;
}
/* a joiner's film arriving: put it where the party is */
static void pending_tick(void) {
  double now = app_now();
  if (now > Pt.pendUntil) {
    Pt.pending = 0;
    return;
  }
  PMedia mine;
  double p, d;
  int st;
  if (!local_media(&mine) || !same_media(&Pt.room, &mine) || !is_live(&p, &d, &st))
    return;
  apply(Pt.pendSt == 2, Pt.pendPos + (Pt.pendSt == 2 ? now - Pt.pendAt : 0));
  Pt.pending = 0;
  Pt.seen = mine;
  Pt.seenInit = 1;
}

/* ---- starting, joining, inviting ---- */
static void on_joined(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len;
  if ((int)(intptr_t)ud != Pt.gen)
    return;
  cJSON *j = status == 200 ? net_json(body) : NULL;
  if (!j) {
    drop(T("Could not join the watch party"));
    return;
  }
  Pt.seq = (long)jnum(j, "seq", 0);
  members_read(cJSON_GetObjectItemCaseSensitive(j, "members"));
  media_read(cJSON_GetObjectItemCaseSensitive(j, "media"), &Pt.room);
  Pt.roomOk = Pt.room.id[0] != 0;
  Pt.pendSt = jbool(j, "playing") ? 2 : 3;
  Pt.pendPos = jnum(j, "pos", 0);
  Pt.pendAt = app_now();
  net_json_free(j);
  PMedia mine;
  int have = local_media(&mine);
  Pt.seen = mine;
  Pt.seenInit = 1;
  if (Pt.roomOk) {
    Pt.pending = 1;
    Pt.pendUntil = Pt.pendAt + 120;
    if (!have || !same_media(&Pt.room, &mine))
      follow_media(&Pt.room);
  }
  toast(T("You joined the watch party"), 2600, NULL);
}
static void join(const char *code) {
  if (Pt.in)
    party_leave();
  Pt.gen++;
  Pt.in = 1;
  Pt.fails = 0;
  Pt.seq = 0;
  Pt.base = Pt.pending = Pt.announce = 0;
  Pt.nextPoll = app_now() + 1;
  snprintf(Pt.code, sizeof Pt.code, "%s", code);
  char path[64];
  snprintf(path, sizeof path, "party/%s/join", code);
  post(path, cJSON_CreateObject(), on_joined, (void *)(intptr_t)Pt.gen);
}
static void on_created(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len;
  Pt.creating = 0;
  if ((int)(intptr_t)ud != Pt.gen)
    return;
  cJSON *j = status == 200 ? net_json(body) : NULL;
  char code[16] = "";
  if (j)
    jstr(j, "code", code, sizeof code);
  if (!code[0]) {
    if (j)
      net_json_free(j);
    toast(T("Could not start the watch party"), 3000, NULL);
    return;
  }
  Pt.in = 1;
  Pt.fails = 0;
  Pt.seq = (long)jnum(j, "seq", 0);
  snprintf(Pt.code, sizeof Pt.code, "%s", code);
  members_read(cJSON_GetObjectItemCaseSensitive(j, "members"));
  net_json_free(j);
  Pt.nextPoll = app_now() + 1;
  PMedia mine;
  Pt.roomOk = local_media(&mine);
  if (Pt.roomOk) {
    Pt.room = mine;
    Pt.announce = 1; /* the film already playing here is the party's: the others are told where it stands */
  }
  Pt.seen = mine;
  Pt.seenInit = 1;
  Pt.base = 0;
  toast(T("Watch party started"), 2600, NULL);
}
static void start(void) {
  if (Pt.in || Pt.creating)
    return;
  Pt.creating = 1;
  Pt.gen++;
  cJSON *b = cJSON_CreateObject();
  PMedia mine;
  if (local_media(&mine))
    cJSON_AddItemToObject(b, "media", media_json(&mine));
  post("party/create", b, on_created, (void *)(intptr_t)Pt.gen);
}
static void invite(int i) {
  if (!Pt.in || i < 0 || i >= Pt.nonline)
    return;
  cJSON *b = cJSON_CreateObject();
  cJSON_AddStringToObject(b, "code", Pt.code);
  cJSON_AddStringToObject(b, "to", Pt.online[i].dev);
  if (Pt.roomOk)
    cJSON_AddItemToObject(b, "media", media_json(&Pt.room));
  post("inbox/invite", b, ignore_cb, NULL);
  toast(T1("Invited {n}", "n", Pt.online[i].name), 2600, NULL);
}
static void answer(int accept) {
  if (!Pt.inv.show)
    return;
  Pt.inv.show = 0;
  Pt.lastInv = Pt.inv.id;
  cJSON *r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "inv", (double)Pt.inv.id);
  post("inbox/respond", r, ignore_cb, NULL);
  if (accept)
    join(Pt.inv.code);
  else {
    char path[64];
    snprintf(path, sizeof path, "party/%s/decline", Pt.inv.code);
    post(path, cJSON_CreateObject(), ignore_cb, NULL); /* (the host sees "can't join right now") */
  }
}
static void on_inbox(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len;
  (void)ud;
  Pt.busyInbox = 0;
  if (status != 200) {
    Pt.fails++;
    return;
  }
  Pt.fails = 0;
  cJSON *j = net_json(body);
  if (!j)
    return;
  Pt.nonline = 0;
  const cJSON *x;
  cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(j, "devices")) {
    if (Pt.nonline >= NMAX)
      break;
    jstr(x, "dev", Pt.online[Pt.nonline].dev, sizeof Pt.online[0].dev);
    jstr(x, "name", Pt.online[Pt.nonline].name, sizeof Pt.online[0].name);
    Pt.nonline++;
  }
  cJSON_ArrayForEach(x, cJSON_GetObjectItemCaseSensitive(j, "invites")) {
    long id = (long)jnum(x, "id", 0);
    char code[16];
    jstr(x, "code", code, sizeof code);
    if (Pt.inv.show || id <= Pt.lastInv || (Pt.in && !strcmp(code, Pt.code)))
      continue;
    Pt.inv.show = 1;
    Pt.inv.id = id;
    Pt.inv.focus = 0;
    Pt.inv.at = app_now();
    snprintf(Pt.inv.code, sizeof Pt.inv.code, "%s", code);
    jstr(x, "from", Pt.inv.from, sizeof Pt.inv.from);
    media_read(cJSON_GetObjectItemCaseSensitive(x, "media"), &Pt.inv.m);
    plat_haptic();
  }
  net_json_free(j);
}

/* ---- the frame ---- */
void party_tick(void) {
  if (!party_available() || !ident()) {
    if (Pt.in)
      drop(NULL);
    Pt.inv.show = Pt.menu = 0;
    return;
  }
  double now = app_now();
  if (Pt.inv.show && now - Pt.inv.at > 90) { /* nobody answered: it goes away, and is not offered again */
    Pt.inv.show = 0;
    Pt.lastInv = Pt.inv.id;
  }
  /* the inbox: every few seconds (this is also how the other devices see this one online), slower after failures */
  double every = Pt.fails > 3 ? 60 : Pt.in ? 10 : 5;
  if (!Pt.busyInbox && now >= Pt.nextInbox && !app_away()) {
    Pt.nextInbox = now + every;
    Pt.busyInbox = 1;
    post("inbox/poll", cJSON_CreateObject(), on_inbox, NULL);
  }
  if (!Pt.in)
    return;
  if (!Pt.busyPoll && now >= Pt.nextPoll) {
    char path[64];
    cJSON *b = cJSON_CreateObject();
    cJSON_AddNumberToObject(b, "since", (double)Pt.seq);
    snprintf(path, sizeof path, "party/%s/poll", Pt.code);
    Pt.nextPoll = now + 1;
    Pt.busyPoll = 1;
    post(path, b, on_poll, (void *)(intptr_t)Pt.gen);
  }
  if (Pt.pending)
    pending_tick();
  else
    local_watch();
}

/* ---- the cards ---- */
static float U(void) {
  return SB_W >= 1000 ? 1.0f : 0.62f;
}
static void card_box(float *x, float *y, float *w, float h) {
  float u = U();
  *w = 680 * u;
  if (*w > SB_W - 24)
    *w = SB_W - 24;
  *x = SB_W / 2 - *w / 2;
  *y = SB_H / 2 - h / 2;
  gfx_shadow(*x, *y + 16 * u, *w, h, 22 * u, 48 * u, BLACK_A(150));
  gfx_rrect(*x, *y, *w, h, 22 * u, RGB(34, 34, 36));
  gfx_ring(*x, *y, *w, h, 22 * u, 1, WHITE_A(20));
}
static void button(float x, float y, float w, float h, const char *lb, int on) {
  float u = U();
  gfx_rrect(x, y, w, h, h / 2, on ? ACC : WHITE_A(22));
  text_draw(lb, x + w / 2, y + h / 2 - text_line_height((int)(24 * u)) / 2.0f, (int)(24 * u), 700, on ? ACC_FG : WHITE_A(230), 0,
            w - 16, ALIGN_CENTER);
}
static void menu_rows(void) {
  Pt.nrow = 0;
#define ROW(A, ARG, TEXT)                                  \
  do {                                                     \
    if (Pt.nrow < 16) {                                    \
      Pt.row[Pt.nrow].act = A;                             \
      Pt.row[Pt.nrow].arg = ARG;                           \
      snprintf(Pt.row[Pt.nrow].lb, 120, "%s", TEXT);       \
      Pt.nrow++;                                           \
    }                                                      \
  } while (0)
  if (!Pt.in) {
    ROW(1, 0, Pt.creating ? T("Starting\xe2\x80\xa6") : T("Start a watch party"));
  } else {
    for (int i = 0; i < Pt.nonline; i++) {
      int mem = 0;
      for (int k = 0; k < Pt.nmem; k++)
        if (!strcmp(Pt.mem[k].dev, Pt.online[i].dev))
          mem = 1;
      if (!mem)
        ROW(2, i, T1("Invite {n}", "n", Pt.online[i].name));
    }
    ROW(3, 0, T("Leave the watch party"));
  }
  ROW(4, 0, T("Close"));
#undef ROW
  if (Pt.menuIdx >= Pt.nrow)
    Pt.menuIdx = Pt.nrow - 1;
  if (Pt.menuIdx < 0)
    Pt.menuIdx = 0;
}
void party_menu_open(void) {
  if (!party_available()) {
    toast(url_ok() ? T("Sign in to Stremio to use watch parties") : T("The watch party server is not set up"), 3500,
          NULL);
    return;
  }
  Pt.menu = 1;
  Pt.menuIdx = 0;
}
static void menu_act(int i) {
  menu_rows();
  if (i < 0 || i >= Pt.nrow)
    return;
  switch (Pt.row[i].act) {
  case 1:
    start();
    break;
  case 2:
    invite(Pt.row[i].arg);
    break;
  case 3:
    party_leave();
    Pt.menu = 0;
    break;
  case 4:
    Pt.menu = 0;
    break;
  }
}
void party_draw(void) {
  float u = U();
  Pt.cardH = 0;
  if (Pt.inv.show) {
    float h = 300 * u, x, y, w;
    card_box(&x, &y, &w, h);
    Pt.cardX = x;
    Pt.cardY = y;
    Pt.cardW = w;
    Pt.cardH = h;
    float pad = 36 * u;
    text_draw(T1("{n} invites you to a watch party", "n", Pt.inv.from[0] ? Pt.inv.from : "?"), x + pad, y + pad, (int)(30 * u), 700,
              WHITE_A(240), -0.1f, w - 2 * pad, ALIGN_LEFT);
    if (Pt.inv.m.name[0])
      text_draw(Pt.inv.m.name, x + pad, y + pad + 52 * u, (int)(26 * u), 500, WHITE_A(170), 0, w - 2 * pad, ALIGN_LEFT);
    text_draw(T("You can leave at any time. Declining changes nothing here."), x + pad, y + pad + 100 * u, (int)(20 * u), 500,
              WHITE_A(120), 0, w - 2 * pad, ALIGN_LEFT);
    float bw = (w - 2 * pad - 16 * u) / 2, bh = 64 * u, by = y + h - pad - bh;
    Pt.ax = x + pad;
    Pt.ay = by;
    Pt.aw = bw;
    Pt.ah = bh;
    Pt.dx = x + pad + bw + 16 * u;
    Pt.dy = by;
    Pt.dw = bw;
    Pt.dh = bh;
    button(Pt.ax, by, bw, bh, T("Accept"), Pt.inv.focus == 0);
    button(Pt.dx, by, bw, bh, T("Decline"), Pt.inv.focus == 1);
    return;
  }
  if (!Pt.menu)
    return;
  menu_rows();
  float rh = 62 * u, pad = 30 * u, hd = 110 * u, h = hd + Pt.nrow * (rh + 8 * u) + pad, x, y, w;
  card_box(&x, &y, &w, h);
  Pt.cardX = x;
  Pt.cardY = y;
  Pt.cardW = w;
  Pt.cardH = h;
  text_draw(T("Watch party"), x + pad, y + pad, (int)(30 * u), 700, WHITE_A(240), -0.1f, w - 2 * pad, ALIGN_LEFT);
  char n[8];
  snprintf(n, sizeof n, "%d", Pt.nmem);
  text_draw(Pt.in ? T1("{n} watching together", "n", n) : T("Watch the same film in step, from anywhere"), x + pad,
            y + pad + 44 * u, (int)(20 * u), 500, WHITE_A(130), 0, w - 2 * pad, ALIGN_LEFT);
  for (int i = 0; i < Pt.nrow; i++) {
    float ry = y + hd + i * (rh + 8 * u);
    Pt.rx[i] = x + pad;
    Pt.ry[i] = ry;
    Pt.rw[i] = w - 2 * pad;
    Pt.rh[i] = rh;
    int on = i == Pt.menuIdx;
    gfx_rrect(x + pad, ry, w - 2 * pad, rh, 14 * u, on ? ACC : WHITE_A(18));
    text_draw(Pt.row[i].lb, x + pad + 22 * u, ry + rh / 2 - text_line_height((int)(24 * u)) / 2.0f, (int)(24 * u), 600,
              on ? ACC_FG : WHITE_A(225), 0, w - 2 * pad - 44 * u, ALIGN_LEFT);
  }
}
int party_key(int k, int down) {
  if (!Pt.inv.show && !Pt.menu) {
    if (down && k == K_YELLOW && party_available() && ident()) {
      party_menu_open();
      return 1;
    }
    return 0;
  }
  if (!down)
    return 1;
  if (Pt.inv.show) {
    if (k == K_LEFT || k == K_RIGHT || k == K_UP || k == K_DOWN)
      Pt.inv.focus = !Pt.inv.focus;
    else if (k == K_OK)
      answer(Pt.inv.focus == 0);
    else if (k == K_BACK || k == K_EXIT)
      answer(0);
    return 1;
  }
  if (k == K_UP && Pt.menuIdx > 0)
    Pt.menuIdx--;
  else if (k == K_DOWN && Pt.menuIdx + 1 < Pt.nrow)
    Pt.menuIdx++;
  else if (k == K_OK)
    menu_act(Pt.menuIdx);
  else if (k == K_BACK || k == K_EXIT || k == K_YELLOW)
    Pt.menu = 0;
  return 1;
}
static int inside(float x, float y, float bx, float by, float bw, float bh) {
  return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}
int party_click(float x, float y) {
  if (Pt.inv.show) {
    if (inside(x, y, Pt.ax, Pt.ay, Pt.aw, Pt.ah))
      answer(1);
    else if (inside(x, y, Pt.dx, Pt.dy, Pt.dw, Pt.dh))
      answer(0);
    return 1; /* (the card takes every click while it is up) */
  }
  if (Pt.menu) {
    if (!inside(x, y, Pt.cardX, Pt.cardY, Pt.cardW, Pt.cardH)) {
      Pt.menu = 0;
      return 1;
    }
    for (int i = 0; i < Pt.nrow; i++)
      if (inside(x, y, Pt.rx[i], Pt.ry[i], Pt.rw[i], Pt.rh[i])) {
        Pt.menuIdx = i;
        menu_act(i);
        break;
      }
    return 1;
  }
  return 0;
}
