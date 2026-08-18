use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn download_stb_image(out_dir: &Path) -> PathBuf {
    let stb_dir = out_dir.join("stb");
    let header_path = stb_dir.join("stb_image.h");

    if header_path.exists() {
        return stb_dir;
    }

    fs::create_dir_all(&stb_dir).expect("Failed to create stb directory");

    let url = "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h";
    println!("cargo:warning=Downloading stb_image.h from GitHub...");

    let response = ureq::get(url).call().expect("Failed to download stb_image.h");
    let mut file = fs::File::create(&header_path).expect("Failed to create stb_image.h file");

    let mut reader = response.into_reader();
    std::io::copy(&mut reader, &mut file).expect("Failed to write stb_image.h");

    stb_dir
}

fn download_and_extract_pdfium(out_dir: &Path) -> (PathBuf, PathBuf) {
    let pdfium_dir = out_dir.join("pdfium");
    let include_dir = pdfium_dir.join("include");
    let lib_dir = pdfium_dir.join("lib");

    if include_dir.exists() && lib_dir.exists() {
        return (include_dir, lib_dir);
    }

    let url = "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F6531/pdfium-linux-x64.tgz";
    println!("cargo:warning=Downloading PDFium binaries from GitHub...");

    let response = ureq::get(url).call().expect("Failed to download PDFium binaries");
    let tar_gz = flate2::read::GzDecoder::new(response.into_reader());
    let mut archive = tar::Archive::new(tar_gz);

    fs::create_dir_all(&pdfium_dir).expect("Failed to create PDFium output directory");
    archive.unpack(&pdfium_dir).expect("Failed to unpack PDFium binaries");

    (include_dir, lib_dir)
}

fn main() {
    let cpp_header = "cpp_engine/include/ffi_bridge.h";
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    // 1. Resolve Third-Party Dependencies (PDFium & stb_image)
    let (pdfium_include, pdfium_lib) = if let (Ok(inc), Ok(lib)) = (env::var("PDFIUM_INCLUDE_DIR"), env::var("PDFIUM_LIB_DIR")) {
        (PathBuf::from(inc), PathBuf::from(lib))
    } else {
        download_and_extract_pdfium(&out_dir)
    };

    let stb_include = download_stb_image(&out_dir);

    // 2. Configure C++ Compilation
    let mut build = cc::Build::new();

    build
        .cpp(true)
        .std("c++17")
        .pic(true)
        .warnings(true)
        .extra_warnings(false)
        .file("cpp_engine/src/ffi_bridge.cpp")
        .file("cpp_engine/src/ocr_pipeline.cpp")
        .file("cpp_engine/src/thermal_sensor.cpp")
        .file("cpp_engine/src/matrix_matcher.cpp")
        .include("cpp_engine/include")
        .include("cpp_engine/src")
        .include(&pdfium_include)
        .include(&stb_include);

    // Check for local vendor paths if available
    if Path::new("third_party/stb").exists() {
        build.include("third_party/stb");
    }

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap_or_default();

    if build.get_compiler().is_like_msvc() {
        build.flag("/O2").flag("/fp:fast").flag("/permissive-");
        #[cfg(target_feature = "avx2")]
        build.flag("/arch:AVX2");
    } else {
        build
            .flag_if_supported("-O3")
            .flag_if_supported("-ffast-math")
            .flag_if_supported("-fvisibility=hidden");

        if target_arch == "x86_64" {
            build.flag_if_supported("-msse4.2");
            build.flag_if_supported("-mavx2");
        }
    }

    build.compile("fin_ocr_native");

    // 3. Linker Configuration
    println!("cargo:rustc-link-search=native={}", pdfium_lib.display());
    println!("cargo:rustc-link-lib=pdfium");

    if target_os == "linux" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    } else if target_os == "macos" {
        println!("cargo:rustc-link-lib=dylib=c++");
    }

    // 4. Track Changes for Incremental Rebuilds
    println!("cargo:rerun-if-env-changed=PDFIUM_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=PDFIUM_LIB_DIR");
    println!("cargo:rerun-if-changed={}", cpp_header);
    println!("cargo:rerun-if-changed=cpp_engine/include/tensor_ops.hpp");
    println!("cargo:rerun-if-changed=cpp_engine/include/thermal_sensor.hpp");
    println!("cargo:rerun-if-changed=cpp_engine/include/matrix_matcher.hpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/ffi_bridge.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/ocr_pipeline.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/thermal_sensor.cpp");
    println!("cargo:rerun-if-changed=cpp_engine/src/matrix_matcher.cpp");

    // 5. Generate Bindgen Bindings
    let bindings = bindgen::Builder::default()
        .header(cpp_header)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .allowlist_function("fin_.*")
        .allowlist_type("Fin.*")
        .size_t_is_usize(true)
        .generate()
        .expect("Unable to generate C++ FFI bindings via bindgen");

    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("Couldn't write bindgen FFI output!");
}
