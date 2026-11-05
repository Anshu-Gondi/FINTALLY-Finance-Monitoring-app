//! Thin executable entry point. Application logic lives in the testable library.

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    kinetib_kernel::bootstrap::run().await
}
