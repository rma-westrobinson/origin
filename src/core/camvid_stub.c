/* The second player where there is none: nothing is available and nothing happens (see camvid.h). */
#include "core/camvid.h"
#if !SB_CAMVID
int camvid_available(void) {
  return 0;
}
void camvid_load(const char *url) {
  (void)url;
}
void camvid_unload(void) {}
void camvid_set_window(float x, float y, float w, float h) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}
void camvid_set_audio(int pct) {
  (void)pct;
}
void camvid_pump(void) {}
VidState camvid_state(void) {
  return VID_IDLE;
}
int camvid_live(void) {
  return 0;
}
const char *camvid_error(void) {
  return "";
}
#endif
