#include "switch/input.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace dusk::sw {

#ifdef __SWITCH__
static PadState g_pad;
#endif

void input_init() {
#ifdef __SWITCH__
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_pad);
#endif
}

void input_exit() {
}

}
