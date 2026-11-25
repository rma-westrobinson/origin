/* Android: the camera grid's players (core/camgrid.h) are tv.stremioorigin.app.SbCamGrid: an ExoPlayer and a
   SurfaceView per cell. Same bridge as camvid_android.c: commands are posted to the main thread, one snapshot per frame. */
#include "core/camgrid.h"
#include "ui/gfx.h"
#include <SDL2/SDL.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define MAXC 12
static jclass cls;
static jmethodID mLoad, mUnload, mCell, mPoll;
static double S[MAXC * 2];
static float lw[MAXC][4];

static JNIEnv *env(void) {
  return (JNIEnv *)SDL_AndroidGetJNIEnv();
}
static void clear_exc(JNIEnv *e, const char *where) {
  if ((*e)->ExceptionCheck(e)) {
    (*e)->ExceptionDescribe(e);
    (*e)->ExceptionClear(e);
    fprintf(stderr, "[camgrid] java exception in %s\n", where);
  }
}
static int bind(void) {
  if (cls)
    return 1;
  JNIEnv *e = env();
  if (!e)
    return 0;
  jclass c = (*e)->FindClass(e, "tv/stremioorigin/app/SbCamGrid");
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
  M(mLoad, "load", "(ILjava/lang/String;)V");
  M(mUnload, "unloadAll", "()V");
  M(mCell, "setCell", "(IFFFF)V");
  M(mPoll, "poll", "([D)V");
#undef M
  cls = g;
  fprintf(stderr, "[camgrid] grid bridge ready\n");
  return 1;
}
int camgrid_available(void) {
  return bind();
}
void camgrid_load(int cell, const char *url) {
  JNIEnv *e = env();
  if (cell < 0 || cell >= MAXC || !bind() || !e)
    return;
  jstring s = (*e)->NewStringUTF(e, url);
  (*e)->CallStaticVoidMethod(e, cls, mLoad, (jint)cell, s);
  (*e)->DeleteLocalRef(e, s);
  clear_exc(e, "load");
  S[cell * 2] = VID_LOADING;
  S[cell * 2 + 1] = 0;
  fprintf(stderr, "[camgrid] load cell %d rtsp://(camera)\n", cell); /* the address carries the account: never logged */
}
void camgrid_unload_all(void) {
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  (*e)->CallStaticVoidMethod(e, cls, mUnload);
  clear_exc(e, "unload");
  memset(S, 0, sizeof S);
  memset(lw, 0, sizeof lw);
}
void camgrid_set_cell(int cell, float x, float y, float w, float h) {
  if (cell < 0 || cell >= MAXC)
    return;
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
  if (lw[cell][0] == x && lw[cell][1] == y && lw[cell][2] == w && lw[cell][3] == h)
    return;
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  lw[cell][0] = x;
  lw[cell][1] = y;
  lw[cell][2] = w;
  lw[cell][3] = h;
  (*e)->CallStaticVoidMethod(e, cls, mCell, (jint)cell, x / SB_W, y / SB_H, w / SB_W, h / SB_H);
  clear_exc(e, "cell");
}
void camgrid_pump(void) {
  JNIEnv *e = env();
  if (!bind() || !e)
    return;
  static jdoubleArray arr;
  if (!arr) {
    jdoubleArray a = (*e)->NewDoubleArray(e, MAXC * 2);
    arr = (*e)->NewGlobalRef(e, a);
    (*e)->DeleteLocalRef(e, a);
  }
  (*e)->CallStaticVoidMethod(e, cls, mPoll, arr);
  clear_exc(e, "poll");
  (*e)->GetDoubleArrayRegion(e, arr, 0, MAXC * 2, (jdouble *)S);
}
VidState camgrid_state(int cell) {
  return cell < 0 || cell >= MAXC ? VID_IDLE : (VidState)(int)S[cell * 2];
}
int camgrid_live(int cell) {
  return cell >= 0 && cell < MAXC && S[cell * 2 + 1] != 0;
}
