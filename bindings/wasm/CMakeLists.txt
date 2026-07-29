# WASM binding. Configured with the Emscripten toolchain:
#
#   emcmake cmake -S . -B build/wasm -DNAINA_BUILD_WASM=ON \
#       -DNAINA_WITH_WASMJS=ON -DNAINA_BUILD_TESTS=OFF \
#       -DNAINA_BUILD_SHARED=OFF -DNAINA_VENDOR_YAMLCPP=ON -DNAINA_RELEASE=ON
#   cmake --build build/wasm
#
# Produces naina.mjs + naina.wasm in bindings/wasm/dist/.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "bindings/wasm requires an Emscripten toolchain (use emcmake)")
endif()

add_executable(naina_wasm naina_wasm.cc)

target_compile_features(naina_wasm PRIVATE cxx_std_20)

# Backends self-register through static initialisers in their own translation
# units, which resolve no undefined symbol — so the linker drops those objects
# from a static archive and naina comes up with no backend at all. It fails as
# "backend unavailable" at naina_init, which reads like a configuration problem
# rather than a link one.
#
# Same fix and same reason as bindings/python/CMakeLists.txt; wasm-ld takes the
# GNU spelling.
target_link_libraries(naina_wasm PRIVATE
    -Wl,--whole-archive naina-core -Wl,--no-whole-archive)

# ASYNCIFY is load-bearing, not a convenience. onnxruntime-web's run() returns a
# Promise while naina's ISession::run() is synchronous; Asyncify unwinds and
# rewinds the WASM stack across that await so the C++ core needs no async
# variant. The alternative — SharedArrayBuffer + Atomics.wait in a worker —
# needs COOP/COEP response headers, which GitHub Pages cannot set.
#
# ASYNCIFY_IMPORTS lists exactly the calls that may suspend. Emscripten can
# usually infer these from EM_ASYNC_JS, but naming them keeps the instrumented
# call graph small, which is what keeps the size cost down.
target_link_options(naina_wasm PRIVATE
    -sASYNCIFY=1
    -sASYNCIFY_IMPORTS=js_session_create,js_session_run
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createNaina
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    # A 269 MB tier plus intermediate tensors needs real headroom, and growth
    # from a small initial heap causes repeated copies on large pages.
    -sINITIAL_MEMORY=64MB
    -sSTACK_SIZE=8MB
    # HEAP* are needed because the JS bridge reads tensor descriptors and copies
    # results straight out of the WASM heap. Current Emscripten does not attach
    # them to Module unless asked. Note they are *replaced* on memory growth, so
    # runtime.mjs must re-read Module.HEAP32 on every access rather than cache it.
    -sEXPORTED_RUNTIME_METHODS=FS,stringToUTF8,UTF8ToString,lengthBytesUTF8,HEAP8,HEAPU8,HEAP32,HEAPU32,HEAPF32
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sFILESYSTEM=1
    -sINVOKE_RUN=0
    --bind
    -O3
    # The registry is a few KB and is part of the build, so embed it rather than
    # making the page fetch a file it cannot function without. NAINA_REGISTRY
    # points the core at this path (set in Module.preRun, see loader.mjs).
    --embed-file "${CMAKE_SOURCE_DIR}/models/registry.yaml@/naina/registry.yaml"
)

# -O3 on the compile side too; Emscripten needs it on both to shrink output.
target_compile_options(naina_wasm PRIVATE -O3)

set_target_properties(naina_wasm PROPERTIES
    OUTPUT_NAME naina
    SUFFIX ".mjs"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dist
)
