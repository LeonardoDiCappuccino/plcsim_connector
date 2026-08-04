find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)

# Prefer an already-available pybind11
include(FetchContent)
FetchContent_Declare(pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS 2.12 NAMES pybind11)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(plcsim_python_core MODULE module.cpp)

set_target_properties(plcsim_python_core PROPERTIES
    OUTPUT_NAME _core
    # Land next to the pure-Python package so the build tree is importable
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/plcsim_connector/$<0:>")

target_link_libraries(plcsim_python_core PRIVATE plcsim::core)

target_compile_definitions(plcsim_python_core PRIVATE
    PLCSIM_CONNECTOR_VERSION="${PROJECT_VERSION}"
    PLCSIM_API_VERSION="${PLCSIM_API_VERSION}")

# Mirror the pure-Python sources into the build tree
set(_plcsim_py_files
    plcsim_connector/__init__.py
    plcsim_connector/__main__.py
    plcsim_connector/_core.pyi
    plcsim_connector/py.typed)

foreach(_file IN LISTS _plcsim_py_files)
    add_custom_command(TARGET plcsim_python_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${_file}"
            "${CMAKE_BINARY_DIR}/python/${_file}"
        COMMENT "Staging ${_file}"
        VERBATIM)
endforeach()

# scikit-build-core drives this when the project is pip-installed.
if(DEFINED SKBUILD)
    install(TARGETS plcsim_python_core DESTINATION plcsim_connector)
    install(FILES
        plcsim_connector/__init__.py
        plcsim_connector/__main__.py
        plcsim_connector/_core.pyi
        plcsim_connector/py.typed
        DESTINATION plcsim_connector)
endif()
