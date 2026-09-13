# ApplySDLSwitchPatch
# Runs as SDL3 FetchContent PATCH_COMMAND (cmake -P script). Applies
# platforms/switch/patches/sdl3-switch.patch with GNU patch (NOT git apply:
# git silently SKIPS every file with "Skipped patch ..." when the target tree
# sits inside another git repo, which _deps/ always does -- and exits 0).
# Idempotent via content marker plus hash stamp. The patch HASH is embedded in
# our PATCH_COMMAND line in SwitchDeps.cmake, so editing the patch file
# changes the recorded populate command and FetchContent re-runs this step.
# Required -D args: SDL_SOURCE_DIR, SWITCH_PATCH, SWITCH_PATCH_HASH,
# SWITCH_STAMP_DIR.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SDL_SOURCE_DIR OR NOT EXISTS "${SDL_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "ApplySDLSwitchPatch: bad SDL_SOURCE_DIR: '${SDL_SOURCE_DIR}'")
endif()
if(NOT EXISTS "${SWITCH_PATCH}")
  message(FATAL_ERROR "ApplySDLSwitchPatch: patch not found: '${SWITCH_PATCH}'")
endif()

file(READ "${SDL_SOURCE_DIR}/CMakeLists.txt" _sdl_cmake)
set(_stamp "${SWITCH_STAMP_DIR}/${SWITCH_PATCH_HASH}.stamp")
if(EXISTS "${_stamp}" AND _sdl_cmake MATCHES "Private fork backend")
  message(STATUS "ApplySDLSwitchPatch: already applied (${SWITCH_PATCH_HASH})")
else()
  find_program(_patch_exe NAMES patch
    HINTS
      "$ENV{DEVKITPRO}/msys2/usr/bin"
      "C:/devkitPro/msys2/usr/bin"
      "C:/Program Files/Git/usr/bin"
  )
  if(NOT _patch_exe)
    message(FATAL_ERROR "ApplySDLSwitchPatch: GNU patch not found. Install patch "
      "(msys2: pacman -S patch, or Git for Windows which ships usr/bin/patch.exe).")
  endif()
  message(STATUS "ApplySDLSwitchPatch: applying sdl3-switch.patch with ${_patch_exe}")
  execute_process(
    COMMAND "${_patch_exe}" -p1 --forward --batch --no-backup-if-mismatch -i "${SWITCH_PATCH}"
    WORKING_DIRECTORY "${SDL_SOURCE_DIR}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_out
    ERROR_VARIABLE _apply_err
  )
  if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "ApplySDLSwitchPatch: patch failed:\n${_apply_out}\n${_apply_err}\nDelete <build>/_deps/sdl-src and <build>/_deps/sdl-subbuild, then reconfigure.")
  endif()
  file(READ "${SDL_SOURCE_DIR}/CMakeLists.txt" _sdl_cmake_verify)
  if(NOT _sdl_cmake_verify MATCHES "Private fork backend")
    message(FATAL_ERROR "ApplySDLSwitchPatch: patch ran but Switch backend marker missing in ${SDL_SOURCE_DIR}/CMakeLists.txt")
  endif()
  file(MAKE_DIRECTORY "${SWITCH_STAMP_DIR}")
  file(GLOB _old_stamps "${SWITCH_STAMP_DIR}/*.stamp")
  foreach(_old IN LISTS _old_stamps)
    if(NOT _old STREQUAL _stamp)
      file(REMOVE "${_old}")
    endif()
  endforeach()
  file(WRITE "${_stamp}" "${SWITCH_PATCH_HASH}\n")
  message(STATUS "ApplySDLSwitchPatch: applied (${SWITCH_PATCH_HASH})")
endif()