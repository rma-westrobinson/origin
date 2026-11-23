/* A security camera floating in a corner (the second player, core/camvid.h), while the page is browsed or a film plays
   on the one player. It is not the film's corner window (player.h): the two never show together. The camera is chosen in
   Settings > Cameras (Play on its row) or is the last one watched or floated; the film's player has a button for it.
   Click or OK-through-the-viewer: the full-screen camera view (cameras.h). Stop closes it, Play/Pause freezes it. */
#ifndef SB_CAMPIP_H
#define SB_CAMPIP_H
#include "core/cameras.h"
int campip_available(void); /* this device can float a camera (a second player; not on a phone) */
int campip_open(const Camera *c); /* 1 when it floats; a film in the corner is closed first */
void campip_close(void); /* the camera is let go: its connection and its decoder */
int campip_active(void); /* a camera floats (live, connecting, frozen or failed) */
void campip_toggle(void); /* closes it, or floats the last camera watched (the first when there is none) */
void campip_tick(void); /* once a frame, even while a film hides the page */
void campip_draw(void); /* once a frame after the page (and the film's controls): its window cut out of them */
int campip_key(int k); /* play, pause, stop for the browsing page; 1 when used */
#endif
