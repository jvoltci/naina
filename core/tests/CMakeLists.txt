function(naina_add_test name)
    add_executable(${name} ${name}.cc)
    target_link_libraries(${name} PRIVATE naina::core naina_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

naina_add_test(test_version)
naina_add_test(test_tensor)
naina_add_test(test_backend_registry)
naina_add_test(test_sha256)
naina_add_test(test_model_loader)
naina_add_test(test_engine_lifecycle)
naina_add_test(test_image_ops_warp)
naina_add_test(test_geometry)
naina_add_test(test_db_postprocess)
naina_add_test(test_charset)
naina_add_test(test_ctc_decode)

# Sha256 test reaches into core/src/ for the internal header.
target_include_directories(test_sha256 PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_include_directories(test_image_ops_warp PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_include_directories(test_geometry PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_include_directories(test_db_postprocess PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_include_directories(test_charset PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
target_include_directories(test_ctc_decode PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

# These tests need to resolve paths under the repo root (models/, etc).
set_tests_properties(test_model_loader test_engine_lifecycle PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

naina_add_test(test_page)
target_include_directories(test_page PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

naina_add_test(test_ocr_e2e)
# Runs against real PP-OCRv6 weights. Skips cleanly when they are not in the
# cache, so an offline checkout still passes; set NAINA_OFFLINE=0 and run once
# to populate the cache.
set_tests_properties(test_ocr_e2e PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "NAINA_E2E_FIXTURE=${CMAKE_SOURCE_DIR}/core/tests/fixtures/hello_world.rgb"
)

# A build with no inference backend cannot exercise the inference path, and a
# silent skip once hid a clean rebuild that had dropped ONNX Runtime entirely
# while every test still reported green. Make that state fail loudly.
if(NAINA_WITH_ONNXRUNTIME OR NAINA_WITH_NCNN)
    set_tests_properties(test_engine_lifecycle PROPERTIES
        ENVIRONMENT "NAINA_REQUIRE_BACKEND=1"
    )
endif()
