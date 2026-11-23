/* The security cameras in the app: the camera editor (name, stream address, camera account; typed on the TV with the
   keyboard, or sent from the phone), the code that opens the phone's page, and the live view, full screen, on the
   one player (core/video.h) while no film is open. Settings > Cameras lists them. */
#ifndef SB_CAMERAS_SCREEN_H
#define SB_CAMERAS_SCREEN_H
#include "core/cameras.h"
void cameras_edit(int idx); /* the editor; idx -1 a new camera */
void cameras_add_phone(void); /* the code to scan: the phone's page saves the camera to the list */
void cameras_view(const Camera *c); /* the live view, full screen; Back closes it */
int camview_covers(void); /* the live view is up: the page under it is not drawn */
void camview_close(void); /* the app goes away (Home pressed): the camera is let go */
int camview_live(void); /* its picture is running: the window must be transparent */
#endif
