# Python binding (built when NAINA_BUILD_PYTHON=ON).
#
# pybind11 is provided by the build environment (scikit-build-core fetches
# it via pip). The wheel layout is:
#   naina/__init__.py     ← Python source (from bindings/python/naina)
#   naina/_binding.so     ← native extension (this target)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(naina-py MODULE src/module.cc)
set_target_properties(naina-py PROPERTIES
    OUTPUT_NAME _binding
    PREFIX ""                          # let pybind11/Python decide the suffix
)
target_link_libraries(naina-py PRIVATE naina::core)
target_compile_features(naina-py PRIVATE cxx_std_20)

# scikit-build-core sets CMAKE_INSTALL_PREFIX so that wheel.install-dir =
# "naina" puts the wheel-root at site-packages/naina/. We install the
# extension and the bundled registry RELATIVE to that root.
install(TARGETS naina-py LIBRARY DESTINATION .)
install(FILES ${CMAKE_SOURCE_DIR}/models/registry.yaml DESTINATION models)
