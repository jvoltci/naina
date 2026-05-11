cmake_minimum_required(VERSION 3.24)

# cmake-js invokes this CMakeLists from bindings/node, providing CMAKE_JS_INC,
# CMAKE_JS_LIB, CMAKE_JS_SRC. We pull naina-core in from the repo root.
project(naina-node CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build the C++ core as part of this addon (static, no tests, no warnings-as-errors
# so consumers' nodejs toolchains don't fight us over -Werror nits).
set(NAINA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NAINA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NAINA_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. naina_core_build EXCLUDE_FROM_ALL)

# ── Native module ──────────────────────────────────────────────────────

file(GLOB SOURCE_FILES src/*.cc)
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_JS_INC})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} naina::core)

# node-addon-api header path — discovered via `node -p`.
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE "[\r\n\"]" "" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE "${NODE_ADDON_API_DIR}")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NAPI_VERSION=8
    NAPI_DISABLE_CPP_EXCEPTIONS
)
