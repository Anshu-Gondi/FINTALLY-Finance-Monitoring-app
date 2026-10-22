//! Build the existing FinTally Voice C23 core and the offline-builder C ABI.
//!
//! CMake remains the only build system for c_core. cc-rs compiles only the
//! narrow bridge in ffi/; this avoids compiling the DSP sources twice with
//! potentially different flags, source lists, or SIMD definitions.
//!
//! The existing c_core CMake project does not declare install() rules in the
//! CMakeLists that defines fintally_voice_core. Therefore use cmake-rs's
//! build_target() explicitly instead of its default "install" target, then
//! locate the produced archive under OUT_DIR/build.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

const CORE_TARGET: &str = "fintally_voice_core";
const BRIDGE_LIBRARY_NAME: &str = "fin_voice_builder_ffi";

fn main() {
    let manifest_dir = PathBuf::from(required_env("CARGO_MANIFEST_DIR"));
    let voice_root = manifest_dir
        .parent()
        .expect("offline_builder must be a direct child of fintally_voice");
    let c_core_dir = voice_root.join("c_core");
    let include_dir = c_core_dir.join("include");
    let ffi_dir = manifest_dir.join("ffi");
    let bridge_header = ffi_dir.join("fin_voice_builder_ffi.h");
    let bridge_source = ffi_dir.join("fin_voice_builder_ffi.c");
    let bindgen_wrapper = ffi_dir.join("wrapper.h");
    let out_dir = PathBuf::from(required_env("OUT_DIR"));
    let target = required_env("TARGET");
    let host = required_env("HOST");
    let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_owned());
    let is_msvc = target.contains("msvc");

    for path in [
        c_core_dir.join("CMakeLists.txt"),
        include_dir.join("acoustic_dsp.h"),
        bridge_header,
        bridge_source,
        bindgen_wrapper,
    ] {
        ensure_file(&path);
    }

    println!(
        "cargo:rerun-if-changed={}",
        manifest_dir.join("build.rs").display()
    );

    emit_rerun_tree(&c_core_dir);

    // Watch the directory itself so Cargo notices generated C sources
    // created after this build script has run.
    println!(
        "cargo:rerun-if-changed={}",
        c_core_dir.join("src").display()
    );

    emit_rerun_tree(&ffi_dir);

    for key in [
        "CC",
        "CFLAGS",
        "CMAKE",
        "CMAKE_GENERATOR",
        "CMAKE_TOOLCHAIN_FILE",
        "CMAKE_ARGS",
        "BINDGEN_EXTRA_CLANG_ARGS",
        "PROFILE",
        "HOST",
        "TARGET",
    ] {
        println!("cargo:rerun-if-env-changed={key}");
    }
    // bindgen/libclang consumes this variable on cross builds when supplied.
    println!("cargo:rerun-if-env-changed=BINDGEN_EXTRA_CLANG_ARGS_{target}");

    // Compile the canonical C core. In this crate, CMake must not enable IPO/LTO:
    // we want native code that is easy to inspect and debug across the FFI edge.
    let mut cmake_config = cmake::Config::new(&c_core_dir);
    cmake_config
        .profile(if profile == "release" {
            "Release"
        } else {
            "Debug"
        })
        .build_target(CORE_TARGET)
        .define("CMAKE_C_STANDARD", "23")
        .define("CMAKE_C_STANDARD_REQUIRED", "ON")
        .define("CMAKE_C_EXTENSIONS", "OFF")
        .define("CMAKE_POSITION_INDEPENDENT_CODE", "ON")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL", "OFF")
        .define("FIN_VOICE_ENABLE_SIMD_DISPATCH", "ON")
        .define("FIN_VOICE_BUILD_PLATFORM_COMPONENTS", "ON")
        .define("FIN_VOICE_BUILD_RUNTIME_COMPONENTS", "OFF")
        .define("FIN_VOICE_BUILD_TESTS", "OFF")
        .define("FIN_VOICE_BUILD_BENCHMARKS", "OFF")
        .define("FIN_VOICE_WARNINGS_AS_ERRORS", "ON");

    if !is_msvc {
        cmake_config.cflag("-fno-lto");
    }

    // cmake-rs returns OUT_DIR. The archive is in OUT_DIR/build (possibly in a
    // configuration subdirectory with multi-config generators); find it rather
    // than hard-coding Makefiles/Ninja/Visual Studio layout assumptions.
    let cmake_out = cmake_config.build();
    let core_archive = find_core_archive(&cmake_out.join("build"), &target);
    let core_lib_dir = core_archive
        .parent()
        .expect("located native archive has no parent directory");
    println!(
        "cargo:warning=FinTally Voice C core: {}",
        core_archive.display()
    );
    println!("cargo:rustc-link-search=native={}", core_lib_dir.display());

    // Compile only the tiny C ABI bridge via cc-rs. It includes acoustic_dsp.h
    // and calls the public DSP API; the linked static core provides the DSP
    // implementation plus MFCC/LPC/pitch/energy/SIMD symbols it depends on.
    let mut bridge_build = cc::Build::new();
    bridge_build
        .file(ffi_dir.join("fin_voice_builder_ffi.c"))
        .include(&include_dir)
        .include(&ffi_dir)
        .warnings(true)
        .extra_warnings(true)
        .warnings_into_errors(true)
        .flag_if_supported("-Wpedantic");

    if !is_msvc {
        bridge_build.std("c23").flag_if_supported("-fno-lto");
    }

    bridge_build.compile(BRIDGE_LIBRARY_NAME);

    // This ordering keeps the static bridge before the static DSP archive.
    println!("cargo:rustc-link-lib=static={CORE_TARGET}");
    if env::var("CARGO_CFG_TARGET_FAMILY")
        .unwrap_or_default()
        .split(',')
        .any(|family| family == "unix")
    {
        println!("cargo:rustc-link-lib=m");
    }

    generate_bindings(
        &ffi_dir.join("wrapper.h"),
        &ffi_dir,
        &out_dir,
        &target,
        &host,
    );
}

fn required_env(name: &str) -> String {
    env::var(name)
        .unwrap_or_else(|_| panic!("Cargo did not set required environment variable {name}"))
}

fn generate_bindings(wrapper: &Path, ffi_dir: &Path, out_dir: &Path, target: &str, host: &str) {
    let mut builder = bindgen::Builder::default()
        .header(wrapper.to_string_lossy().into_owned())
        .clang_arg(format!("-I{}", ffi_dir.display()))
        .allowlist_function("^fin_voice_builder_dsp_.*$")
        .allowlist_type("^FinVoiceBuilderDsp$")
        .allowlist_var("^FIN_VOICE_BUILDER_.*$")
        .opaque_type("FinVoiceBuilderDsp")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .layout_tests(false)
        .derive_debug(false);

    if target != host {
        builder = builder.clang_arg(format!("--target={target}"));
    }

    let bindings = builder.generate().unwrap_or_else(|error| {
        panic!(
            "bindgen failed to parse {}. Install a compatible libclang. For cross builds, configure the target sysroot/include arguments with BINDGEN_EXTRA_CLANG_ARGS. Error: {error}",
            wrapper.display()
        )
    });
    let destination = out_dir.join("fin_voice_builder_bindings.rs");
    bindings
        .write_to_file(&destination)
        .unwrap_or_else(|error| panic!("could not write {}: {error}", destination.display()));
}

fn ensure_file(path: &Path) {
    if !path.is_file() {
        panic!(
            "required FinTally Voice build input is missing: {}",
            path.display()
        );
    }
}

fn find_core_archive(build_root: &Path, target: &str) -> PathBuf {
    if !build_root.is_dir() {
        panic!(
            "CMake build directory does not exist: {}",
            build_root.display()
        );
    }

    let expected = if target.contains("msvc") {
        "fintally_voice_core.lib"
    } else {
        "libfintally_voice_core.a"
    };
    let mut pending = vec![build_root.to_path_buf()];
    let mut candidates = Vec::new();

    while let Some(directory) = pending.pop() {
        let entries = fs::read_dir(&directory)
            .unwrap_or_else(|error| panic!("cannot inspect {}: {error}", directory.display()));
        for entry in entries {
            let entry = entry.unwrap_or_else(|error| {
                panic!(
                    "cannot inspect CMake output beneath {}: {error}",
                    directory.display()
                )
            });
            let path = entry.path();
            let kind = entry
                .file_type()
                .unwrap_or_else(|error| panic!("cannot inspect {}: {error}", path.display()));
            if kind.is_dir() {
                pending.push(path);
            } else if kind.is_file()
                && path.file_name().and_then(|name| name.to_str()) == Some(expected)
            {
                candidates.push(path);
            }
        }
    }

    candidates.sort();
    candidates.dedup();
    match candidates.as_slice() {
        [only] => only.clone(),
        [] => panic!(
            "CMake built target {CORE_TARGET}, but {expected} was not found under {}. Inspect the CMake target output name/location.",
            build_root.display()
        ),
        many => panic!(
            "found multiple {expected} archives under {}: {many:?}. Clean the offline_builder Cargo output and rebuild to avoid linking an ambiguous configuration.",
            build_root.display()
        ),
    }
}

fn emit_rerun_tree(root: &Path) {
    let mut pending = vec![root.to_path_buf()];
    while let Some(directory) = pending.pop() {
        let entries = fs::read_dir(&directory)
            .unwrap_or_else(|error| panic!("cannot read {}: {error}", directory.display()));
        for entry in entries {
            let entry = entry.unwrap_or_else(|error| {
                panic!(
                    "cannot inspect build input under {}: {error}",
                    directory.display()
                )
            });
            let path = entry.path();
            if path.is_dir() {
                let name = path
                    .file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or_default();
                if name == ".git"
                    || name == "target"
                    || name == "build"
                    || name.starts_with("cmake-build-")
                    || name == "data"
                    || name == "data_raw"
                {
                    continue;
                }
                pending.push(path);
            } else if path.is_file() {
                println!("cargo:rerun-if-changed={}", path.display());
            }
        }
    }
}
