# ApplyTracyPatch
cmake_minimum_required(VERSION 3.25)

# Captured at include time
set(_APPLY_TRACY_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(dusk_switch_patch_tracy)
  set(_patch "${_APPLY_TRACY_DIR}/../patches/tracy-switch.patch")
  if(NOT EXISTS "${_patch}")
    message(FATAL_ERROR "SwitchTracy: patch not found: ${_patch}")
  endif()

  include(FetchContent)
  FetchContent_GetProperties(tracy)
  if(NOT tracy_POPULATED)
    message(STATUS "SwitchTracy: Tracy not populated yet, skipping tracy patch")
    return()
  endif()
  set(_tracy_system "${tracy_SOURCE_DIR}/public/common/TracySystem.cpp")
  if(NOT EXISTS "${_tracy_system}")
    message(STATUS "SwitchTracy: TracySystem.cpp missing, skipping tracy patch")
    return()
  endif()

  file(SHA256 "${_patch}" _patch_hash)
  set(_stamp_dir "${CMAKE_BINARY_DIR}/_deps/switch-tracy-patch-stamps")
  set(_stamp "${_stamp_dir}/${_patch_hash}.stamp")
  file(READ "${_tracy_system}" _text)
  if(_text MATCHES "defined __SWITCH__")
    if(NOT EXISTS "${_stamp}")
      file(MAKE_DIRECTORY "${_stamp_dir}")
      file(WRITE "${_stamp}" "${_patch_hash}\n")
    endif()
    message(STATUS "SwitchTracy: already applied (${_patch_hash})")
    return()
  endif()

  find_program(_patch_exe NAMES patch
    HINTS
      "$ENV{DEVKITPRO}/msys2/usr/bin"
      "C:/devkitPro/msys2/usr/bin"
      "C:/Program Files/Git/usr/bin"
  )
  if(NOT _patch_exe)
    message(FATAL_ERROR "SwitchTracy: GNU patch not found. Install patch "
      "(msys2: pacman -S patch, or Git for Windows which ships usr/bin/patch.exe).")
  endif()
  message(STATUS "SwitchTracy: applying tracy-switch.patch with ${_patch_exe}")
  execute_process(
    COMMAND "${_patch_exe}" -p1 --forward --batch --no-backup-if-mismatch -i "${_patch}"
    WORKING_DIRECTORY "${tracy_SOURCE_DIR}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_out
    ERROR_VARIABLE _apply_err
  )
  if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "SwitchTracy: patch failed:\n${_apply_out}\n${_apply_err}\n"
      "The Tracy revision may have drifted; inspect and adapt platforms/switch/patches/tracy-switch.patch.")
  endif()
  file(READ "${_tracy_system}" _verify)
  if(NOT _verify MATCHES "defined __SWITCH__")
    message(FATAL_ERROR "SwitchTracy: patch ran but marker missing in TracySystem.cpp")
  endif()
  file(MAKE_DIRECTORY "${_stamp_dir}")
  file(GLOB _old_stamps "${_stamp_dir}/*.stamp")
  foreach(_old IN LISTS _old_stamps)
    if(NOT _old STREQUAL _stamp)
      file(REMOVE "${_old}")
    endif()
  endforeach()
  file(WRITE "${_stamp}" "${_patch_hash}\n")
  message(STATUS "SwitchTracy: applied (${_patch_hash})")
endfunction()
