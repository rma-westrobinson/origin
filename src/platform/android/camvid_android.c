/* Android: the second player (core/camvid.h) is tv.stremioorigin.app.SbCam, an ExoPlayer of its own in its own
   SurfaceView, over the film's and under SDL's translucent surface. Same bridge as video_android.c: commands are
   posted to the main thread, and one snapshot is read once a frame (camvid_pump). */
#include "core/camvid.h"
#include "ui/gfx.h"
#include <SDL2/SDL.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

static jclass cls;
static jmethodID mLoad, mUnload, mWin, mPoll, mErr;
static struct {
  double st, live;
} S;
static char err[256];

static JNIEnv *env(void) {
  return (JNIEnv *)SDL_AndroidGetJNIEnv();
}
static void clear_exc(JNIEnv *e, const char *where) {
  if ((*e)->ExceptionCheck(e)) {
    (*e)->ExceptionDescribe(e);
    (*e)->ExceptionClear(e);
    fprintf(stderr, "[camvid] java exception in %s\n", where);
  }
}
/* found on first use (the film's bridge does it in video_init; this one has no init call) */
static int bind(void) {
  if (cls)
    return 1;
  JNIEnv *e = env();
  if (!e)
    return 0;
  jclass c = (*e)->FindClass(e, "tv/stremioorigin/app/SbCam");
  if (!c) {
    clear_exc(e, "find");
    return 0;
  }
  jclass g = (*e)->NewGlobalRef(e, c);
  (*e)->DeleteLocalRef(e, c);
#define M(v, n, s)                           \
  v = (*e)->GetStaticMethodID(e, g, n, s);   \
  if (!v) {                                  \
    clear_exc(e, n);                         \
    (*e)->DeleteGlobalRef(e, g);             \
    return 0;                                \
  }
  M(mLoad, "load", "(Ljava/lang/String;)V");
  M(mUnload, "unload", "()V");
  M(mWin, "setWindow", "(FFFF)V");
  M(mPoll, "poll", "([D)I");
  M(mErr, "error", "()Ljava/lang/String;");
#undef M
  cls = g;
  fprintf(stderr, "[camvid] second player bridge ready\n");
  return 1;
}
int camvid_available(void) {
  return bind();
}
void camvid_load(const char *url) {
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  jstring s = (*e)->NewStringUTF(e, url);
  (*e)->CallStaticVoidMethod(e, cls, mLoad, s);
  (*e)->DeleteLocalRef(e, s);
  clear_exc(e, "load");
  memset(&S, 0, sizeof S);
  S.st = VID_LOADING;
  err[0] = 0;
  fprintf(stderr, "[camvid] load rtsp://(camera)\n"); /* the address carries the camera's account: never logged */
}
void camvid_unload(void) {
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  (*e)->CallStaticVoidMethod(e, cls, mUnload);
  clear_exc(e, "unload");
  memset(&S, 0, sizeof S);
  err[0] = 0;
}
static float lw[4] = { -1, -1, -1, -1 };
void camvid_set_window(float x, float y, float w, float h) {
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > SB_W)
    w = SB_W - x;
  if (y + h > SB_H)
    h = SB_H - y;
  if (lw[0] == x && lw[1] == y && lw[2] == w && lw[3] == h)
    return;
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  lw[0] = x;
  lw[1] = y;
  lw[2] = w;
  lw[3] = h;
  (*e)->CallStaticVoidMethod(e, cls, mWin, x / SB_W, y / SB_H, w / SB_W, h / SB_H);
  clear_exc(e, "window");
}
void camvid_pump(void) {
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  static jdoubleArray arr;
  if (!arr) {
    jdoubleArray a = (*e)->NewDoubleArray(e, 2);
    arr = (*e)->NewGlobalRef(e, a);
    (*e)->DeleteLocalRef(e, a);
  }
  jint flags = (*e)->CallStaticIntMethod(e, cls, mPoll, arr);
  clear_exc(e, "poll");
  (*e)->GetDoubleArrayRegion(e, arr, 0, 2, (jdouble *)&S);
  if (flags & 2) {
    jstring js = (jstring)(*e)->CallStaticObjectMethod(e, cls, mErr);
    clear_exc(e, "error");
    const char *s = js ? (*e)->GetStringUTFChars(e, js, NULL) : NULL;
    snprintf(err, sizeof err, "%s", s ? s : "");
    if (s)
      (*e)->ReleaseStringUTFChars(e, js, s);
    if (js)
      (*e)->DeleteLocalRef(e, js);
  }
}
VidState camvid_state(void) {
  return (VidState)(int)S.st;
}
int camvid_live(void) {
  return S.live != 0;
}
const char *camvid_error(void) {
  return err; /* (the player's own words may hold the address: the caller never logs them) */
}
