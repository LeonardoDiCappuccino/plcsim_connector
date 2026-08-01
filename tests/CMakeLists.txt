include(FetchContent)

# Catch2 v3. FIND_PACKAGE_ARGS lets a system/vcpkg install satisfy this without
# a download; otherwise it is fetched once and cached in the build tree.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS 3 NAMES Catch2)

FetchContent_MakeAvailable(Catch2)

# catch_discover_tests() lives in Catch2's extras directory; when Catch2 came
# from find_package the module is already on the path.
if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)

# ---------------------------------------------------------------------------
# Unit tests over the SDK-free code.
#
# These link the pure sources directly rather than plcsim_core, so the suite
# builds and runs on any Windows box - no S7-PLCSIM Advanced needed. That keeps
# the address parser and error classification covered in CI.
# ---------------------------------------------------------------------------
add_executable(plcsim_unit_tests
    test_address.cpp
    test_error.cpp
    ${PLCSIM_PURE_SOURCES})

target_include_directories(plcsim_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(plcsim_unit_tests PRIVATE Catch2::Catch2WithMain)
target_compile_features(plcsim_unit_tests PRIVATE cxx_std_20)
target_compile_options(plcsim_unit_tests PRIVATE /W4)

catch_discover_tests(plcsim_unit_tests
    TEST_PREFIX "unit."
    REPORTER console)

# ---------------------------------------------------------------------------
# Live tests. Built always so they cannot rot, but tagged [live] and excluded
# from the default CTest run: they need a running instance to attach to.
#
#   ctest                      -> unit tests only
#   ctest -L live              -> the live suite
#   ctest -L live -- --plcsim-instance MyPlc
# ---------------------------------------------------------------------------
add_executable(plcsim_live_tests test_live.cpp)
target_link_libraries(plcsim_live_tests PRIVATE plcsim::core Catch2::Catch2WithMain)
target_compile_features(plcsim_live_tests PRIVATE cxx_std_20)

set(PLCSIM_LIVE_INSTANCE "" CACHE STRING
    "Name of a running PLCSIM Advanced instance for the live test suite")

add_test(NAME live.instance
    COMMAND plcsim_live_tests "[live]" --instance "${PLCSIM_LIVE_INSTANCE}")
set_tests_properties(live.instance PROPERTIES
    LABELS live
    DISABLED $<NOT:$<BOOL:${PLCSIM_LIVE_INSTANCE}>>)
