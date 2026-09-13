#include "switch/runtime.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#include <cstring>
#endif

namespace dusk::sw {

#ifdef __SWITCH__
extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;

void userAppInit(void) { romfsInit(); }
void userAppExit(void) { romfsExit(); }
}

static FILE* g_log = nullptr;

void runtime_init() {
  g_log = fopen("sdmc:/switch/dusklight/dusklight.log", "w");
}

void runtime_exit() {
  if (g_log) {
    fclose(g_log);
    g_log = nullptr;
  }
  romfsExit();
}

void log_line(const char* msg) {
  svcOutputDebugString(msg, strlen(msg));
  if (g_log) {
    fputs(msg, g_log);
    fputs("\n", g_log);
    fflush(g_log);
  }
}
#else
void runtime_init() {}
void runtime_exit() {}
void log_line(const char*) {}
#endif

}
