#include "switch/nxvk.hpp"

#ifdef __SWITCH__
#include <stdlib.h>
#endif

namespace dusk::sw {

void nxvk_env_setup() {
#ifdef __SWITCH__
  setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
#endif
}

bool nxvk_has_driver() {
#ifdef __SWITCH__
  return true;
#else
  return false;
#endif
}

const char* const* nxvk_instance_exts(unsigned* count) {
  static const char* exts[] = {
    "VK_KHR_surface",
    "VK_NN_vi_surface",
  };
  if (count)
    *count = 2;
  return exts;
}

}
