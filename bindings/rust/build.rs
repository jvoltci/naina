// Build the C++ core and link it.
//
// Two paths, in order:
//
//   1. NAINA_LIB_DIR points at an existing build (fast for development in this
//      repo; no recompilation of the core per cargo invocation).
//   2. Otherwise compile the vendored core with CMake.
//
// ONNX Runtime is NOT vendored. It is ~17 MB per platform with its own platform
// floors (macOS arm64 needs 13.3, Linux needs glibc 2.28), and shipping it inside
// a crate would mean shipping four copies and inheriting all of that silently.
// Callers point at it with ONNXRUNTIME_ROOT, the same variable the rest of
// naina's tooling uses.

use std::{env, path::PathBuf};

fn main() {
    if env::var("CARGO_FEATURE_DOCSRS").is_ok() {
        println!("cargo:rustc-cfg=naina_no_native");
        return;
    }

    println!("cargo:rerun-if-env-changed=NAINA_LIB_DIR");
    println!("cargo:rerun-if-env-changed=ONNXRUNTIME_ROOT");

    // Path 1: link a library that already exists.
    if let Ok(dir) = env::var("NAINA_LIB_DIR") {
        println!("cargo:rustc-link-search=native={dir}");
        println!("cargo:rustc-link-lib=dylib=naina");
        emit_rpath(&dir);
        return;
    }

    // Path 2: compile the vendored core.
    let vendor = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("vendor");
    if !vendor.join("CMakeLists.txt").exists() {
        panic!(
            "naina: no vendored core at {} and NAINA_LIB_DIR is unset.\n\
             Inside this repository, point at a build:\n  \
             NAINA_LIB_DIR=<repo>/build/<preset>/core cargo test",
            vendor.display()
        );
    }

    let mut cfg = cmake::Config::new(&vendor);
    cfg.define("NAINA_BUILD_RUST", "ON")
        .define("NAINA_BUILD_TESTS", "OFF")
        .define("NAINA_BUILD_SHARED", "OFF")
        .define("NAINA_VENDOR_YAMLCPP", "ON")
        .define("NAINA_WITH_ONNXRUNTIME", "ON")
        .define("NAINA_RELEASE", "ON")
        .profile("Release");

    let dst = cfg.build();
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=naina");

    // A static naina still needs ONNX Runtime and the C++ runtime at link time.
    if let Ok(ort) = env::var("ONNXRUNTIME_ROOT") {
        println!("cargo:rustc-link-search=native={ort}/lib");
    }
    println!("cargo:rustc-link-lib=dylib=onnxruntime");
    link_cxx_runtime();
}

fn emit_rpath(dir: &str) {
    // So integration tests run without the caller exporting DYLD/LD paths.
    // Both Mach-O and ELF take the same -rpath spelling here; Windows has no
    // equivalent and resolves by PATH instead.
    if cfg!(any(target_os = "macos", target_os = "linux")) {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    }
}

fn link_cxx_runtime() {
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=dylib=c++");
    } else if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    }
}
