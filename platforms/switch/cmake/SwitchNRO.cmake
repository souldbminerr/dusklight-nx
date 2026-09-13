# SwitchNRO
# elf -> nro via elf2nro + nacptool

function(switch_target_link_libnx target)
  if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set.")
  endif()
  # NOTE: do NOT add -specs=switch.specs here. devkitPro's
  # Platform/NintendoSwitch.cmake already injects it into every target's
  # link flags, and switch.specs expands to '-T switch.ld' -- passing
  # -specs twice makes ld fail with "linker script file appears
  # multiple times". -lnx is also in the platform's standard libs, but
  # repeating a -l is harmless, so keep it explicit.
  target_link_libraries(${target} PRIVATE nx)
endfunction()

function(switch_add_nro target)
  cmake_parse_arguments(ARG "" "TITLE;AUTHOR;VERSION;ICON;ROMFSDIR" "" ${ARGN})
  if(NOT ARG_TITLE)
    set(ARG_TITLE "${target}")
  endif()
  if(NOT ARG_AUTHOR)
    set(ARG_AUTHOR "dusklight")
  endif()
  if(NOT ARG_VERSION)
    set(ARG_VERSION "1.0.0")
  endif()

  find_program(NACPTGTOOL nacptool REQUIRED)
  find_program(ELF2NRO elf2nro REQUIRED)

  set(_nacp "${CMAKE_CURRENT_BINARY_DIR}/${target}.nacp")
  add_custom_command(
    OUTPUT "${_nacp}"
    COMMAND "${NACPTGTOOL}" --create "${ARG_TITLE}" "${ARG_AUTHOR}" "${ARG_VERSION}" "${_nacp}"
    VERBATIM
  )

  set(_nro "${CMAKE_CURRENT_BINARY_DIR}/${target}.nro")
  set(_nro_deps "$<TARGET_FILE:${target}>" "${_nacp}")
  set(_nro_cmd "${ELF2NRO}" "$<TARGET_FILE:${target}>" "${_nro}" "--nacp=${_nacp}")
  if(ARG_ICON AND EXISTS "${ARG_ICON}")
    list(APPEND _nro_cmd "--icon=${ARG_ICON}")
    list(APPEND _nro_deps "${ARG_ICON}")
  endif()
  if(ARG_ROMFSDIR AND EXISTS "${ARG_ROMFSDIR}")
    list(APPEND _nro_cmd "--romfsdir=${ARG_ROMFSDIR}")
  endif()

  add_custom_command(
    OUTPUT "${_nro}"
    COMMAND ${_nro_cmd}
    DEPENDS ${_nro_deps} ${target}
    VERBATIM
  )
  add_custom_target(${target}_nro ALL DEPENDS "${_nro}")
endfunction()
