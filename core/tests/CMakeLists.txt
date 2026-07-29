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
