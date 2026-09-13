#pragma once

#include <cstddef>
#include <cstdint>

namespace dusk::sw {

// Graphics backend selector. TODO: add Deko3d fully
enum class GfxBackend : uint8_t {
  DawnVulkanNxvk = 0,
  Deko3D = 1,
};

constexpr uint32_t kPresentWidth = 1280;
constexpr uint32_t kPresentHeight = 720;
constexpr uint32_t kSwapchainImages = 3;

}
