use std::{
    env,
    fs,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

// ============================================================================
// Configuration
// ============================================================================

const CPP_ENGINE_DIR: &str = "cpp_engine";
const CPP_HEADER: &str = "cpp_engine/include/ffi_bridge.h";
const CMAKE_NATIVE_TARGET: &str = "fin_ocr_native";

// ============================================================================
// Environment helpers
// ============================================================================

fn env_bool(name: &str, default: bool) -> bool {
    match env::var(name) {
        Ok(value) => match value.trim().to_ascii_lowercase().as_str() {
            "1" | "true" | "on" | "yes" => true,
            "0" | "false" | "off" | "no" => false,
            other => panic!("[FinOCR] Invalid boolean value for {name}: {other}"),
        },
        Err(_) => default,
    }
}

fn cmake_bool(value: bool) -> &'static str {
    if value {
        "ON"
    } else {
        "OFF"
    }
}

// ============================================================================
// Command helpers
// ============================================================================

fn run_command(command: &mut Command, description: &str) {
    let status = command.status().unwrap_or_else(|error| {
        panic!("[FinOCR] Failed to execute {description}: {error}")
    });

    if !status.success() {
        panic!("[FinOCR] {description} failed with status: {status}");
    }
}

fn command_exists(command: &str) -> bool {
    Command::new(command)
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|status| status.success())
        .unwrap_or(false)
}

// ============================================================================
// Recursive file search
// ============================================================================

fn find_file(root: &Path, filename: &str) -> Option<PathBuf> {
    let entries = fs::read_dir(root).ok()?;

    for entry in entries.flatten() {
        let path = entry.path();

        if path.is_file() {
            if path
                .file_name()
                .and_then(|name| name.to_str())
                == Some(filename)
            {
                return Some(path);
            }
        } else if path.is_dir() {
            if let Some(found) = find_file(&path, filename) {
                return Some(found);
            }
        }
    }

    None
}

// ============================================================================
// Find CMake-generated native library (Target OS aware)
// ============================================================================

fn find_native_library(
    cmake_build_dir: &Path,
    shared: bool,
    target_os: &str,
) -> PathBuf {
    let candidates: &[&str] = if target_os == "windows" {
        if shared {
            &["fin_ocr_native.lib", "fin_ocr_native.dll"]
        } else {
            &["fin_ocr_native.lib"]
        }
    } else if target_os == "macos" {
        if shared {
            &["libfin_ocr_native.dylib"]
        } else {
            &["libfin_ocr_native.a"]
        }
    } else {
        if shared {
            &["libfin_ocr_native.so"]
        } else {
            &["libfin_ocr_native.a"]
        }
    };

    for candidate in candidates {
        if let Some(path) = find_file(cmake_build_dir, candidate) {
            return path;
        }
    }

    panic!(
        "[FinOCR] Could not find '{CMAKE_NATIVE_TARGET}' under:\n{}",
        cmake_build_dir.display()
    );
}

// ============================================================================
// PDFium discovery (Target OS aware)
// ============================================================================

fn find_pdfium_library(root: &Path, target_os: &str) -> Option<PathBuf> {
    let candidates: &[&str] = if target_os == "windows" {
        &["pdfium.lib", "libpdfium.lib", "pdfium.dll"]
    } else if target_os == "macos" {
        &["libpdfium.dylib", "libpdfium.a"]
    } else {
        &["libpdfium.so", "libpdfium.a"]
    };

    for candidate in candidates {
        if let Some(path) = find_file(root, candidate) {
            return Some(path);
        }
    }

    None
}

// ============================================================================
// Emit cargo native link directives
// ============================================================================

fn setup_native_link(shared: bool, target_os: &str) {
    if shared {
        println!("cargo:rustc-link-lib=dylib=fin_ocr_native");
    } else if target_os == "linux" {
        // Force the linker to keep all C/C++ symbols from the static library
        println!("cargo:rustc-link-lib=static:+whole-archive=fin_ocr_native");
    } else {
        println!("cargo:rustc-link-lib=static=fin_ocr_native");
    }
}

// ============================================================================
// Main
// ============================================================================

fn main() {
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    let manifest_dir = PathBuf::from(
        env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR is not set"),
    );
    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR is not set"));

    let cpp_engine_dir = manifest_dir.join(CPP_ENGINE_DIR);
    let cmake_build_dir = out_dir.join("fin_ocr_cmake");

    fs::create_dir_all(&cmake_build_dir)
        .expect("[FinOCR] Failed to create CMake build directory");

    // Rebuild triggers - Source files
    println!("cargo:rerun-if-changed={}", cpp_engine_dir.join("CMakeLists.txt").display());
    println!("cargo:rerun-if-changed={}", manifest_dir.join(CPP_HEADER).display());
    println!("cargo:rerun-if-changed={}", manifest_dir.join("cpp_engine/src/ffi_bridge.cpp").display());
    println!("cargo:rerun-if-changed={}", manifest_dir.join("cpp_engine/src/ocr_pipeline.cpp").display());

    // Rebuild triggers - Environment variables
    println!("cargo:rerun-if-env-changed=PDFIUM_ROOT_DIR");
    println!("cargo:rerun-if-env-changed=FIN_BUILD_SHARED");
    println!("cargo:rerun-if-env-changed=FIN_ENABLE_NATIVE");
    println!("cargo:rerun-if-env-changed=FIN_ENABLE_AVX2");
    println!("cargo:rerun-if-env-changed=FIN_ENABLE_AVX512");
    println!("cargo:rerun-if-env-changed=FIN_ENABLE_LTO");
    println!("cargo:rerun-if-env-changed=FIN_ENABLE_TESSERACT");
    println!("cargo:rerun-if-env-changed=FIN_WARNINGS_AS_ERRORS");
    println!("cargo:rerun-if-env-changed=NUM_JOBS");

    // Options
    let fin_build_shared = env_bool("FIN_BUILD_SHARED", false);
    let fin_enable_native = env_bool("FIN_ENABLE_NATIVE", true);
    let fin_enable_avx2 = env_bool("FIN_ENABLE_AVX2", true);
    let fin_enable_avx512 = env_bool("FIN_ENABLE_AVX512", true);
    let fin_enable_lto = env_bool("FIN_ENABLE_LTO", false);
    let fin_enable_tesseract = env_bool("FIN_ENABLE_TESSERACT", true);
    let fin_warnings_as_errors = env_bool("FIN_WARNINGS_AS_ERRORS", false);

    // CMake Configure
    let mut configure = Command::new("cmake");
    configure
        .arg("-S").arg(&cpp_engine_dir)
        .arg("-B").arg(&cmake_build_dir)
        .arg("-DCMAKE_BUILD_TYPE=Release")
        .arg(format!("-DFIN_BUILD_SHARED={}", cmake_bool(fin_build_shared)))
        .arg(format!("-DFIN_ENABLE_NATIVE={}", cmake_bool(fin_enable_native)))
        .arg(format!("-DFIN_ENABLE_AVX2={}", cmake_bool(fin_enable_avx2)))
        .arg(format!("-DFIN_ENABLE_AVX512={}", cmake_bool(fin_enable_avx512)))
        .arg(format!("-DFIN_ENABLE_LTO={}", cmake_bool(fin_enable_lto)))
        .arg(format!("-DFIN_ENABLE_TESSERACT={}", cmake_bool(fin_enable_tesseract)))
        .arg(format!("-DFIN_WARNINGS_AS_ERRORS={}", cmake_bool(fin_warnings_as_errors)));

    if let Ok(pdfium_root) = env::var("PDFIUM_ROOT_DIR") {
        configure.arg(format!("-DPDFIUM_ROOT_DIR={pdfium_root}"));
    }

    if command_exists("ninja") {
        configure.arg("-G").arg("Ninja");
    }

    run_command(&mut configure, "CMake configuration");

    // CMake Build
    let mut build = Command::new("cmake");
    build
        .arg("--build").arg(&cmake_build_dir)
        .arg("--target").arg(CMAKE_NATIVE_TARGET)
        .arg("--config").arg("Release");

    if let Ok(num_jobs) = env::var("NUM_JOBS") {
        if let Ok(jobs) = num_jobs.parse::<usize>() {
            build.arg("--parallel").arg(jobs.to_string());
        }
    }

    run_command(&mut build, "CMake native compilation");

    // Locate Built Native Library
    let native_library = find_native_library(&cmake_build_dir, fin_build_shared, &target_os);
    let native_library_dir = native_library.parent().expect("[FinOCR] Missing parent dir");

    // Emit search paths first
    println!("cargo:rustc-link-search=native={}", native_library_dir.display());
    println!("cargo:rustc-link-search=native={}", cmake_build_dir.display());
    println!("cargo:rustc-link-search=native={}", cmake_build_dir.join("lib").display());

    setup_native_link(fin_build_shared, &target_os);

    // Tesseract + Leptonica via pkg-config
    if fin_enable_tesseract {
        let mut cfg = pkg_config::Config::new();
        cfg.cargo_metadata(false);

        let tess_lib = cfg
            .probe("tesseract")
            .expect("[FinOCR] Failed to probe 'tesseract' via pkg-config");

        let lept_lib = cfg
            .probe("leptonica")
            .or_else(|_| cfg.probe("lept"))
            .expect("[FinOCR] Failed to probe 'leptonica' or 'lept' via pkg-config");

        for path in tess_lib.link_paths.iter().chain(lept_lib.link_paths.iter()) {
            println!("cargo:rustc-link-search=native={}", path.display());
        }

        println!("cargo:rustc-link-lib=dylib=tesseract");
        println!("cargo:rustc-link-lib=dylib=leptonica");
    }

    // PDFium Link Search
    let pdfium_library = if let Ok(pdfium_root) = env::var("PDFIUM_ROOT_DIR") {
        find_pdfium_library(&PathBuf::from(pdfium_root), &target_os)
    } else {
        find_pdfium_library(&cmake_build_dir, &target_os)
    };

    if let Some(pdfium_lib) = pdfium_library {
        if let Some(pdfium_dir) = pdfium_lib.parent() {
            println!("cargo:rustc-link-search=native={}", pdfium_dir.display());
        }
    }
    println!("cargo:rustc-link-lib=dylib=pdfium");

    // macOS System Search Paths
    if target_os == "macos" {
        println!("cargo:rustc-link-search=native=/opt/homebrew/lib");
        println!("cargo:rustc-link-search=native=/usr/local/lib");
    }

    // Standard C++ Runtime
    match target_os.as_str() {
        "linux" => println!("cargo:rustc-link-lib=dylib=stdc++"),
        "macos" => println!("cargo:rustc-link-lib=dylib=c++"),
        _ => {}
    }

    // Bindgen
    let include_dir = cpp_engine_dir.join("include");
    let mut builder = bindgen::Builder::default()
        .header(manifest_dir.join(CPP_HEADER).to_string_lossy().into_owned())
        .clang_arg(format!("-I{}", include_dir.display()))
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("fin_.*")
        .allowlist_type("Fin.*")
        .size_t_is_usize(true);

    if target_os == "macos" {
        builder = builder
            .clang_arg("-I/opt/homebrew/include")
            .clang_arg("-I/usr/local/include");
    }

    let bindings = builder
        .generate()
        .expect("[FinOCR] Unable to generate C++ FFI bindings");

    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("[FinOCR] Failed to write bindings.rs");
}
