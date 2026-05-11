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

# Sha256 test reaches into core/src/ for the internal header.
target_include_directories(test_sha256 PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

# Model-loader test runs from the build dir; ensure it can find the repo.
set_tests_properties(test_model_loader PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
