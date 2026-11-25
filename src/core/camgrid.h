/* Several cameras playing at once, one decoder and one video plane per cell, for the full-screen camera grid
   (screens/camgrid.h). Android only (SbCamGrid.java, the same ExoPlayer RTSP path as core/camvid.h); camgrid_available()
   is 0 elsewhere and the rest is a no-op. Always silent. */
#ifndef SB_CAMGRID_H
#define SB_CAMGRID_H
#include "core/video.h"
int camgrid_available(void);
void camgrid_load(int cell, const char *url); /* an rtsp:// address, account included: never logged */
void camgrid_unload_all(void);
void camgrid_set_cell(int cell, float x, float y, float w, float h); /* layout units, kept inside the screen */
void camgrid_pump(void); /* once a frame: takes in every cell's state */
VidState camgrid_state(int cell);
int camgrid_live(int cell); /* a first frame has been shown */
#endif
