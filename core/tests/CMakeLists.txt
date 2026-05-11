function(naina_add_test name)
    add_executable(${name} ${name}.cc)
    target_link_libraries(${name} PRIVATE naina::core naina_warnings)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

naina_add_test(test_version)
naina_add_test(test_tensor)
naina_add_test(test_backend_registry)
