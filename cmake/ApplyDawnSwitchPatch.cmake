# ApplyDawnSwitchPatch
# Applies platforms/switch/patches/dawn-switch.patch to a local upstream Dawn
# clone (GNU patch, NOT git apply: git silently SKIPS files with
# "Skipped patch ..." when the tree sits inside another git repo, and exits 0).
# Idempotent via dawn.json content marker plus hash stamp.
# Usage (from SwitchDeps.cmake, before extern/aurora):
#   dusk_switch_apply_dawn_patch("<dawn-source-dir>")
function(dusk_switch_apply_dawn_patch _dawn_src)
  set(_patch "${CMAKE_CURRENT_LIST_DIR}/../platforms/switch/patches/dawn-switch.patch")
  if(NOT EXISTS "${_patch}")
    message(FATAL_ERROR "SwitchDawn: patch not found: ${_patch}")
  endif()
  if(NOT EXISTS "${_dawn_src}/src/dawn/dawn.json")
    message(FATAL_ERROR "SwitchDawn: not a Dawn tree: ${_dawn_src}")
  endif()

  file(SHA256 "${_patch}" _patch_hash)
  set(_stamp_dir "${CMAKE_BINARY_DIR}/_deps/switch-dawn-patch-stamps")
  set(_stamp "${_stamp_dir}/${_patch_hash}.stamp")
  file(READ "${_dawn_src}/src/dawn/dawn.json" _dawn_json)
  if(EXISTS "${_stamp}" AND _dawn_json MATCHES "surface source switch NWindow")
    message(STATUS "SwitchDawn: already applied (${_patch_hash})")
    return()
  endif()

  find_program(_patch_exe NAMES patch
    HINTS
      "$ENV{DEVKITPRO}/msys2/usr/bin"
      "C:/devkitPro/msys2/usr/bin"
      "C:/Program Files/Git/usr/bin"
  )
  if(NOT _patch_exe)
    message(FATAL_ERROR "SwitchDawn: GNU patch not found. Install patch "
      "(msys2: pacman -S patch, or Git for Windows which ships usr/bin/patch.exe).")
  endif()
  find_program(_git_exe NAMES git
    HINTS
      "$ENV{DEVKITPRO}/msys2/usr/bin"
      "C:/devkitPro/msys2/usr/bin"
      "C:/Program Files/Git/cmd"
  )
  message(STATUS "SwitchDawn: applying dawn-switch.patch to ${_dawn_src} with ${_patch_exe}")
  execute_process(
    COMMAND "${_patch_exe}" -p1 --forward --batch --no-backup-if-mismatch -i "${_patch}"
    WORKING_DIRECTORY "${_dawn_src}"
    RESULT_VARIABLE _apply_result
    OUTPUT_VARIABLE _apply_out
    ERROR_VARIABLE _apply_err
  )
  if(NOT _apply_result EQUAL 0)
    message(STATUS "SwitchDawn: incremental apply failed, reverting patched files and retrying fresh")
    if(NOT _git_exe)
      message(FATAL_ERROR "SwitchDawn: patch failed and git not found for recovery:\n${_apply_out}\n${_apply_err}\n"
        "Revert Dawn sources manually: git -C <clone> checkout -- src")
    endif()
    execute_process(
      COMMAND "${_git_exe}" -c core.autocrlf=false checkout -- src
      WORKING_DIRECTORY "${_dawn_src}"
      RESULT_VARIABLE _co_result
      OUTPUT_VARIABLE _co_out
      ERROR_VARIABLE _co_err
    )
    file(GLOB_RECURSE _rej_files "${_dawn_src}/src/*.rej")
    file(REMOVE ${_rej_files})
    if(NOT _co_result EQUAL 0)
      message(FATAL_ERROR "SwitchDawn: git checkout recovery failed:\n${_co_out}\n${_co_err}")
    endif()
    execute_process(
      COMMAND "${_patch_exe}" -p1 --batch --no-backup-if-mismatch -i "${_patch}"
      WORKING_DIRECTORY "${_dawn_src}"
      RESULT_VARIABLE _apply_result
      OUTPUT_VARIABLE _apply_out
      ERROR_VARIABLE _apply_err
    )
    if(NOT _apply_result EQUAL 0)
      message(FATAL_ERROR "SwitchDawn: fresh patch apply failed:\n${_apply_out}\n${_apply_err}")
    endif()
  endif()
  file(READ "${_dawn_src}/src/dawn/dawn.json" _dawn_json_verify)
  if(NOT _dawn_json_verify MATCHES "surface source switch NWindow")
    message(FATAL_ERROR "SwitchDawn: patch ran but Switch marker missing in dawn.json")
  endif()
  file(MAKE_DIRECTORY "${_stamp_dir}")
  file(GLOB _old_stamps "${_stamp_dir}/*.stamp")
  foreach(_old IN LISTS _old_stamps)
    if(NOT _old STREQUAL _stamp)
      file(REMOVE "${_old}")
    endif()
  endforeach()
  file(WRITE "${_stamp}" "${_patch_hash}\n")
  message(STATUS "SwitchDawn: applied (${_patch_hash})")
endfunction()
