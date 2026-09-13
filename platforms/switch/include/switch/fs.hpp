#pragma once

#include <filesystem>

namespace dusk::sw {

// sdmc:/switch/dusklight, romfs:/res
std::filesystem::path data_root();
std::filesystem::path romfs_root();
void ensure_data_root();

}
