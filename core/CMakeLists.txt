if(NAINA_BUILD_SHARED)
    add_library(naina-core SHARED)
else()
    add_library(naina-core STATIC)
endif()
add_library(naina::core ALIAS naina-core)

set(NAINA_PUBLIC_HEADERS
    include/naina/naina.h
    include/naina/naina.hpp
    include/naina/backend.hpp
    include/naina/tensor.hpp
    include/naina/model_loader.hpp
)

set(NAINA_SOURCES
    src/version.cc
    src/tensor.cc
    src/backend_registry.cc
    src/sha256.cc
    src/model_loader.cc
    src/image_ops.cc
    src/geometry.cc
    src/modules/db_postprocess.cc
    src/modules/charset.cc
    src/modules/ctc_decode.cc
    src/modules/text_detect.cc
    src/modules/text_recognize.cc
    src/modules/layout_detect.cc
    src/modules/doc_assemble.cc
    src/page.cc
    src/api.cc
    src/api_cpp.cc
)

# yaml-cpp parses models/registry.yaml; libcurl fetches model weights.
#
# yaml-cpp is vendored as a STATIC library when NAINA_VENDOR_YAMLCPP is ON,
# which is the default for redistributable builds (wheels, npm addons). A
# system yaml-cpp leaves the extension linked against e.g.
# /opt/homebrew/opt/yaml-cpp/lib/libyaml-cpp.0.9.dylib, so anyone without that
# exact install — including every Intel Mac, where the Homebrew prefix
# differs — gets an ImportError. Static linking removes the runtime dependency
# outright rather than bundling another shared object.
if(NAINA_VENDOR_YAMLCPP)
    include(FetchContent)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # yaml-cpp 0.8.0 declares cmake_minimum_required(VERSION 3.4), which CMake
    # 4.x refuses outright. This is the documented escape hatch for consuming a
    # dependency that predates the 3.5 floor; it applies only to the fetched
    # subproject, not to naina.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG        0.8.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(yaml-cpp)

    # yaml-cpp 0.8.0's own headers shadow names in namespace YAML, which trips
    # naina's -Wshadow -Werror the moment they are included. Third-party code
    # should not be held to our warning flags: re-export its include directories
    # as SYSTEM so the compiler suppresses diagnostics from them.
    #
    # Only shows up when yaml-cpp is VENDORED -- a system copy is already found
    # via find_package, which marks its includes SYSTEM for us. That is why this
    # surfaced in the wheel build and nowhere else.
    get_target_property(NAINA_YAMLCPP_INC yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
    if(NAINA_YAMLCPP_INC)
        set_target_properties(yaml-cpp PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${NAINA_YAMLCPP_INC}")
    endif()

    message(STATUS "  yaml-cpp:          vendored (static, 0.8.0, headers SYSTEM)")
else()
    find_package(yaml-cpp REQUIRED)
    message(STATUS "  yaml-cpp:          system")
endif()

# Some targets have no libcurl to link and no business fetching anything
# themselves: Emscripten has no sockets layer, and the Android NDK ships no
# libcurl. On both, the host (the page, or the Dart side) fetches model files and
# stages them, and the core reads them off disk.
#
# sha256 verification still happens in the shared core, so a staged file is
# checked exactly as a downloaded one would be — the integrity guarantee does not
# depend on who did the fetching.
if(EMSCRIPTEN OR ANDROID)
    set(NAINA_NO_CURL ON)
    message(STATUS "  libcurl:           skipped (host stages models)")
else()
    set(NAINA_NO_CURL OFF)
    find_package(CURL REQUIRED)
endif()

# Backends are added conditionally; each backend file is gated on its option
# and Find* result, so a build without ONNX Runtime / NCNN still produces a
# working naina-core (without those EPs).

if(NAINA_WITH_ONNXRUNTIME)
    find_package(ONNXRuntime REQUIRED)
    list(APPEND NAINA_SOURCES src/backends/onnxruntime_backend.cc)
endif()

if(NAINA_WITH_NCNN)
    find_package(NCNN REQUIRED)
    list(APPEND NAINA_SOURCES src/backends/ncnn_backend.cc)
endif()

if(NAINA_WITH_WASMJS)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "NAINA_WITH_WASMJS requires an Emscripten toolchain")
    endif()
    list(APPEND NAINA_SOURCES src/backends/wasmjs_backend.cc)
endif()

target_sources(naina-core PRIVATE ${NAINA_SOURCES})
target_sources(naina-core PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${NAINA_PUBLIC_HEADERS}
)

target_include_directories(naina-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(naina-core PUBLIC cxx_std_20)
# naina_warnings is a build-only INTERFACE target (compile flags). Wrap it
# in BUILD_INTERFACE so it isn't required by the install/export set.
target_link_libraries(naina-core
    PRIVATE
        $<BUILD_INTERFACE:naina_warnings>
        yaml-cpp::yaml-cpp
)
if(NOT NAINA_NO_CURL)
    target_link_libraries(naina-core PRIVATE CURL::libcurl)
else()
    target_compile_definitions(naina-core PRIVATE NAINA_NO_CURL=1)
endif()

# Export symbols on Windows, hide on Unix (visibility is set at project level).
set_target_properties(naina-core PROPERTIES
    OUTPUT_NAME naina
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Tell naina.h to emit `__attribute__((visibility("default")))` / dllexport
# on public symbols when building the shared lib. Consumers see no decoration
# (or dllimport on Windows), automatically.
if(NAINA_BUILD_SHARED)
    target_compile_definitions(naina-core PRIVATE NAINA_BUILDING_SHARED=1)
endif()

# Drop the "-dev" suffix from naina_version_string() so a published artifact
# does not disagree with its own package metadata.
if(NAINA_RELEASE)
    target_compile_definitions(naina-core PRIVATE NAINA_RELEASE=1)
endif()

if(NAINA_WITH_ONNXRUNTIME)
    target_link_libraries(naina-core PRIVATE ONNXRuntime::ONNXRuntime)
    target_compile_definitions(naina-core PRIVATE NAINA_HAVE_ONNXRUNTIME=1)
endif()

if(NAINA_WITH_NCNN)
    target_link_libraries(naina-core PRIVATE NCNN::NCNN)
    target_compile_definitions(naina-core PRIVATE NAINA_HAVE_NCNN=1)
endif()

# cargo, not CMake, issues the final link for the Rust crate, and a static
# libnaina.a records nothing about what it needs. build.rs passes a path in;
# CMake — the only party that knows where ONNX Runtime was found, where the
# vendored yaml-cpp archive landed and which libcurl was linked — writes the
# answer there. Without it build.rs has to restate every transitive dependency
# of this file by hand, which is how the rust CI job shipped naming one of three.
#
# It has to happen *here*, not in the root CMakeLists: find_package creates
# imported targets in the calling directory's scope, so CURL::libcurl and
# ONNXRuntime::ONNXRuntime are not targets one level up. Called from the root
# they came out as bare -lCURL::libcurl.
if(NAINA_RUST_LINK_MANIFEST)
    include(NainaRustLinkManifest)
    naina_write_rust_link_manifest(naina-core "${NAINA_RUST_LINK_MANIFEST}")
endif()

if(NOT NAINA_BUILD_PYTHON)
    include(GNUInstallDirs)
    install(TARGETS naina-core
        EXPORT naina-targets
        RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
