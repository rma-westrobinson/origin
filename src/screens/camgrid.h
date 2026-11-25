/* The full-screen camera grid: every configured camera playing at once, one cell each (core/camgrid.h), laid out to the
   count. OK or a tap on a cell opens that camera in the full-screen viewer (cameras.h); Back there returns to the grid. */
#ifndef SB_CAMGRID_SCREEN_H
#define SB_CAMGRID_SCREEN_H
int camgrid_supported(void); /* this device can play several cameras at once (Android) */
void camgrid_open(void); /* from Settings > Cameras; with no camera it says so and points at the add flow */
int camgrid_covers(void); /* the grid is the page: the page under it is not drawn */
void camgrid_close(void); /* closed, or the app goes away (Home pressed): every camera is let go */
#endif
