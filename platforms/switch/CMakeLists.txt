# platforms/switch
# Game + runtime lib + nro wiring

if(NOT DUSK_PLATFORM_SWITCH)
  return()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(SwitchNRO)

# Patch Dawn-fetched abseil for libnx (needs GNU patch; runs pre-build).
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyDawnDepsPatch.cmake)
dusk_switch_patch_dawn_deps()

# Patch aurora-fetched Tracy for libnx (needs GNU patch; runs pre-build).
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ApplyTracyPatch.cmake)
dusk_switch_patch_tracy()

# sqlite amalgamation: no WAL/mmap on Switch; stub sys/mman.h for constants.
if(TARGET sqlite3)
  target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_WAL=1 SQLITE_OMIT_LOAD_EXTENSION=1)
  target_include_directories(sqlite3 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/compat")
endif()


# Tracy and Dawn link `dl` into their interfaces (bare `dl` via PUBLIC, or
# `$<LINK_ONLY:dl>` which CMake adds for PRIVATE plain-lib deps of static
# libs); bare-metal newlib has no libdl. Strip every rendering post-aurora
# (same configure, before generation).
foreach(_nodl_target IN ITEMS TracyClient webgpu_dawn dawn_common dawn_native_objects webgpu_dawn_objects)
  if(TARGET ${_nodl_target})
    foreach(_nodl_prop IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
      get_target_property(_nodl_val ${_nodl_target} ${_nodl_prop})
      if(_nodl_val AND NOT _nodl_val MATCHES "-NOTFOUND$")
        set(_nodl_fixed "")
        foreach(_nodl_item IN LISTS _nodl_val)
          if(_nodl_item MATCHES "(^|:)dl(>|$)|^-ldl$")
            # drop: no libdl on Switch
          else()
            list(APPEND _nodl_fixed "${_nodl_item}")
          endif()
        endforeach()
        set_target_properties(${_nodl_target} PROPERTIES ${_nodl_prop} "${_nodl_fixed}")
        unset(_nodl_fixed)
      endif()
      unset(_nodl_val)
    endforeach()
  endif()
endforeach()
unset(_nodl_target)

# Portlibs freetype was built with HarfBuzz (autofit shaper) and libharfbuzz
# in turn uses freetype (hb-ft): link freetype -> harfbuzz -> freetype.
# Freetype::Freetype comes from the portlibs CMake config.
if(TARGET Freetype::Freetype)
  set(_switch_hb "$ENV{DEVKITPRO}/portlibs/switch/lib/libharfbuzz.a")
  set(_switch_ft "$ENV{DEVKITPRO}/portlibs/switch/lib/libfreetype.a")
  if(EXISTS "${_switch_hb}" AND EXISTS "${_switch_ft}")
    get_target_property(_switch_ft_il Freetype::Freetype INTERFACE_LINK_LIBRARIES)
    if(NOT _switch_ft_il OR _switch_ft_il MATCHES "-NOTFOUND$")
      set(_switch_ft_il "")
    endif()
    set(_switch_bz2 "$ENV{DEVKITPRO}/portlibs/switch/lib/libbz2.a")
    if(EXISTS "${_switch_bz2}")
      list(APPEND _switch_ft_il "${_switch_hb}" "${_switch_ft}" "${_switch_bz2}")
    else()
      list(APPEND _switch_ft_il "${_switch_hb}" "${_switch_ft}")
    endif()
    unset(_switch_bz2)
    set_target_properties(Freetype::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES "${_switch_ft_il}")
    unset(_switch_ft_il)
  endif()
  unset(_switch_hb)
  unset(_switch_ft)
endif()

find_package(NXVK QUIET)

add_library(dusk_switch_runtime STATIC
  source/switch_runtime.cpp
  source/switch_posix_shim.cpp
  source/switch_egl_stub.cpp
  source/switch_fs.cpp
  source/switch_perf.cpp
  source/switch_vulkan_nxvk.cpp
  source/switch_audio_audren.cpp
  source/switch_input_hid.cpp
  source/speedrun_switch.cpp
)

target_include_directories(dusk_switch_runtime PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_include_directories(dusk_switch_runtime PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/sdk/include"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/extern/aurora/include/dolphin"
)

target_compile_features(dusk_switch_runtime PUBLIC cxx_std_20)
# Same game headers/defs the game TUs see (dolphin types, TARGET_PC, ...).
target_link_libraries(dusk_switch_runtime PRIVATE dusklight_game_headers)
# Game headers assume global.h (DUSK_GAME_DATA et al); force-include like game TUs get transitively.
target_compile_options(dusk_switch_runtime PRIVATE -include global.h)
target_compile_options(dusk_switch_runtime PRIVATE ${DUSK_SWITCH_OPT_FLAGS})

# Native borealis replacements (same target names, built only when
# borealis skips them on Switch).
if(NOT TARGET borealis_io)
  add_library(borealis_io STATIC source/borealis_io_switch.cpp)
  add_library(borealis::io ALIAS borealis_io)
  target_include_directories(borealis_io PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_compile_features(borealis_io PUBLIC cxx_std_20)
  target_compile_options(borealis_io PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_data)
  add_library(borealis_data STATIC source/borealis_data_switch.cpp)
  add_library(borealis::data ALIAS borealis_data)
  target_include_directories(borealis_data PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_data PUBLIC borealis::core borealis::io)
  target_compile_features(borealis_data PUBLIC cxx_std_20)
  target_compile_options(borealis_data PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_file_select)
  add_library(borealis_file_select STATIC source/borealis_file_select_switch.cpp)
  add_library(borealis::file_select ALIAS borealis_file_select)
  target_include_directories(borealis_file_select PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_compile_features(borealis_file_select PUBLIC cxx_std_20)
  target_compile_options(borealis_file_select PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT TARGET borealis_update)
  add_library(borealis_update STATIC source/borealis_update_switch.cpp)
  add_library(borealis::update ALIAS borealis_update)
  target_include_directories(borealis_update PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_update PUBLIC borealis::core
    borealis::http nlohmann_json::nlohmann_json)
  target_compile_features(borealis_update PUBLIC cxx_std_20)
  target_compile_options(borealis_update PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_presentation)
  add_library(borealis_presentation STATIC source/borealis_presentation_switch.cpp)
  add_library(borealis::presentation ALIAS borealis_presentation)
  target_include_directories(borealis_presentation PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_presentation PUBLIC borealis::core)
  target_compile_features(borealis_presentation PUBLIC cxx_std_20)
  target_compile_options(borealis_presentation PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_http)
  add_library(borealis_http STATIC source/borealis_http_switch.cpp)
  add_library(borealis::http ALIAS borealis_http)
  target_include_directories(borealis_http PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_http PUBLIC borealis::core borealis::task borealis::url)
  target_compile_features(borealis_http PUBLIC cxx_std_20)
  target_compile_options(borealis_http PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_net)
  add_library(borealis_net STATIC source/borealis_net_switch.cpp)
  add_library(borealis::net ALIAS borealis_net)
  target_include_directories(borealis_net PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_net PUBLIC borealis::core)
  target_compile_features(borealis_net PUBLIC cxx_std_20)
  target_compile_options(borealis_net PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_crash)
  add_library(borealis_crash STATIC source/borealis_crash_switch.cpp)
  add_library(borealis::crash ALIAS borealis_crash)
  target_include_directories(borealis_crash PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_crash PUBLIC borealis::core)
  target_compile_features(borealis_crash PUBLIC cxx_std_20)
  target_compile_options(borealis_crash PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_ws)
  add_library(borealis_ws STATIC source/borealis_ws_switch.cpp)
  add_library(borealis::ws ALIAS borealis_ws)
  target_include_directories(borealis_ws PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_ws PUBLIC borealis::core borealis::http)
  target_compile_features(borealis_ws PUBLIC cxx_std_20)
  target_compile_options(borealis_ws PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

if(NOT TARGET borealis_disc)
  if(NOT TARGET xxhash)
    include(FetchContent)
    FetchContent_Declare(xxhash
      URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz
      URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
      SOURCE_SUBDIR cmake_unofficial
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )
    set(XXHASH_BUILD_XXHSUM OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(xxhash)
  endif()
  add_library(borealis_disc STATIC source/borealis_disc_switch.cpp)
  add_library(borealis::disc ALIAS borealis_disc)
  target_include_directories(borealis_disc PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/borealis/include")
  target_link_libraries(borealis_disc PUBLIC borealis::core xxhash)
  target_compile_features(borealis_disc PUBLIC cxx_std_20)
  target_compile_options(borealis_disc PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

# Native aurora_dvd (same target names): raw-ISO DVD layer, no nod/Rust.
# AURORA_ENABLE_DVD=OFF keeps aurora from building its nod-backed version.
if(NOT TARGET aurora_dvd)
  add_library(aurora_dvd STATIC source/aurora_dvd_switch.cpp)
  add_library(aurora::dvd ALIAS aurora_dvd)
  target_include_directories(aurora_dvd PUBLIC
    "${CMAKE_SOURCE_DIR}/extern/aurora/include"
    "${CMAKE_SOURCE_DIR}/extern/aurora/include/dolphin")
  target_include_directories(aurora_dvd PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include")
  # Match game TUs (dusklight_game_headers): fixed-width int types + BOOL.
  target_compile_definitions(aurora_dvd PRIVATE TARGET_PC=1)
  target_compile_features(aurora_dvd PUBLIC cxx_std_20)
  target_compile_options(aurora_dvd PRIVATE ${DUSK_SWITCH_OPT_FLAGS})
endif()

# Whole-archive: the POSIX/EGL shims must resolve no matter where their
# consumers (SDL/NXVK/...) land in GNU ld's single left-to-right pass.
# (Spelled with raw -Wl flags: this bare-metal toolchain defines no
# LINK_LIBRARY feature for CMake to expand.)
target_link_libraries(dusklight PRIVATE "-Wl,--whole-archive" dusk_switch_runtime "-Wl,--no-whole-archive")
# borealis::net/crash were removed from GAME_LIBS on Switch; link our native
# shims (same target names) explicitly.
target_link_libraries(dusklight PRIVATE borealis::net borealis::crash)
switch_target_link_libnx(dusklight)

if(NXVK_FOUND)
  target_link_libraries(dusklight PRIVATE NXVK::nvk)
endif()

target_link_options(dusklight PRIVATE "-Wl,--gc-sections")

switch_add_nro(dusklight
  TITLE "Dusklight"
  AUTHOR "Twilit Realm"
  VERSION "${BOREALIS_APP_VERSION}"
  ICON "${CMAKE_CURRENT_LIST_DIR}/icon.jpg"
)

