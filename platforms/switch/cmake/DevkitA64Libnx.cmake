# DevkitA64 + libnx
# Usage
# cmake --preset switch-devkitA64-release

cmake_minimum_required(VERSION 3.25)

set(CMAKE_SYSTEM_NAME NintendoSwitch)
set(CMAKE_SYSTEM_VERSION "DKA-NX-19")
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{DEVKITPRO})
  message(FATAL_ERROR "DEVKITPRO is not set.")
endif()

list(APPEND CMAKE_MODULE_PATH "$ENV{DEVKITPRO}/cmake")

set(DEVKITPRO "$ENV{DEVKITPRO}")

set(NX_ROOT "${DEVKITPRO}/libnx")

# SDL3-switch convention
set(SWITCH TRUE)
if(NOT EXISTS "${NX_ROOT}/switch.specs")
  message(WARNING "DevkitA64Libnx: NX_ROOT switch.specs not found at ${NX_ROOT}/switch.specs")
endif()

set(DEVKITA64 "${DEVKITPRO}/devkitA64")
set(LIBNX "${DEVKITPRO}/libnx")
set(SWITCH_PORTLIBS "${DEVKITPRO}/portlibs/switch")

find_program(CMAKE_C_COMPILER NAMES aarch64-none-elf-gcc HINTS "${DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES aarch64-none-elf-g++ HINTS "${DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES aarch64-none-elf-gcc HINTS "${DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_AR NAMES aarch64-none-elf-gcc-ar HINTS "${DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_RANLIB NAMES aarch64-none-elf-gcc-ranlib HINTS "${DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)

list(APPEND CMAKE_PROGRAM_PATH "${DEVKITPRO}/tools/bin" "${DEVKITA64}/bin")

set(CMAKE_FIND_ROOT_PATH "${DEVKITPRO}" "${DEVKITA64}" "${SWITCH_PORTLIBS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_INSTALL_PREFIX "${SWITCH_PORTLIBS}" CACHE PATH "Install prefix")
set(CMAKE_PREFIX_PATH "${SWITCH_PORTLIBS}" CACHE PATH "Find prefix")

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
# drop the default -ldl.
set(CMAKE_DL_LIBS "" CACHE STRING "No libdl on Switch" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(DUSK_SWITCH_ARCH_FLAGS
  "-march=armv8-a+crc+crypto;-mtune=cortex-a57;-mtp=soft;-fPIE"
  CACHE STRING "Tegra X1 arch flags")

add_compile_options(${DUSK_SWITCH_ARCH_FLAGS})
add_compile_options("-I${LIBNX}/include")
add_link_options(${DUSK_SWITCH_ARCH_FLAGS})
add_link_options("-L${LIBNX}/lib")
add_compile_definitions(SWITCH __SWITCH__=1 DUSK_PLATFORM_SWITCH=1 TARGET_SWITCH=1 _DEFAULT_SOURCE)

set(DUSK_SWITCH_SPECS_FLAG "-specs=${LIBNX}/switch.specs" CACHE STRING "libnx specs flag")
