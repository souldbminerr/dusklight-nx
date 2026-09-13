# SwitchAuroraPatch
# EOL-agnostic application of platforms/switch/patches/aurora-switch.patch.
# (extern/aurora may be CRLF on Windows; the patch is LF.)

function(dusk_switch_apply_aurora_patch)
  set(_file "${CMAKE_SOURCE_DIR}/extern/aurora/lib/dawn/BackendBinding.cpp")
  if(NOT EXISTS "${_file}")
    message(WARNING "SwitchAuroraPatch: ${_file} not found, skipping")
    return()
  endif()
  file(READ "${_file}" _content)
  if(_content MATCHES "SurfaceSourceSwitchNWindow")
    return()
  endif()

  string(REGEX REPLACE "(#include <SDL3/SDL_video\\.h>[ \t]*\r?\n#endif)"
    "\\1\n\n#if defined(SDL_PLATFORM_SWITCH)\n#include <switch.h>\n#endif"
    _content "${_content}")
  string(REGEX REPLACE "(return std::move\\(desc\\);[ \t]*\r?\n#elif defined\\(SDL_PLATFORM_LINUX\\))"
    "#elif defined(SDL_PLATFORM_SWITCH)\n  // Dawn/Vulkan path never creates an SDL GL context, so libnx's default\n  // NWindow is free for Dawn's VK_NN_vi_surface swapchain.\n  (void)props;\n  auto desc = std::make_shared<wgpu::SurfaceSourceSwitchNWindow>();\n  desc->window = nwindowGetDefault();\n  return desc;\n#elif defined(SDL_PLATFORM_LINUX)"
    _content "${_content}")

  if(NOT _content MATCHES "SurfaceSourceSwitchNWindow")
    message(FATAL_ERROR "SwitchAuroraPatch: insertion failed, patch manually: "
      "platforms/switch/patches/aurora-switch.patch")
  endif()
  file(WRITE "${_file}" "${_content}")
  message(STATUS "SwitchAuroraPatch: applied Switch surface support")

  # Dawn API drift: upstream removed wgpu::ConvertibleStatus (encounter fork
  # still has it). Normalize aurora.cpp to plain wgpu::Status + explicit
  # Success comparison; compiles against both old and new Dawn.
  set(_aurora_present "${CMAKE_SOURCE_DIR}/extern/aurora/lib/aurora.cpp")
  if(EXISTS "${_aurora_present}")
    file(READ "${_aurora_present}" _aurora_content)
    if(_aurora_content MATCHES "ConvertibleStatus")
      string(REPLACE "wgpu::ConvertibleStatus status = wgpu::Status::Error;"
        "wgpu::Status status = wgpu::Status::Error;"
        _aurora_content "${_aurora_content}")
      string(REPLACE "status = g_surface.Present();"
        "status = static_cast<wgpu::Status>(g_surface.Present());"
        _aurora_content "${_aurora_content}")
      string(REPLACE "if (status) {"
        "if (status == wgpu::Status::Success) {"
        _aurora_content "${_aurora_content}")
      if(_aurora_content MATCHES "ConvertibleStatus")
        message(FATAL_ERROR "SwitchAuroraPatch: ConvertibleStatus fix failed")
      endif()
      file(WRITE "${_aurora_present}" "${_aurora_content}")
      message(STATUS "SwitchAuroraPatch: normalized Surface::Present status check")
    endif()
  endif()
endfunction()
