#pragma once

namespace dusk::sw {

// Some nxvk setup
void nxvk_env_setup();
bool nxvk_has_driver();
const char* const* nxvk_instance_exts(unsigned* count);

}
