#include "switch/fs.hpp"

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

namespace dusk::sw {

std::filesystem::path data_root() {
  return std::filesystem::path("sdmc:/switch/dusklight");
}

std::filesystem::path romfs_root() {
  return std::filesystem::path("romfs:/res");
}

void ensure_data_root() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/dusklight", 0777);
#else
  std::error_code ec;
  std::filesystem::create_directories(data_root(), ec);
#endif
}

}
