cmake_minimum_required(VERSION 3.24)

project(plcsim_connector
    VERSION 0.1.0
    DESCRIPTION "C++/Python connector for SIMATIC S7-PLCSIM Advanced digital I/O"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Platform guards
#
# The Siemens SDK exposes IInstance as an abstract C++ class with virtual
# functions, so the object layout is MSVC's. There is no portable ABI here and
# no point in pretending otherwise.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "plcsim_connector requires Windows: S7-PLCSIM Advanced is Windows-only.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR
        "plcsim_connector requires the MSVC toolchain. The Siemens API exposes C++ classes "
        "with virtual functions, which are only ABI-compatible with MSVC.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "plcsim_connector requires a 64-bit build so that the x64 Siemens runtime DLL is loaded. "
        "Configure with an x64 generator/preset (e.g. -A x64).")
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PLCSIM_BUILD_PYTHON   "Build the Python extension module" ON)
option(PLCSIM_BUILD_TESTS    "Build the C++ test suite"          ON)
option(PLCSIM_BUILD_EXAMPLES "Build the C++ examples"            ON)
option(PLCSIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets VS Code's C/C++ and clangd extensions resolve includes without guessing.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# /Zc:__cplusplus makes MSVC report a truthful __cplusplus; /permissive- and
# /EHsc keep the code honest and exceptions working across the pybind11 boundary.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> /permissive- /EHsc /utf-8)

add_subdirectory(src)

if(PLCSIM_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()

if(PLCSIM_BUILD_EXAMPLES)
    add_subdirectory(examples/cpp)
endif()

if(PLCSIM_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / package config
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS plcsim_core
    EXPORT plcsim_connectorTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT plcsim_connectorTargets
    FILE plcsim_connectorTargets.cmake
    NAMESPACE plcsim::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcsim_connector)

configure_package_config_file(
    cmake/plcsim_connectorConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/plcsim_connectorConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcsim_connector)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/plcsim_connectorConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/plcsim_connectorConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/plcsim_connectorConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPlcSimAdvancedApi.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plcsim_connector)
