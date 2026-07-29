# Builds naina's core for Android.
#
# Kept separate from the repo root so the plugin can point the NDK at the same
# core sources without the root's tests, presets or install rules.
cmake_minimum_required(VERSION 3.22)
project(naina_android)

# ONNX Runtime comes from the AAR that build.gradle declares. Gradle unpacks it
# under the module's build directory; these are the paths it uses.
set(ORT_AAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/onnxruntime")
if(NOT EXISTS "${ORT_AAR_ROOT}/headers/onnxruntime_c_api.h")
    # Fall back to a caller-provided location, which is also how CI can point at
    # a cached extraction rather than re-resolving the AAR.
    if(DEFINED ORT_ANDROID_ROOT)
        set(ORT_AAR_ROOT "${ORT_ANDROID_ROOT}")
    else()
        message(FATAL_ERROR
            "ONNX Runtime headers not found under ${ORT_AAR_ROOT}. "
            "Run the Gradle task that extracts onnxruntime-android, or pass "
            "-DORT_ANDROID_ROOT=<dir containing headers/ and jni/<abi>/>.")
    endif()
endif()

set(ONNXRuntime_INCLUDE_DIR "${ORT_AAR_ROOT}/headers" CACHE PATH "" FORCE)
set(ONNXRuntime_LIBRARY
    "${ORT_AAR_ROOT}/jni/${ANDROID_ABI}/libonnxruntime.so" CACHE FILEPATH "" FORCE)

add_subdirectory(../../.. naina_core_build)
