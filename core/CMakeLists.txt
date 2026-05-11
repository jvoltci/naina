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
)

# yaml-cpp is required for parsing models/registry.yaml.
find_package(yaml-cpp REQUIRED)

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
target_link_libraries(naina-core PRIVATE naina_warnings yaml-cpp::yaml-cpp)

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

if(NAINA_WITH_ONNXRUNTIME)
    target_link_libraries(naina-core PRIVATE ONNXRuntime::ONNXRuntime)
    target_compile_definitions(naina-core PRIVATE NAINA_HAVE_ONNXRUNTIME=1)
endif()

if(NAINA_WITH_NCNN)
    target_link_libraries(naina-core PRIVATE NCNN::NCNN)
    target_compile_definitions(naina-core PRIVATE NAINA_HAVE_NCNN=1)
endif()

install(TARGETS naina-core
    EXPORT naina-targets
    RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
