#include "switch/runtime.hpp"
#include "switch/nxvk.hpp"
#include "switch/fs.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#include <cstring>
#endif

namespace dusk::sw {

#ifdef __SWITCH__

void log_line(const char* msg) {
  svcOutputDebugString(msg, strlen(msg));
}

void runtime_init() {
  ensure_data_root();
}

void runtime_exit() {}

bool require_full_takeover() {
  if (appletGetAppletType() == AppletType_Application) {
    return true;
  }
  consoleInit(NULL);
  printf("Dusklight needs Title Takeover to run properly.\n\n");
  printf("Hold R on a title to enter title override and run Dusklight or launch a forwarder if you have one,\n");
  printf("Press PLUS to exit.\n");
  consoleUpdate(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
      break;
    }
    consoleUpdate(NULL);
  }
  consoleExit(NULL);
  return false;
}

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;

void userAppInit(void) {
  romfsInit();
  nxvk_env_setup();
  ensure_data_root();
  runtime_init();
  nvInitialize();
}

void userAppExit(void) {
  nvExit();
  runtime_exit();
  romfsExit();
}
}

#else
void runtime_init() {}
void runtime_exit() {}
void log_line(const char*) {}
bool require_full_takeover() { return true; }
void heartbeat_start() {}
#endif

}
