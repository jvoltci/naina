# FindONNXRuntime.cmake
#
# Locates the ONNX Runtime C++ library. Exposes the `ONNXRuntime::ONNXRuntime`
# imported target.
#
# Lookup order:
#   1. ONNXRUNTIME_ROOT (env or CMake var)
#   2. CMAKE_PREFIX_PATH (e.g. vcpkg, system install)
#   3. pkg-config (Linux/macOS, some distros)
#
# Variables set:
#   ONNXRuntime_FOUND
#   ONNXRuntime_INCLUDE_DIR
#   ONNXRuntime_LIBRARY
#   ONNXRuntime_VERSION  (best effort, from header)

include(FindPackageHandleStandardArgs)

set(_onnxrt_hints
    "${ONNXRUNTIME_ROOT}"
    "$ENV{ONNXRUNTIME_ROOT}"
    "${CMAKE_PREFIX_PATH}"
)

find_path(ONNXRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS ${_onnxrt_hints}
    PATH_SUFFIXES include include/onnxruntime
)

find_library(ONNXRuntime_LIBRARY
    NAMES onnxruntime
    HINTS ${_onnxrt_hints}
    PATH_SUFFIXES lib lib64
)

if(ONNXRuntime_INCLUDE_DIR AND EXISTS "${ONNXRuntime_INCLUDE_DIR}/onnxruntime_c_api.h")
    file(STRINGS "${ONNXRuntime_INCLUDE_DIR}/onnxruntime_c_api.h" _ort_ver_line
         REGEX "ORT_API_VERSION ")
    if(_ort_ver_line MATCHES "([0-9]+)")
        set(ONNXRuntime_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

find_package_handle_standard_args(ONNXRuntime
    REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR
    VERSION_VAR   ONNXRuntime_VERSION
)

if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
    add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY)
