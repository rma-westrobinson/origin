/* The security cameras: a short list kept in the settings (name, stream address, the camera account's user and
   password), the playable address built from one, and the phone's way of adding one (the page served by httpd.c).
   Wired Tapo cameras give RTSP: rtsp://user:password@<ip>:554/stream1 (full quality) or /stream2 (standard); the
   camera account is made in the Tapo app and is not the Tapo login. */
#ifndef SB_CAMERAS_H
#define SB_CAMERAS_H
#include <stddef.h>
#define CAM_MAX 12
typedef struct {
  char name[48], addr[240], user[48], pass[64];
} Camera;
int cam_count(void);
int cam_get(int i, Camera *c); /* 1 when there is a camera i */
int cam_save(int i, const Camera *c); /* i < 0: added; returns its index, or -1 (the list is full, nothing to open) */
void cam_remove(int i);
unsigned cam_generation(void); /* bumps when the list changes (from the phone too) */
void cam_poll(void); /* takes in a camera the phone saved; from the main thread, a few times a second */
int cam_save_failed(void); /* the last camera taken in from the phone could not be saved (the list is full) */
void cam_save_failed_clear(void);
/* the address to play: "rtsp://" put in front when missing, "/stream1" when the address names no path, and the
   account's user and password put in (percent-encoded) unless the address already has them; 0 when there is no address */
int cam_url(const Camera *c, char *out, size_t cap);
/* the address as shown on the TV and in lists: no user, no password */
void cam_shown(const Camera *c, char *out, size_t cap);
#endif
