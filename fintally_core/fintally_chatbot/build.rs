use std::env;
use std::path::PathBuf;

fn main() {
    let cpp_header = "cpp_engine/include/ffi_bridge.h";

    // 1. Compile native C++ preprocessing engine
    cc::Build::new()
        .cpp(true)
        .flag_if_supported("-std=c++17")
        .flag_if_supported("-O3")
        .flag_if_supported("-fno-exceptions")
        .flag_if_supported("-fno-rtti")
        .file("cpp_engine/src/ffi_bridge.cpp")
        .file("cpp_engine/src/ocr_pipeline.cpp")
        .include("cpp_engine/include")
        .compile("fin_ocr_native");

    // Recompilation triggers
    println!("cargo:rerun-if-changed={}", cpp_header);
    println!("cargo:rerun-if-changed=cpp_engine/include/tensor_ops.hpp"); // Added header watch
    println!("cargo:rerun-if-changed=cpp_engine/src/ffi_bridge.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/ocr_pipeline.cpp");

    // 2. Automated FFI generation via bindgen
    let bindings = bindgen::Builder::default()
        .header(cpp_header)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("fin_.*")
        .allowlist_type("Fin.*")
        .generate()
        .expect("Unable to generate native C++ FFI bindings via bindgen");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindgen FFI output!");
}
