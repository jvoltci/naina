cmake_minimum_required(VERSION 3.24)

project(naina
    VERSION 0.1.0
    DESCRIPTION "Embeddable computer-vision runtime for face & person understanding"
    LANGUAGES CXX C
)

# ─── Project-wide settings ─────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Don't hide-default the whole project: naina.hpp, backend.hpp, and tensor.hpp
# expose C++ APIs that consumers (and our own tests) link against directly.
# The C ABI in naina.h has its own NAINA_API decoration for cross-binary safety
# (dllimport/dllexport on Windows, default visibility on POSIX).

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ─── Options ───────────────────────────────────────────────────────────
# Backends start OFF; they turn ON in Phase C when their wrappers land.
option(NAINA_BUILD_TESTS         "Build naina unit tests"          ON)
option(NAINA_BUILD_SHARED        "Build naina as a shared library" ON)
option(NAINA_BUILD_PYTHON        "Build the Python binding (naina-py)" OFF)
option(NAINA_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors" ON)

option(NAINA_WITH_ONNXRUNTIME    "Enable ONNX Runtime backend" OFF)
option(NAINA_WITH_NCNN           "Enable NCNN backend"         OFF)
option(NAINA_WITH_OPENVINO       "Enable OpenVINO backend"     OFF)
option(NAINA_WITH_COREML         "Enable CoreML backend (Apple only)" OFF)
option(NAINA_WITH_TENSORRT       "Enable TensorRT backend (NVIDIA only)" OFF)

# ─── Warnings ──────────────────────────────────────────────────────────

add_library(naina_warnings INTERFACE)
if(MSVC)
    target_compile_options(naina_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
    if(NAINA_WARNINGS_AS_ERRORS)
        target_compile_options(naina_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(naina_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
    if(NAINA_WARNINGS_AS_ERRORS)
        target_compile_options(naina_warnings INTERFACE -Werror)
    endif()
endif()

# ─── Subdirs ───────────────────────────────────────────────────────────

add_subdirectory(core)

if(NAINA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

if(NAINA_BUILD_PYTHON)
    add_subdirectory(bindings/python)
endif()

# ─── Install + export ──────────────────────────────────────────────────

# C++ install / package-config rules fire for the standalone library
# install, NOT for the Python wheel build (where we only want the .so and
# the registry.yaml). NAINA_BUILD_PYTHON suppresses these so the wheel
# stays small and self-contained.
if(NOT NAINA_BUILD_PYTHON)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(NAINA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/naina")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/nainaConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        cmake/nainaConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/nainaConfig.cmake"
        INSTALL_DESTINATION "${NAINA_INSTALL_CMAKEDIR}"
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/nainaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/nainaConfigVersion.cmake"
        DESTINATION "${NAINA_INSTALL_CMAKEDIR}"
    )

    install(EXPORT naina-targets
        FILE nainaTargets.cmake
        NAMESPACE naina::
        DESTINATION "${NAINA_INSTALL_CMAKEDIR}"
    )
endif()

# ─── Summary ───────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "── naina ${PROJECT_VERSION} ──")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  System:            ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  Compiler:          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Shared library:    ${NAINA_BUILD_SHARED}")
message(STATUS "  Tests:             ${NAINA_BUILD_TESTS}")
message(STATUS "  Backends:")
message(STATUS "    ONNX Runtime:    ${NAINA_WITH_ONNXRUNTIME}")
message(STATUS "    NCNN:            ${NAINA_WITH_NCNN}")
message(STATUS "    OpenVINO:        ${NAINA_WITH_OPENVINO}")
message(STATUS "    CoreML:          ${NAINA_WITH_COREML}")
message(STATUS "    TensorRT:        ${NAINA_WITH_TENSORRT}")
message(STATUS "")
