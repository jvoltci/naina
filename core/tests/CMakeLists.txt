add_executable(test_version test_version.cc)
target_link_libraries(test_version PRIVATE naina::core naina_warnings)
add_test(NAME version COMMAND test_version)
