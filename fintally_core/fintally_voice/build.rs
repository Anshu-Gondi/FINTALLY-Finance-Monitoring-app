//! FinTally Voice native build integration.
//!
//! Responsibilities:
//! - build the C23 core exactly once through its existing CMake project;
//! - keep LTO / CMake IPO disabled by default;
//! - compile only the small ABI-report shim through `cc`;
//! - generate Rust declarations from the public C headers using bindgen;
//! - explicitly link the installed static archive and its system dependencies.
//!
//! The C core sources are intentionally NOT compiled again through `cc`.

use std::env;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

const CORE_LIBRARY_BASENAME: &str = "fintally_voice_core";
const BINDINGS_FILE_NAME: &str = "fin_voice_bindings.rs";

fn cmake_bool(value: bool) -> &'static str {
    if value {
        "ON"
    } else {
        "OFF"
    }
}

fn env_bool(name: &str, default: bool) -> bool {
    match env::var(name) {
        Ok(value) => match value.trim().to_ascii_lowercase().as_str() {
            "1" | "true" | "yes" | "on" => true,
            "0" | "false" | "no" | "off" | "" => false,
            other => {
                panic!("Invalid boolean value for {name}={other:?}; use ON/OFF, true/false, or 1/0")
            }
        },
        Err(env::VarError::NotPresent) => default,
        Err(error) => panic!("Could not read {name}: {error}"),
    }
}

fn emit_environment_watchers() {
    for name in [
        "TARGET",
        "HOST",
        "CC",
        "CFLAGS",
        "AR",
        "RANLIB",
        "CMAKE_GENERATOR",
        "CMAKE_PREFIX_PATH",
        "FIN_VOICE_CMAKE_TOOLCHAIN_FILE",
        "FIN_VOICE_ENABLE_SIMD_DISPATCH",
        "FIN_VOICE_BUILD_PLATFORM_COMPONENTS",
        "FIN_VOICE_ENABLE_THREADS",
        "FIN_VOICE_ENABLE_ESP_DSP",
        "FIN_VOICE_ESP_DSP_ROOT",
        "FIN_VOICE_ESP_DSP_LIBRARY",
        "FIN_VOICE_ESP_DSP_INCLUDE_DIR",
        "LIBCLANG_PATH",
    ] {
        println!("cargo:rerun-if-env-changed={name}");
    }
}

/// Ask Cargo to rerun this script when a source/header/CMake input changes.
/// Watching both directories and relevant files also notices newly added modules.
fn watch_native_tree(root: &Path) -> io::Result<()> {
    println!("cargo:rerun-if-changed={}", root.display());

    let mut pending = vec![root.to_path_buf()];
    while let Some(directory) = pending.pop() {
        println!("cargo:rerun-if-changed={}", directory.display());
        for entry in fs::read_dir(&directory)? {
            let entry = entry?;
            let path = entry.path();
            let file_type = entry.file_type()?;

            if file_type.is_dir() {
                let name = entry.file_name();
                if name == ".git" || name == "target" || name == "build" {
                    continue;
                }
                pending.push(path);
                continue;
            }

            if !file_type.is_file() {
                continue;
            }

            let name = entry.file_name();
            let name = name.to_string_lossy();
            let extension = path
                .extension()
                .and_then(|value| value.to_str())
                .unwrap_or("");
            if matches!(extension, "c" | "h" | "cmake")
                || name == "CMakeLists.txt"
                || name == "CMakePresets.json"
            {
                println!("cargo:rerun-if-changed={}", path.display());
            }
        }
    }
    Ok(())
}

fn find_static_archive(install_prefix: &Path, target_env: &str) -> io::Result<PathBuf> {
    let archive_names: &[&str] = if target_env == "msvc" {
        &["fintally_voice_core.lib", "libfintally_voice_core.lib"]
    } else {
        &["libfintally_voice_core.a", "fintally_voice_core.a"]
    };

    // Prefer the CMake install tree. This keeps the Rust link step aligned with
    // the target's install rules instead of relying on CMake's private build tree.
    for directory in ["lib", "lib64", "Lib", "bin"] {
        let candidate_directory = install_prefix.join(directory);
        for archive_name in archive_names {
            let candidate = candidate_directory.join(archive_name);
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }

    // Some generators/platforms install archives in a different subdirectory.
    // Fall back to searching the CMake prefix, but accept only the exact target.
    let mut pending = vec![install_prefix.to_path_buf()];
    while let Some(directory) = pending.pop() {
        for entry in fs::read_dir(&directory)? {
            let entry = entry?;
            let path = entry.path();
            let file_type = entry.file_type()?;
            if file_type.is_dir() {
                pending.push(path);
            } else if file_type.is_file()
                && archive_names
                    .iter()
                    .any(|name| path.file_name().and_then(|value| value.to_str()) == Some(*name))
            {
                return Ok(path);
            }
        }
    }

    Err(io::Error::new(
        io::ErrorKind::NotFound,
        format!(
            "CMake completed, but the static archive for target {CORE_LIBRARY_BASENAME:?} was not found under {}. Check the install(TARGETS ...) rules in c_core/CMakeLists.txt.",
            install_prefix.display()
        ),
    ))
}

fn write_bindgen_wrapper(include_dir: &Path, ffi_dir: &Path, output: &Path) -> io::Result<()> {
    // These are the public, caller-facing headers shown in the project's C-core
    // layout. Optional generated codebook headers are included only when present.
    let required_headers = [
        "acoustic_dsp.h",
        "energy_zcr.h",
        "event_queue.h",
        "language_fsm.h",
        "lpc_formants.h",
        "mfcc.h",
        "pitch_f0.h",
        "platform_profile.h",
        "spsc_ring.h",
        "text_builder.h",
        "thermal_policy.h",
        "voice_pipeline.h",
        "vq_quantize.h",
        "kernel/simd_dispatch.h",
    ];

    let optional_headers = [
        "codebooks/fin_voice_codebook_data.h",
        "codebooks/codebook_en.h",
        "codebooks/codebook_fr.h",
        "codebooks/codebook_hi.h",
        "codebooks/codebook_zh.h",
    ];

    let mut wrapper = String::from(
        "/* Generated into Cargo OUT_DIR; do not edit. */\n\
         #include <stddef.h>\n\
         #include <stdint.h>\n",
    );

    for header in required_headers {
        let path = include_dir.join(header);
        if !path.is_file() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("Required public C header is missing: {}", path.display()),
            ));
        }
        wrapper.push_str(&format!("#include \"{header}\"\n"));
    }

    for header in optional_headers {
        if include_dir.join(header).is_file() {
            wrapper.push_str(&format!("#include \"{header}\"\n"));
        }
    }

    let shim_header = ffi_dir.join("fin_voice_ffi_shim.h");
    if !shim_header.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("FFI shim header is missing: {}", shim_header.display()),
        ));
    }
    wrapper.push_str("#include \"fin_voice_ffi_shim.h\"\n");

    fs::write(output, wrapper)
}

fn main() {
    emit_environment_watchers();

    let manifest_dir = PathBuf::from(
        env::var_os("CARGO_MANIFEST_DIR").expect("Cargo did not set CARGO_MANIFEST_DIR"),
    );
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo did not set OUT_DIR"));
    let target = env::var("TARGET").expect("Cargo did not set TARGET");
    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    let target_family = env::var("CARGO_CFG_TARGET_FAMILY").unwrap_or_default();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let core_dir = manifest_dir.join("c_core");
    let include_dir = core_dir.join("include");
    let ffi_dir = core_dir.join("ffi");
    let cmake_file = core_dir.join("CMakeLists.txt");
    let shim_source = ffi_dir.join("fin_voice_ffi_shim.c");
    let shim_header = ffi_dir.join("fin_voice_ffi_shim.h");

    for required in [&cmake_file, &include_dir, &shim_source, &shim_header] {
        assert!(
            required.exists(),
            "Required FinTally Voice native-build input is missing: {}",
            required.display()
        );
    }
    watch_native_tree(&core_dir).expect("Could not register C-core rebuild inputs");

    let enable_simd = env_bool("FIN_VOICE_ENABLE_SIMD_DISPATCH", true);
    let enable_platform = env_bool("FIN_VOICE_BUILD_PLATFORM_COMPONENTS", true);
    let enable_threads = env_bool("FIN_VOICE_ENABLE_THREADS", false);
    let enable_esp_dsp = env_bool("FIN_VOICE_ENABLE_ESP_DSP", false);

    // The currently supplied Dependencies.cmake does not define
    // fin_voice_configure_dependencies(), which src/CMakeLists.txt requires when
    // ESP-DSP is enabled. Preserve the known-good default until that integration
    // function is implemented.
    if enable_esp_dsp {
        println!(
            "cargo:warning=FIN_VOICE_ENABLE_ESP_DSP=ON was requested; the supplied Dependencies.cmake must define fin_voice_configure_dependencies() before this configuration can build."
        );
    }

    let mut config = cmake::Config::new(&core_dir);
    config
        .target(&target)
        .define("BUILD_SHARED_LIBS", "OFF")
        .define("CMAKE_POSITION_INDEPENDENT_CODE", "ON")
        .define("CMAKE_INSTALL_LIBDIR", "lib")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE", "OFF")
        .define("CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO", "OFF")
        .define("FIN_VOICE_WARNINGS_AS_ERRORS", "OFF")
        .define("FIN_VOICE_PEDANTIC_C23", "OFF")
        .define("FIN_VOICE_ENABLE_THREADS", cmake_bool(enable_threads))
        .define("FIN_VOICE_ENABLE_SIMD_DISPATCH", cmake_bool(enable_simd))
        .define(
            "FIN_VOICE_BUILD_PLATFORM_COMPONENTS",
            cmake_bool(enable_platform),
        )
        .define("FIN_VOICE_ENABLE_ESP_DSP", cmake_bool(enable_esp_dsp))
        // Native C sanitizers are kept off in the Cargo-linked library by
        // default. Run CMake's standalone tests with sanitizer options instead,
        // so the final executable's sanitizer runtime is configured deliberately.
        .define("FIN_VOICE_BUILD_TESTS", "OFF")
        .define("FIN_VOICE_BUILD_BENCHMARKS", "OFF")
        .define("FIN_VOICE_ENABLE_ASAN_UBSAN", "OFF")
        .define("FIN_VOICE_ENABLE_TSAN", "OFF")
        .define("FIN_VOICE_SANITIZE_ADDRESS", "OFF")
        .define("FIN_VOICE_SANITIZE_UNDEFINED", "OFF")
        .define("FIN_VOICE_SANITIZE_THREAD", "OFF");

    if let Ok(toolchain_file) = env::var("FIN_VOICE_CMAKE_TOOLCHAIN_FILE") {
        config.define("CMAKE_TOOLCHAIN_FILE", toolchain_file);
    }
    for (environment_name, cmake_name) in [
        ("FIN_VOICE_ESP_DSP_ROOT", "FIN_VOICE_ESP_DSP_ROOT"),
        ("FIN_VOICE_ESP_DSP_LIBRARY", "FIN_VOICE_ESP_DSP_LIBRARY"),
        (
            "FIN_VOICE_ESP_DSP_INCLUDE_DIR",
            "FIN_VOICE_ESP_DSP_INCLUDE_DIR",
        ),
    ] {
        if let Ok(value) = env::var(environment_name) {
            config.define(cmake_name, value);
        }
    }

    println!(
        "cargo:warning=Building FinTally Voice C core for target {target}; CMake IPO/LTO is OFF, SIMD dispatch is {}, platform components are {}, and native tests/benchmarks are OFF.",
        if enable_simd { "ON" } else { "OFF" },
        if enable_platform { "ON" } else { "OFF" },
    );

    let install_prefix = config.build();
    let archive = find_static_archive(&install_prefix, &target_env).unwrap_or_else(|error| {
        panic!("Could not locate the FinTally Voice static library: {error}")
    });

    // Compile only the FFI shim, not the C core itself. cc-rs emits link
    // metadata for the shim here; we emit the core archive afterwards, preserving
    // the static-link dependency order (shim first, core second).
    cc::Build::new()
        .file(&shim_source)
        .include(&include_dir)
        .include(&ffi_dir)
        .target(&target)
        .pic(true)
        .warnings(true)
        .warnings_into_errors(false)
        .compile("fin_voice_ffi_shim");

    println!(
        "cargo:rustc-link-search=native={}",
        archive.parent().unwrap().display()
    );
    println!("cargo:rustc-link-lib=static={CORE_LIBRARY_BASENAME}");

    // CMake's PUBLIC INTERFACE link dependencies are not automatically turned
    // into Cargo link metadata, so reproduce the small platform dependency set.
    if target_family.split(',').any(|family| family == "unix") {
        println!("cargo:rustc-link-lib=m");
    }
    if enable_threads
        && matches!(
            target_os.as_str(),
            "linux" | "freebsd" | "openbsd" | "netbsd"
        )
    {
        println!("cargo:rustc-link-lib=pthread");
    }

    let wrapper_path = out_dir.join("fin_voice_bindgen_wrapper.h");
    write_bindgen_wrapper(&include_dir, &ffi_dir, &wrapper_path)
        .unwrap_or_else(|error| panic!("Could not create the bindgen wrapper: {error}"));

    let bindings_builder = bindgen::Builder::default()
        .header(wrapper_path.display().to_string())
        .clang_arg(format!("-I{}", include_dir.display()))
        .clang_arg(format!("-I{}", ffi_dir.display()))
        .clang_arg("-std=c2x")
        .allowlist_function("^fin_.*")
        .allowlist_type("^Fin[A-Za-z0-9_]*$")
        .allowlist_var("^FIN_[A-Z0-9_]+$")
        .allowlist_recursively(true)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate_comments(true);

    let bindings = bindings_builder
        .generate()
        .unwrap_or_else(|error| {
            panic!(
                "bindgen could not parse the FinTally Voice public C headers. Install libclang and verify the C headers/compiler standard. Original error: {error}"
            )
        });

    bindings
        .write_to_file(out_dir.join(BINDINGS_FILE_NAME))
        .unwrap_or_else(|error| panic!("Could not write generated Rust bindings: {error}"));
}
