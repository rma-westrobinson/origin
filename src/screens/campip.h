/* A security camera floating in a corner (the second player, core/camvid.h), while the page is browsed or a film plays
   on the one player. It is not the film's corner window (player.h): the two never show together. The camera is chosen in
   Settings > Cameras (Play on its row) or is the last one watched or floated; the film's player has a button for it.
   Click or OK-through-the-viewer: the full-screen camera view (cameras.h). Stop closes it, Play/Pause freezes it. */
#ifndef SB_CAMPIP_H
#define SB_CAMPIP_H
#include "core/cameras.h"
int campip_available(void); /* this device can float a camera (a second player: Android, TV and phone) */
int campip_open(const Camera *c); /* 1 when it floats; a film in the corner is kept, hidden (campip_swap brings it back) */
void campip_close(void); /* the camera is let go: its connection and its decoder */
int campip_active(void); /* a camera floats (live, connecting, frozen or failed) */
void campip_toggle(void); /* closes it, or floats the last camera watched (the first when there is none) */
void campip_tick(void); /* once a frame, even while a film hides the page */
void campip_draw(void); /* once a frame after the page (and the film's controls): its window cut out of them */
int campip_key(int k); /* play, pause, stop for the browsing page; 1 when used */
int campip_visible(void); /* a camera floats and its window shows (not hidden behind a film in the corner) */
void campip_float_last(void); /* floats the last camera watched (the first when there is none) */
void campip_step(int dir); /* the next (1) or previous (-1) camera, in the same window */
void campip_hide(int hide); /* a film takes the corner: the camera is kept alive, hidden; 0 brings it back */
void campip_swap(void); /* one corner window shows at a time, the film's or the camera's: the other one comes */
/* the remote's focus on the corner window (app.c): an arrow that moved nothing on the page lands on it, then OK
   expands it, Back/Up gives the focus back (a phone: touch buttons on the window instead) */
int pip_focus_try(int physicalArrow); /* 1 when the arrow put the focus on a window */
int pip_focus_summon(void); /* the Blue key, from any screen: the focus goes to the window in the corner; 1 when used */
int pip_dblok_armed(void); /* a window shows in the corner and has no focus: OK waits for a possible double-click */
int pip_focus_key(int k); /* a key while a window has the focus; 1 when used */
int pip_focused(void);
void campip_focus_draw(int which, float x, float y, float w, float h); /* which: 1 the film's window, 2 the camera's */
#endif
