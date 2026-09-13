// EGL is unused so stub it

#include <cstdint>

using EGLint = std::int32_t;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;
using EGLDisplay = void*;
using EGLConfig = void*;
using EGLSurface = void*;
using EGLContext = void*;
using EGLNativeDisplayType = void*;
using EGLNativeWindowType = void*;
using EGLProc = void (*)();

namespace {
constexpr EGLBoolean kFalse = 0;
constexpr EGLint kSuccess = 0x3000;
}  // namespace

extern "C" {

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
  (void)display_id;
  return nullptr;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
  (void)dpy;
  (void)major;
  (void)minor;
  return kFalse;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
  (void)dpy;
  return kFalse;
}

EGLBoolean eglChooseConfig(
    EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config) {
  (void)dpy;
  (void)attrib_list;
  (void)configs;
  (void)config_size;
  (void)num_config;
  return kFalse;
}

EGLSurface eglCreateWindowSurface(
    EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint* attrib_list) {
  (void)dpy;
  (void)config;
  (void)win;
  (void)attrib_list;
  return nullptr;
}

EGLContext eglCreateContext(
    EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list) {
  (void)dpy;
  (void)config;
  (void)share_context;
  (void)attrib_list;
  return nullptr;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
  (void)dpy;
  (void)surface;
  return kFalse;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
  (void)dpy;
  (void)ctx;
  return kFalse;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
  (void)dpy;
  (void)draw;
  (void)read;
  (void)ctx;
  return kFalse;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
  (void)dpy;
  (void)interval;
  return kFalse;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  (void)dpy;
  (void)surface;
  return kFalse;
}

EGLBoolean eglBindAPI(EGLenum api) {
  (void)api;
  return kFalse;
}

EGLint eglGetError(void) {
  return kSuccess;
}

EGLProc eglGetProcAddress(const char* procname) {
  (void)procname;
  return nullptr;
}

}  // extern "C"
