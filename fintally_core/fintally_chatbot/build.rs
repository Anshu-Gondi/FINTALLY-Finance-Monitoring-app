use std::env;
use std::path::PathBuf;

fn main() {
    let cpp_header = "cpp_engine/include/ffi_bridge.h";

    // 1. Initialize C++ compilation pipeline
    let mut build = cc::Build::new();

    build
        .cpp(true)
        .std("c++17")
        .pic(true) // Required for relocatable static library linking into Rust
        .warnings(true)
        .extra_warnings(false)
        .file("cpp_engine/src/ffi_bridge.cpp")
        .file("cpp_engine/src/ocr_pipeline.cpp")
        .file("cpp_engine/src/thermal_sensor.cpp") // <--- ADD THIS LINE
        .include("cpp_engine/include");

    // Configure compiler flags based on target OS & architecture
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap_or_default();

    if build.get_compiler().is_like_msvc() {
        build
            .flag("/O2")
            .flag("/fp:fast")
            .flag("/permissive-");

        #[cfg(target_feature = "avx2")]
        build.flag("/arch:AVX2");
    } else {
        // GCC / Clang
        build
            .flag_if_supported("-O3")
            .flag_if_supported("-ffast-math")
            .flag_if_supported("-fvisibility=hidden");

        // Hardware Vectorization Flags
        if target_arch == "x86_64" {
            build.flag_if_supported("-msse4.2");
            build.flag_if_supported("-mavx2");
        }
    }

    // Compile static library libfin_ocr_native.a / fin_ocr_native.lib
    build.compile("fin_ocr_native");

    // 2. Explicitly link the underlying C++ Standard Library
    if target_os == "linux" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    } else if target_os == "macos" {
        println!("cargo:rustc-link-lib=dylib=c++");
    }

    // 3. Recompilation Watch Triggers
    println!("cargo:rerun-if-changed={}", cpp_header);
    println!("cargo:rerun-if-changed=cpp_engine/include/tensor_ops.hpp");
    println!("cargo:rerun-if-changed=cpp_engine/include/thermal_sensor.hpp"); // Recommended
    println!("cargo:rerun-if-changed=cpp_engine/src/ffi_bridge.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/ocr_pipeline.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/thermal_sensor.cpp");  // <--- ADD THIS LINE

    // 4. Automated FFI Binding Generation
    let bindings = bindgen::Builder::default()
        .header(cpp_header)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("fin_.*")
        .allowlist_type("Fin.*")
        .size_t_is_usize(true)
        .generate()
        .expect("Unable to generate C++ FFI bindings via bindgen");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindgen FFI output!");
}
