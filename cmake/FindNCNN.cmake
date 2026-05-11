# FindNCNN.cmake
#
# Locates the NCNN library (Tencent's high-performance edge inference engine).
# Exposes the `NCNN::NCNN` imported target.
#
# NCNN's own CMake config (when installed via `make install`) provides a
# `ncnnConfig.cmake`. We try config-mode first, then fall back to manual probe.

include(FindPackageHandleStandardArgs)

# NCNN's CMake config references OpenMP::OpenMP_CXX; ensure it exists before
# the consume call. On Apple's clang, point find_package(OpenMP) at Homebrew's
# libomp so it can be located.
if(APPLE AND NOT TARGET OpenMP::OpenMP_CXX)
    foreach(_omp_root /opt/homebrew/opt/libomp /usr/local/opt/libomp)
        if(EXISTS "${_omp_root}/include/omp.h")
            set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${_omp_root}/include")
            set(OpenMP_CXX_LIB_NAMES "omp")
            set(OpenMP_omp_LIBRARY "${_omp_root}/lib/libomp.dylib")
            break()
        endif()
    endforeach()
endif()
find_package(OpenMP QUIET)

# Config-mode first (works for vcpkg, brew, build-from-source installs).
find_package(ncnn QUIET CONFIG)

if(ncnn_FOUND)
    set(NCNN_FOUND TRUE)
    set(NCNN_VERSION ${ncnn_VERSION})
    if(NOT TARGET NCNN::NCNN)
        add_library(NCNN::NCNN INTERFACE IMPORTED)
        target_link_libraries(NCNN::NCNN INTERFACE ncnn)
    endif()
    find_package_handle_standard_args(NCNN
        REQUIRED_VARS ncnn_DIR
        VERSION_VAR   NCNN_VERSION
    )
    return()
endif()

# Manual fallback.
set(_ncnn_hints
    "${NCNN_ROOT}"
    "$ENV{NCNN_ROOT}"
    "${CMAKE_PREFIX_PATH}"
)

find_path(NCNN_INCLUDE_DIR
    NAMES ncnn/net.h net.h
    HINTS ${_ncnn_hints}
    PATH_SUFFIXES include include/ncnn
)

find_library(NCNN_LIBRARY
    NAMES ncnn
    HINTS ${_ncnn_hints}
    PATH_SUFFIXES lib lib64
)

find_package_handle_standard_args(NCNN
    REQUIRED_VARS NCNN_LIBRARY NCNN_INCLUDE_DIR
)

if(NCNN_FOUND AND NOT TARGET NCNN::NCNN)
    add_library(NCNN::NCNN UNKNOWN IMPORTED)
    set_target_properties(NCNN::NCNN PROPERTIES
        IMPORTED_LOCATION "${NCNN_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${NCNN_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(NCNN_INCLUDE_DIR NCNN_LIBRARY)
