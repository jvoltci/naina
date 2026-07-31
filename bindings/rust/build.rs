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

use std::{
    env, fs,
    path::{Path, PathBuf},
};

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

    // Nothing here restates what the core needs. CMake writes its resolved link
    // line to this file and we read it back — see the long comment in
    // cmake/NainaRustLinkManifest.cmake. A static libnaina.a carries no
    // dependency metadata, so the hand-written list this replaced named ONNX
    // Runtime (behind an env var CI never set) and neither yaml-cpp nor libcurl.
    // `cargo build` never noticed: an rlib is not linked, so only `cargo test`
    // ever issued the link that failed.
    let manifest = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR")).join("naina-link.txt");

    // OUT_DIR survives between cargo invocations, so delete any manifest an
    // earlier configure left behind. Without this a core that has stopped
    // emitting one links happily against the previous answer, which is the
    // stale-cache version of the bug this whole file exists to fix. Found by
    // deleting the generator and watching the build succeed anyway.
    let _ = fs::remove_file(&manifest);

    let mut cfg = cmake::Config::new(&vendor);
    cfg.define("NAINA_BUILD_RUST", "ON")
        .define("NAINA_BUILD_TESTS", "OFF")
        .define("NAINA_BUILD_SHARED", "OFF")
        .define("NAINA_VENDOR_YAMLCPP", "ON")
        .define("NAINA_WITH_ONNXRUNTIME", "ON")
        .define("NAINA_RELEASE", "ON")
        .define("NAINA_RUST_LINK_MANIFEST", &manifest)
        .profile("Release");

    cfg.build();
    emit_manifest(&manifest);
    link_cxx_runtime();
}

/// Turn CMake's resolved link line into cargo directives.
///
/// Every unrecognised line is a panic rather than a skip: a link input dropped
/// here reappears hundreds of lines later as an undefined symbol, and a backend
/// whose static registrar never linked fails by reading nothing at all.
fn emit_manifest(manifest: &Path) {
    // Cargo replays a cached build script rather than re-running it, so it has to
    // be told what this one reads. The rerun-if-env-changed lines above already
    // switched off the default "re-run if any file in the package changed" rule,
    // and without these two an edit to the vendored core went unnoticed until
    // build.rs itself was touched: a stale link line that looks like a working
    // fix, which is the failure mode this file was in to begin with.
    println!("cargo:rerun-if-changed=vendor");
    println!("cargo:rerun-if-changed={}", manifest.display());

    let text = fs::read_to_string(manifest).unwrap_or_else(|e| {
        panic!(
            "naina: CMake wrote no link manifest at {} ({e}).\n\
             The build configures -DNAINA_RUST_LINK_MANIFEST; if that reached an \
             older vendored core, re-run bindings/rust/vendor.sh.",
            manifest.display()
        )
    });

    let mut rpaths: Vec<String> = Vec::new();
    let mut seen: Vec<&str> = Vec::new();

    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (key, value) = line
            .split_once('=')
            .unwrap_or_else(|| panic!("naina: malformed link manifest line: {line}"));

        match key {
            "lib" => {
                // CMake dedupes what it can, but it dedupes generator
                // expressions, and two different ones can resolve to one file:
                // FindCURL names libcurl both as an imported location and as a
                // plain path, which produced -lcurl twice.
                if seen.contains(&value) {
                    continue;
                }
                seen.push(value);
                if let Some(dir) = link_file(Path::new(value)) {
                    if !rpaths.contains(&dir) {
                        rpaths.push(dir);
                    }
                }
            }
            "name" => println!("cargo:rustc-link-lib={value}"),
            "framework" => println!("cargo:rustc-link-lib=framework={value}"),
            "flag" => println!("cargo:rustc-link-arg={value}"),
            _ => panic!(
                "naina: unknown key {key:?} in {} — line: {line}",
                manifest.display()
            ),
        }
    }

    // libnaina.a itself is in there, so an empty list means the walk found
    // nothing and the link would fail with no clue why.
    assert!(
        !seen.is_empty(),
        "naina: link manifest {} names no libraries",
        manifest.display()
    );

    for dir in rpaths {
        emit_rpath(&dir);
    }
}

/// `-L` the directory and `-l` the library.
///
/// Returns the directory when the file is a shared library, since those must
/// also be findable at run time.
///
/// The link name is derived the way a linker spells it: drop the `lib` prefix
/// and everything from the first dot, so `libonnxruntime.1.26.0.dylib` (macOS,
/// Homebrew) and `libonnxruntime.so.1.20.1` (Linux, upstream tarball) both give
/// `onnxruntime`. Both packages ship the unversioned symlink that `-l` then
/// needs; CMake's find_library resolves to that symlink in the first place.
fn link_file(path: &Path) -> Option<String> {
    let dir = path
        .parent()
        .unwrap_or_else(|| panic!("naina: link input has no directory: {}", path.display()));
    let file = path
        .file_name()
        .and_then(|f| f.to_str())
        .unwrap_or_else(|| panic!("naina: link input has no file name: {}", path.display()));

    let stem = file.strip_prefix("lib").unwrap_or(file);
    let name = stem
        .split('.')
        .next()
        .filter(|n| !n.is_empty())
        .unwrap_or_else(|| panic!("naina: cannot derive a link name from {file}"));

    let is_static = file.ends_with(".a") || file.ends_with(".lib");
    let kind = if is_static { "static" } else { "dylib" };

    println!("cargo:rustc-link-search=native={}", dir.display());
    println!("cargo:rustc-link-lib={kind}={name}");

    // A .tbd is an Apple SDK stub for a library that lives in the dyld shared
    // cache — there is nothing at that path to find at run time, so an rpath to
    // it would just be a nonsense entry baked into the binary.
    let needs_rpath = !is_static && !file.ends_with(".tbd");
    needs_rpath.then(|| dir.display().to_string())
}

fn emit_rpath(dir: &str) {
    // So integration tests run without the caller exporting DYLD/LD paths. On
    // path 2 that means the directory of every shared dependency: an ONNX
    // Runtime unpacked from upstream's macOS tarball has an @rpath install name,
    // and without this the crate links but every read skips for want of a
    // backend — the failure mode this project keeps getting bitten by.
    //
    // Both Mach-O and ELF take the same -rpath spelling here; Windows has no
    // equivalent and resolves by PATH instead. This only reaches our own test
    // binaries: rustc-link-arg does not propagate to a dependent's link the way
    // rustc-link-lib does.
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
