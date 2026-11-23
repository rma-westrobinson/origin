/* The second player: one security camera in a small window, beside whatever the one player (core/video.h) has.
   Only where the platform can run two decoders and two video planes in one app (Android: a second ExoPlayer and its own
   SurfaceView, tv.stremioorigin.app.SbCam); camvid_available() is 0 everywhere else and the rest is a no-op. The camera
   has sound when asked (camvid_set_audio, 0 is silent): it takes no audio focus, so it mixes with a film's sound. */
#ifndef SB_CAMVID_H
#define SB_CAMVID_H
#include "core/video.h"
int camvid_available(void);
void camvid_load(const char *url); /* an rtsp:// address, account included: never logged */
void camvid_unload(void); /* the decoder and the connection are let go */
void camvid_set_window(float x, float y, float w, float h); /* layout units, kept inside the screen */
void camvid_set_audio(int pct); /* the camera's sound, 0-100; 0 switches the audio track off (the default until set) */
void camvid_pump(void); /* once a frame: takes in the player's state */
VidState camvid_state(void);
int camvid_live(void); /* a first frame has been shown */
const char *camvid_error(void);
#endif
