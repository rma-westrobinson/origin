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

/* the camera grid's players (core/camgrid.h): the same, where there are none */
#include "core/camgrid.h"
#if !SB_CAMVID
int camgrid_available(void) {
  return 0;
}
void camgrid_load(int cell, const char *url) {
  (void)cell;
  (void)url;
}
void camgrid_unload_all(void) {}
void camgrid_set_cell(int cell, float x, float y, float w, float h) {
  (void)cell;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
}
void camgrid_pump(void) {}
VidState camgrid_state(int cell) {
  (void)cell;
  return VID_IDLE;
}
int camgrid_live(int cell) {
  (void)cell;
  return 0;
}
#endif
