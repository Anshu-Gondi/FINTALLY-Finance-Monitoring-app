fn main() {
    // // Read search paths propagated from the library crate's build script
    // if let Ok(path) = std::env::var("DEP_FIN_OCR_NATIVE_SEARCH_PATH") {
    //     println!("cargo:rustc-link-search=native={}/fin_ocr_cmake", path);
    //     println!("cargo:rustc-link-search=native={}/fin_ocr_cmake/lib", path);
    // }

    // // Force link static symbols and system dynamic libraries into the final binary
    // println!("cargo:rustc-link-arg=-Wl,--whole-archive");
    // println!("cargo:rustc-link-lib=static=fin_ocr_native");
    // println!("cargo:rustc-link-arg=-Wl,--no-whole-archive");

    // println!("cargo:rustc-link-lib=stdc++");
    // println!("cargo:rustc-link-lib=tesseract");
    // println!("cargo:rustc-link-lib=leptonica");
    // println!("cargo:rustc-link-lib=pdfium");
}
