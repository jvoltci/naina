# NainaRustLinkManifest.cmake
#
# Writes the resolved link line of a target to a file, for a consumer that is
# not CMake.
#
# Only the Rust crate needs this. The node addon and the Python extension both
# link naina-core through CMake, so CMake resolves the transitive dependencies
# of core/ for them. cargo issues the final link for the crate instead, and
# libnaina.a is a plain archive carrying no record of what it needs — so
# build.rs had to restate every dependency of the core by hand. It restated one
# of three (ONNX Runtime, and only when ONNXRUNTIME_ROOT happened to be set);
# yaml-cpp and libcurl were missing outright, and the rust CI job never linked
# once from the commit that introduced it.
#
# Reading CMake's answer instead means a new dependency in core/ needs no edit
# in build.rs.
#
#   naina_write_rust_link_manifest(<target> <outfile>)
#
# The file is `key=value` lines, in link order — dependents before their
# dependencies, which is what a one-pass ELF link needs to resolve archives:
#
#   lib=/abs/path/libfoo.a     a library file, by absolute path
#   name=foo                   a bare library name, as in -lfoo
#   framework=CoreML           an Apple framework
#   flag=-Wl,--whatever        a raw linker flag
#
# An item this cannot classify is a FATAL_ERROR, never a skip. A dropped link
# input surfaces as an undefined symbol far from its cause, or worse as a
# backend that silently failed to register.

include_guard(GLOBAL)

# First occurrence wins. Repeats are harmless for shared libraries but would
# reorder static archives.
function(_naina_link_add entry)
    get_property(_items GLOBAL PROPERTY NAINA_RUST_LINK_ITEMS)
    if(NOT "${entry}" IN_LIST _items)
        set_property(GLOBAL APPEND PROPERTY NAINA_RUST_LINK_ITEMS "${entry}")
    endif()
endfunction()

# Depth-first pre-order over LINK_LIBRARIES + INTERFACE_LINK_LIBRARIES.
function(_naina_link_walk target)
    get_target_property(_alias ${target} ALIASED_TARGET)
    if(_alias)
        set(target ${_alias})
    endif()

    # A dependency named twice is normal; a cycle through INTERFACE_LINK_LIBRARIES
    # would otherwise recurse forever.
    get_property(_seen GLOBAL PROPERTY NAINA_RUST_LINK_SEEN)
    if("${target}" IN_LIST _seen)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY NAINA_RUST_LINK_SEEN "${target}")

    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        # Carries flags and further dependencies, but has no file to link.
    elseif(_type MATCHES "^(STATIC|SHARED|MODULE|UNKNOWN)_LIBRARY$")
        # TARGET_LINKER_FILE resolves at generate time, the earliest point at
        # which the path of a target built by this same build is known — which is
        # also why the vendored yaml-cpp archive must not be spelled out by hand:
        # _deps/ layout is a CMake implementation detail. It works on IMPORTED
        # targets too, where it yields IMPORTED_LOCATION.
        _naina_link_add("lib=$<TARGET_LINKER_FILE:${target}>")
    else()
        message(FATAL_ERROR
            "naina: cannot link ${target} (TYPE ${_type}) into the Rust crate")
    endif()

    set(_deps "")
    foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_value ${target} ${_prop})
        if(_value)
            list(APPEND _deps ${_value})
        endif()
    endforeach()

    foreach(_item IN LISTS _deps)
        # PRIVATE dependencies of a static library arrive wrapped in
        # $<LINK_ONLY:>, build-only interface targets in $<BUILD_INTERFACE:>.
        # Both are transparent to a linker; unwrap and carry on.
        #
        # They nest, which cost a build: a PRIVATE $<BUILD_INTERFACE:naina_warnings>
        # comes back out of INTERFACE_LINK_LIBRARIES as
        # $<LINK_ONLY:$<BUILD_INTERFACE:naina_warnings>>. Peel until stable rather
        # than once.
        set(_previous "")
        while(NOT _previous STREQUAL _item)
            set(_previous "${_item}")
            string(REGEX REPLACE "^\\$<(LINK_ONLY|BUILD_INTERFACE):(.+)>$" "\\2"
                   _item "${_item}")
        endwhile()

        # Quoted: an item like "-framework Foo" is one item, and unquoted it would
        # arrive at if(TARGET) as two arguments and abort the configure.
        if(TARGET "${_item}")
            _naina_link_walk("${_item}")
        elseif(_item MATCHES "^\\$<")
            message(FATAL_ERROR
                "naina: unhandled generator expression in ${target}'s link "
                "libraries: ${_item}. Teach cmake/NainaRustLinkManifest.cmake "
                "about it rather than dropping it.")
        elseif(_item STREQUAL "-framework")
            # CMake sometimes splits `-framework Foo` into two list items, and a
            # split pair would emit a flag with no framework and a bogus -lFoo.
            message(FATAL_ERROR
                "naina: ${target} lists a split -framework pair. Pass it as one "
                "string, \"-framework Foo\", so it survives as one item.")
        elseif(_item MATCHES "::")
            # A namespaced name is never a linker argument, so this is a target
            # that is not visible from here — find_package creates its imported
            # targets in the calling directory's scope. Emitting it as a name
            # produced a mystifying `-lCURL::libcurl` once; say what it is.
            message(FATAL_ERROR
                "naina: ${_item} is not a target in this scope. Call "
                "naina_write_rust_link_manifest() from the directory that "
                "found it.")
        elseif(IS_ABSOLUTE "${_item}")
            _naina_link_add("lib=${_item}")
        elseif(_item MATCHES "^-framework +(.+)$")
            _naina_link_add("framework=${CMAKE_MATCH_1}")
        elseif(_item MATCHES "^-l(.+)$")
            _naina_link_add("name=${CMAKE_MATCH_1}")
        elseif(_item MATCHES "^-")
            _naina_link_add("flag=${_item}")
        else()
            _naina_link_add("name=${_item}")
        endif()
    endforeach()
endfunction()

function(naina_write_rust_link_manifest target outfile)
    set_property(GLOBAL PROPERTY NAINA_RUST_LINK_ITEMS "")
    set_property(GLOBAL PROPERTY NAINA_RUST_LINK_SEEN "")
    _naina_link_walk(${target})
    get_property(_items GLOBAL PROPERTY NAINA_RUST_LINK_ITEMS)
    list(JOIN _items "\n" _lines)

    # file(GENERATE), not file(WRITE): TARGET_LINKER_FILE is unresolvable at
    # configure time.
    #
    # Single-config generators only. A multi-config generator (Visual Studio,
    # Xcode) resolves these paths per configuration, and file(GENERATE) refuses
    # to write differing content to one path — so it stops with an error naming
    # this file rather than emitting a wrong link line. Put $<CONFIG> in the
    # output name if the crate is ever built on Windows; it never has been.
    file(GENERATE OUTPUT "${outfile}" CONTENT
"# naina link inputs, resolved by CMake. Generated — do not edit.
# Consumed by bindings/rust/build.rs; format in cmake/NainaRustLinkManifest.cmake.
${_lines}
")
    message(STATUS "  Rust link manifest: ${outfile}")
endfunction()
