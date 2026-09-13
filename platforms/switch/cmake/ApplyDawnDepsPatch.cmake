# ApplyDawnPatch

cmake_minimum_required(VERSION 3.25)

set(_APPLY_DAWN_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(dusk_switch_patch_dawn_deps)
  set(_patch "${_APPLY_DAWN_DEPS_DIR}/../patches/dawn-deps-abseil.patch")
  if(NOT EXISTS "${_patch}")
    message(FATAL_ERROR "SwitchDawnDeps: patch not found: ${_patch}")
  endif()

  include(FetchContent)
  FetchContent_GetProperties(dawn)
  if(NOT dawn_POPULATED)
    message(STATUS "SwitchDawnDeps: Dawn not populated yet, skipping abseil patch")
    return()
  endif()
  set(_abseil_dir "${dawn_SOURCE_DIR}/third_party/abseil-cpp")
  if(NOT EXISTS "${_abseil_dir}/absl/base/internal/sysinfo.cc")
    message(STATUS "SwitchDawnDeps: abseil not fetched yet, skipping abseil patch")
    return()
  endif()

  file(SHA256 "${_patch}" _patch_hash)
  set(_stamp_dir "${CMAKE_BINARY_DIR}/_deps/switch-dawn-deps-patch-stamps")
  set(_stamp "${_stamp_dir}/${_patch_hash}.stamp")
  file(READ "${_abseil_dir}/absl/base/internal/sysinfo.cc" _sysinfo)
  if(_sysinfo MATCHES "reinterpret_cast<uintptr_t>")
    if(NOT EXISTS "${_stamp}")
      file(MAKE_DIRECTORY "${_stamp_dir}")
      file(WRITE "${_stamp}" "${_patch_hash}\n")
    endif()
    message(STATUS "SwitchDawnDeps: already applied (${_patch_hash})")
    return()
  endif()

  find_program(_patch_exe NAMES patch
    HINTS
      "$ENV{DEVKITPRO}/msys2/usr/bin"
      "C:/devkitPro/msys2/usr/bin"
      "C:/Program Files/Git/usr/bin"
  )
  if(NOT _patch_exe)
    message(FATAL_ERROR "SwitchDawnDeps: GNU patch not found. Install patch "
      "(msys2: pacman -S patch, or Git for Windows which ships usr/bin/patch.exe).")
  endif()
  message(STATUS "SwitchDawnDeps: applying dawn-deps-abseil.patch with ${_patch_exe}")
  execute_process(
    COMMAND "${_patch_exe}" -p1 --forward --batch --no-backup-if-mismatch -i "${_patch}"
    WORKING_DIRECTORY "${_abseil_dir}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_out
    ERROR_VARIABLE _apply_err
  )
  if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "SwitchDawnDeps: patch failed:\n${_apply_out}\n${_apply_err}\n"
      "The abseil revision may have drifted; inspect and adapt platforms/switch/patches/dawn-deps-abseil.patch.")
  endif()
  file(READ "${_abseil_dir}/absl/base/internal/sysinfo.cc" _sysinfo_verify)
  if(NOT _sysinfo_verify MATCHES "reinterpret_cast<uintptr_t>")
    message(FATAL_ERROR "SwitchDawnDeps: patch ran but GetTID marker missing in sysinfo.cc")
  endif()
  file(MAKE_DIRECTORY "${_stamp_dir}")
  file(GLOB _old_stamps "${_stamp_dir}/*.stamp")
  foreach(_old IN LISTS _old_stamps)
    if(NOT _old STREQUAL _stamp)
      file(REMOVE "${_old}")
    endif()
  endforeach()
  file(WRITE "${_stamp}" "${_patch_hash}\n")
  message(STATUS "SwitchDawnDeps: applied (${_patch_hash})")
endfunction()
