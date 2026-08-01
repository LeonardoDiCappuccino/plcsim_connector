# FindPlcSimAdvancedApi.cmake
# ---------------------------------------------------------------------------
# Locates the SIMATIC S7-PLCSIM Advanced native C++ SDK header.
#
# The SDK is header-only at link time: SimulationRuntimeApi.h defines
# InitializeApi() as an inline function that resolves and LoadLibraryW()s
# Siemens.Simatic.Simulation.Runtime.Api.x64.dll at *runtime*. There is no
# import library to link and no DLL to locate at build time.
#
# Cache variables:
#   PLCSIM_API_VERSION  API subdirectory to use (default: 8.0)
#   PLCSIM_API_DIR      Explicit directory containing SimulationRuntimeApi.h.
#                       Set this to override all automatic detection.
#
# Result variables:
#   PlcSimAdvancedApi_FOUND
#   PlcSimAdvancedApi_INCLUDE_DIR
#   PlcSimAdvancedApi_VERSION
#
# Imported target:
#   PlcSimAdvanced::Api  (INTERFACE)
# ---------------------------------------------------------------------------

set(PLCSIM_API_VERSION "8.0" CACHE STRING
    "S7-PLCSIM Advanced API version subdirectory to build against")

set(PLCSIM_API_DIR "" CACHE PATH
    "Directory containing SimulationRuntimeApi.h (overrides auto-detection)")

set(_plcsim_search_hints "")

# 1. Explicit override always wins.
if(PLCSIM_API_DIR)
    list(APPEND _plcsim_search_hints "${PLCSIM_API_DIR}")
endif()

# 2. Installation path from the registry. The key lives under Wow6432Node
#    because the SimRT installer is 32-bit, so query the 32-bit view.
if(WIN32)
    cmake_host_system_information(RESULT _plcsim_install_path
        QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Siemens/Shared Tools/PLCSIMADV_SimRT"
        VALUE "Path"
        VIEW 32
        ERROR_VARIABLE _plcsim_registry_error)

    if(_plcsim_install_path)
        file(TO_CMAKE_PATH "${_plcsim_install_path}" _plcsim_install_path)
        list(APPEND _plcsim_search_hints
            "${_plcsim_install_path}/API/${PLCSIM_API_VERSION}")
    endif()
endif()

# 3. Default installation location, in case the registry key is missing.
list(APPEND _plcsim_search_hints
    "C:/Program Files (x86)/Common Files/Siemens/PLCSIMADV/API/${PLCSIM_API_VERSION}")

find_path(PlcSimAdvancedApi_INCLUDE_DIR
    NAMES SimulationRuntimeApi.h
    HINTS ${_plcsim_search_hints}
    NO_DEFAULT_PATH
    DOC "Directory containing the S7-PLCSIM Advanced SimulationRuntimeApi.h header")

# Read the SDK's own version macro rather than trusting the directory name.
if(PlcSimAdvancedApi_INCLUDE_DIR)
    file(STRINGS "${PlcSimAdvancedApi_INCLUDE_DIR}/SimulationRuntimeApi.h"
        _plcsim_version_line
        REGEX "^#define[ \t]+DAPI_DLL_INTERFACE_VERSION[ \t]+0x[0-9A-Fa-f]+")

    if(_plcsim_version_line MATCHES "0x([0-9A-Fa-f]{4})([0-9A-Fa-f]{4})")
        # 0x00080000 -> major 0x0008, minor 0x0000 -> "8.0"
        math(EXPR _plcsim_major "0x${CMAKE_MATCH_1}")
        math(EXPR _plcsim_minor "0x${CMAKE_MATCH_2}")
        set(PlcSimAdvancedApi_VERSION "${_plcsim_major}.${_plcsim_minor}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PlcSimAdvancedApi
    REQUIRED_VARS PlcSimAdvancedApi_INCLUDE_DIR
    VERSION_VAR PlcSimAdvancedApi_VERSION
    FAIL_MESSAGE
        "Could not find SimulationRuntimeApi.h. Install SIMATIC S7-PLCSIM Advanced, \
or point -DPLCSIM_API_DIR=<dir> at the directory containing the header \
(typically 'C:/Program Files (x86)/Common Files/Siemens/PLCSIMADV/API/<version>'). \
Use -DPLCSIM_API_VERSION=<version> to select a different installed API version.")

if(PlcSimAdvancedApi_FOUND AND NOT TARGET PlcSimAdvanced::Api)
    add_library(PlcSimAdvanced::Api INTERFACE IMPORTED)
    set_target_properties(PlcSimAdvanced::Api PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${PlcSimAdvancedApi_INCLUDE_DIR}")

    # The header pulls in Windows.h; keep the damage contained for anything
    # that ends up in the same translation unit.
    target_compile_definitions(PlcSimAdvanced::Api INTERFACE
        WIN32_LEAN_AND_MEAN NOMINMAX)

    # SimulationRuntimeApi.h has '#pragma comment(lib, "Shlwapi.lib")' for
    # PathCombineW, but state it explicitly so the dependency is visible.
    target_link_libraries(PlcSimAdvanced::Api INTERFACE Shlwapi)
endif()

mark_as_advanced(PlcSimAdvancedApi_INCLUDE_DIR)
unset(_plcsim_search_hints)
