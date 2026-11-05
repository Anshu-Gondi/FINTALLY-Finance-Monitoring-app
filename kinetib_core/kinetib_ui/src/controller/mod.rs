#[cfg(target_arch = "wasm32")]
mod browser;
mod budgets;
mod feedback;
#[cfg(not(target_arch = "wasm32"))]
mod native;
mod transactions;

#[cfg(target_arch = "wasm32")]
pub(crate) use browser::attach_browser;
#[cfg(not(target_arch = "wasm32"))]
pub(crate) use native::attach_native;
