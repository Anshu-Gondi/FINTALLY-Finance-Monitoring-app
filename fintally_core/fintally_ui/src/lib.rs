//! FinTally Slint front end.
//!
//! The UI talks only to `fintally_ipc_client`. Session tokens remain private
//! inside that client; this crate does not access the database or application
//! services directly.

mod backend;
mod controller;
pub mod presentation;

slint::include_modules!();

#[cfg(not(target_arch = "wasm32"))]
pub fn run_native() -> Result<(), String> {
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()
        .map_err(|_| "failed to initialize the asynchronous runtime".to_owned())?;

    let window =
        MainWindow::new().map_err(|_| "failed to create the FinTally window".to_owned())?;
    controller::attach_native(&window, runtime.handle().clone());
    window
        .run()
        .map_err(|_| "the FinTally UI event loop failed".to_owned())
}

#[cfg(target_arch = "wasm32")]
fn run_web() -> Result<(), String> {
    let window =
        MainWindow::new().map_err(|_| "failed to create the FinTally window".to_owned())?;
    controller::attach_browser(&window);
    window
        .run()
        .map_err(|_| "the FinTally UI event loop failed".to_owned())
}

#[cfg(target_arch = "wasm32")]
#[wasm_bindgen::prelude::wasm_bindgen(start)]
pub fn wasm_start() {
    if run_web().is_err() {
        // Never expose credentials, tokens, or internal transport details.
        web_sys::console::error_1(&"FinTally UI failed to start".into());
    }
}

/// Slint Android entry point. The app must host the kernel service in-process
/// or configure a supported local endpoint before sign-in can succeed.
#[cfg(target_os = "android")]
#[unsafe(no_mangle)]
pub fn android_main(app: slint::android::AndroidApp) {
    if slint::android::init(app).is_err() {
        return;
    }
    if run_native().is_err() {
        // Avoid logging credentials or transport internals on mobile.
        eprintln!("FinTally UI failed to start");
    }
}

pub mod navigation;
