# SwitchDeps
# Runs before extern/aurora: patched SDL3-switch, Dawn Vulkan-only flags,
# aurora surface patch. Included from SwitchOptions.cmake.

include(${CMAKE_CURRENT_LIST_DIR}/SwitchAuroraPatch.cmake)

# Dawn: Vulkan-only. option() honors existing cache entries.
set(DAWN_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
set(DAWN_ENABLE_WEBGPU_ON_WEBGPU OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DAWN_BUILD_PROTOBUF OFF CACHE BOOL "No protobuf on Switch (kills host-protoc requirement)" FORCE)
set(TINT_BUILD_IR_BINARY OFF CACHE BOOL "No Tint IR-binary tools on Switch" FORCE)
set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINT_BUILD_CMD_TOOLS OFF CACHE BOOL "" FORCE)
set(DAWN_PLATFORM_SWITCH ON CACHE BOOL "Dawn Switch backend (enables encounter Switch guards)" FORCE)

# Dawn-switch: a local upstream clone + our delta as patches (clone stays pristine).
# Pass -DFETCHCONTENT_SOURCE_DIR_DAWN=<upstream-clone> to use it; otherwise
# aurora downloads its pinned encounter tarball.
if(DEFINED CACHE{FETCHCONTENT_SOURCE_DIR_DAWN})
  include(${CMAKE_CURRENT_LIST_DIR}/ApplyDawnSwitchPatch.cmake)
  dusk_switch_apply_dawn_patch("${FETCHCONTENT_SOURCE_DIR_DAWN}")
endif()

# SDL3-switch: pre-provide so aurora reuses it (same ref it would fetch).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
include(FetchContent)
# Hash embedded in PATCH_COMMAND so patch edits re-trigger the populate patch step.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../platforms/switch/patches/sdl3-switch.patch" _sdl_switch_patch_hash)
FetchContent_Declare(SDL
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.10.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  PATCH_COMMAND ${CMAKE_COMMAND}
    "-DSDL_SOURCE_DIR=<SOURCE_DIR>"
    "-DSWITCH_PATCH=${CMAKE_CURRENT_LIST_DIR}/../platforms/switch/patches/sdl3-switch.patch"
    "-DSWITCH_PATCH_HASH=${_sdl_switch_patch_hash}"
    "-DSWITCH_STAMP_DIR=${CMAKE_BINARY_DIR}/_deps/switch-sdl-patch-stamps"
    -P "${CMAKE_CURRENT_LIST_DIR}/ApplySDLSwitchPatch.cmake"
  EXCLUDE_FROM_ALL
)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)
FetchContent_MakeAvailable(SDL)
set(CMAKE_DISABLE_PRECOMPILE_HEADERS OFF)

# Aurora Switch surface support (record: platforms/switch/patches).
dusk_switch_apply_aurora_patch()
