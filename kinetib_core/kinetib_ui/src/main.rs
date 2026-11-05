#![cfg_attr(target_arch = "wasm32", allow(dead_code))]

#[cfg(not(target_arch = "wasm32"))]
fn main() {
    if kinetib_ui::run_native().is_err() {
        eprintln!("FinTally UI failed to start");
        std::process::exit(1);
    }
}

#[cfg(target_arch = "wasm32")]
fn main() {}
