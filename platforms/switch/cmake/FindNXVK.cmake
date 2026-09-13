# FindNXVK
# NXVK_ROOT or DEVKITPRO/portlibs/switch

set(NXVK_ROOT "" CACHE PATH "NXVK root (portlib or source tree)")

set(_nxvk_roots "")
if(NXVK_ROOT)
  list(APPEND _nxvk_roots "${NXVK_ROOT}")
endif()
if(DEFINED ENV{DEVKITPRO})
  list(APPEND _nxvk_roots "$ENV{DEVKITPRO}/portlibs/switch")
endif()

find_path(NXVK_INCLUDE_DIR
  NAMES vulkan/vulkan.h
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)

find_library(NXVK_LIB
  NAMES nvk
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES lib switch/build/cross/src/nouveau/vulkan
  NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_library(NXVK_SUPPORT_LIB
  NAMES nvk_support
  PATHS ${_nxvk_roots}
  PATH_SUFFIXES lib
  NO_DEFAULT_PATH
)

find_package_handle_standard_args(NXVK
  REQUIRED_VARS NXVK_LIB NXVK_SUPPORT_LIB NXVK_INCLUDE_DIR
)

if(NXVK_FOUND)
  set(NXVK_INCLUDE_DIRS "${NXVK_INCLUDE_DIR}")
  set(NXVK_LIBRARIES "${NXVK_LIB}")
  if(NXVK_SUPPORT_LIB)
    list(APPEND NXVK_LIBRARIES "${NXVK_SUPPORT_LIB}")
  endif()
  if(NOT TARGET NXVK::nvk)
    add_library(NXVK::nvk UNKNOWN IMPORTED)
    set_target_properties(NXVK::nvk PROPERTIES
      IMPORTED_LOCATION "${NXVK_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${NXVK_INCLUDE_DIR}"
      INTERFACE_COMPILE_DEFINITIONS "__SWITCH__;VK_USE_PLATFORM_VI_NN"
      INTERFACE_LINK_OPTIONS "-Wl,--whole-archive;-Wl,-u,vk_icdGetInstanceProcAddr;-Wl,--no-whole-archive;-Wl,--gc-sections"
    )
    # Mesa support libs (nir/util/blake3/ralloc/...) that libnvk needs.
    # Mesa xmlconfig (in nvk_support) needs expat (XML_*).
    set(_switch_expat "$ENV{DEVKITPRO}/portlibs/switch/lib/libexpat.a")
    if(EXISTS "${_switch_expat}")
      set_target_properties(NXVK::nvk PROPERTIES
        INTERFACE_LINK_LIBRARIES "${NXVK_SUPPORT_LIB};${_switch_expat}"
      )
    else()
      set_target_properties(NXVK::nvk PROPERTIES
        INTERFACE_LINK_LIBRARIES "${NXVK_SUPPORT_LIB}"
      )
    endif()
    unset(_switch_expat)
  endif()
endif()

mark_as_advanced(NXVK_INCLUDE_DIR NXVK_LIB)
