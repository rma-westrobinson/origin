#include "core/cameras.h"
#include "core/httpd.h"
#include "core/store.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define KEY "cameras"
static unsigned gen;

static void put(const cJSON *o, const char *k, char *out, size_t cap) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, k);
  snprintf(out, cap, "%s", cJSON_IsString(v) && v->valuestring ? v->valuestring : "");
}
static int load(Camera *list) {
  const cJSON *a = store_node(KEY), *o;
  int n = 0;
  if (!cJSON_IsArray(a))
    return 0;
  cJSON_ArrayForEach(o, a) {
    if (n >= CAM_MAX)
      break;
    Camera *c = &list[n++];
    put(o, "name", c->name, sizeof c->name);
    put(o, "addr", c->addr, sizeof c->addr);
    put(o, "user", c->user, sizeof c->user);
    put(o, "pass", c->pass, sizeof c->pass);
  }
  return n;
}
static void write_all(const Camera *list, int n) {
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < n; i++) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", list[i].name);
    cJSON_AddStringToObject(o, "addr", list[i].addr);
    cJSON_AddStringToObject(o, "user", list[i].user);
    cJSON_AddStringToObject(o, "pass", list[i].pass);
    cJSON_AddItemToArray(a, o);
  }
  store_set_node(KEY, a);
  gen++;
}
int cam_count(void) {
  Camera l[CAM_MAX];
  return load(l);
}
int cam_get(int i, Camera *c) {
  Camera l[CAM_MAX];
  int n = load(l);
  if (i < 0 || i >= n)
    return 0;
  *c = l[i];
  return 1;
}
int cam_save(int i, const Camera *c) {
  Camera l[CAM_MAX];
  int n = load(l);
  if (i < 0 || i >= n) {
    if (n >= CAM_MAX)
      return -1;
    i = n++;
  }
  l[i] = *c;
  if (!l[i].name[0])
    snprintf(l[i].name, sizeof l[i].name, "Camera %d", i + 1);
  write_all(l, n);
  return i;
}
void cam_remove(int i) {
  Camera l[CAM_MAX];
  int n = load(l);
  if (i < 0 || i >= n)
    return;
  memmove(&l[i], &l[i + 1], (size_t)(n - i - 1) * sizeof l[0]);
  write_all(l, n - 1);
}
unsigned cam_generation(void) {
  return gen;
}
void cam_poll(void) {
  HttpdCamera h;
  if (!httpd_take_camera(&h))
    return;
  Camera c;
  memset(&c, 0, sizeof c);
  snprintf(c.name, sizeof c.name, "%s", h.name);
  snprintf(c.addr, sizeof c.addr, "%s", h.addr);
  snprintf(c.user, sizeof c.user, "%s", h.user);
  snprintf(c.pass, sizeof c.pass, "%s", h.pass);
  cam_save(-1, &c);
}

/* user and password go into the address: anything but unreserved characters is percent-encoded */
static size_t enc(const char *s, char *out, size_t cap) {
  size_t o = 0;
  for (; *s && o + 4 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '.' ||
        c == '_' || c == '~')
      out[o++] = (char)c;
    else
      o += (size_t)snprintf(out + o, cap - o, "%%%02X", c);
  }
  out[o] = 0;
  return o;
}
int cam_url(const Camera *c, char *out, size_t cap) {
  const char *a = c->addr;
  while (*a == ' ')
    a++;
  if (!*a)
    return 0;
  const char *rest = !strncasecmp(a, "rtsp://", 7) ? a + 7 : a;
  const char *slash = strchr(rest, '/');
  size_t alen = slash ? (size_t)(slash - rest) : strlen(rest);
  int hasAuth = memchr(rest, '@', alen) != NULL;
  char cred[256] = "";
  if (!hasAuth && c->user[0]) {
    size_t n = enc(c->user, cred, sizeof cred);
    if (c->pass[0]) {
      cred[n++] = ':';
      n += enc(c->pass, cred + n, sizeof cred - n);
    }
    if (n + 2 < sizeof cred) {
      cred[n++] = '@';
      cred[n] = 0;
    }
  }
  snprintf(out, cap, "rtsp://%s%.*s%s", cred, (int)alen, rest, slash && slash[1] ? slash : "/stream1");
  return 1;
}
void cam_shown(const Camera *c, char *out, size_t cap) {
  const char *a = c->addr;
  while (*a == ' ')
    a++;
  if (!strncasecmp(a, "rtsp://", 7))
    a += 7;
  const char *at = strchr(a, '@'), *slash = strchr(a, '/');
  if (at && (!slash || at < slash))
    a = at + 1;
  snprintf(out, cap, "%s", a);
}
