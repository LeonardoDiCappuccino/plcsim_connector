find_package(PlcSimAdvancedApi REQUIRED)

# ---------------------------------------------------------------------------
# plcsim_core - the C++ connector
#
# Split deliberately into two groups:
#   * SDK-free sources (Address, Error, Types) - pure, portable, unit-testable
#     without S7-PLCSIM Advanced installed. tests/ links these directly.
#   * SdkBridge.cpp - the sole consumer of SimulationRuntimeApi.h.
# ---------------------------------------------------------------------------
set(PLCSIM_PURE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Address.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Error.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Types.cpp"
    CACHE INTERNAL "Sources that do not depend on the Siemens SDK")

add_library(plcsim_core STATIC
    ${PLCSIM_PURE_SOURCES}
    SdkBridge.cpp)

add_library(plcsim::core ALIAS plcsim_core)

target_include_directories(plcsim_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# PRIVATE: the Siemens header is an implementation detail. Consumers of
# plcsim_core get our headers only - no Windows.h, no SimulationRuntimeApi.h.
target_link_libraries(plcsim_core PRIVATE PlcSimAdvanced::Api)

target_compile_features(plcsim_core PUBLIC cxx_std_20)

set_target_properties(plcsim_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME plcsim_core)

target_compile_options(plcsim_core PRIVATE /W4)
if(PLCSIM_WARNINGS_AS_ERRORS)
    target_compile_options(plcsim_core PRIVATE /WX)
endif()
