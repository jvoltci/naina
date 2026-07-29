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
    message(STATUS "  yaml-cpp:          vendored (static, 0.8.0)")
else()
    find_package(yaml-cpp REQUIRED)
    message(STATUS "  yaml-cpp:          system")
endif()

find_package(CURL REQUIRED)

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
        CURL::libcurl
)

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
