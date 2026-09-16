#pragma once
#include <vulkan/vulkan.h>

namespace dusk::sw {

// Some nxvk setup
void nxvk_env_setup();
bool nxvk_has_driver();
const char* const* nxvk_instance_exts(unsigned* count);

void nvk_dispatch_fixup();

#ifdef __SWITCH__

PFN_vkVoidFunction nvk_lookup_instance_proc(const char* name);
PFN_vkVoidFunction nvk_lookup_device_proc(const char* name);
#endif

}
