use serde_json::Value;
use crate::core::llm::planner::Planner;
use crate::core::utils::errors::AppError;

/// High-performance tool executor for LLM planner and financial mathematical tools.
/// Executes the tool asynchronously, matching your core architecture.
pub async fn execute_tool(
    tool_name: &str,
    args_json: &str,
) -> Result<String, AppError> {
    // 1. Parse JSON string parameter slice into a queryable serde object
    let args: Value = serde_json::from_str(args_json)
        .map_err(|e| AppError::ValidationError(format!("Malformed engine tool arguments: {e}")))?;

    // 2. Await the tool result directly inside your existing async engine pipeline
    let result: Value = Planner::execute(tool_name, args).await?;

    // 3. Serialize back into a clean Rust String payload
    serde_json::to_string(&result)
        .map_err(|e| AppError::SerializationError(format!("Failed serializing tool outcome: {e}")))
}