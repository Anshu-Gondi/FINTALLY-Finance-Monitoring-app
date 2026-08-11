// src/core/llm/planner.rs

use serde_json::{json, Value};
use crate::core::llm::tools::{execute_tool_async, ToolDiagnosticReport};

/// Async Planner for orchestrating LLM tool execution
pub struct Planner;

impl Planner {
    /// Execute any tool asynchronously. Returns either execution result or diagnostic payload.
    pub async fn execute(tool_name: &str, args: Value) -> Result<Value, ToolDiagnosticReport> {
        execute_tool_async(tool_name, args).await
    }

    /// Helper that guarantees a Value response (useful for directly returning tool execution reports back to LLM context)
    pub async fn execute_with_diagnostic_fallback(tool_name: &str, args: Value) -> Value {
        match Self::execute(tool_name, args).await {
            Ok(output) => json!({
                "status": "success",
                "result": output
            }),
            Err(diagnostic) => diagnostic.to_llm_payload(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use tokio;

    #[tokio::test]
    #[cfg_attr(miri, ignore)]
    async fn planner_returns_diagnostic_payload_on_missing_field() {
        let args = json!({ "principal": 500000 }); // missing rate and tenure
        let response = Planner::execute_with_diagnostic_fallback("calculate_emi", args).await;

        assert_eq!(response["status"], "error");
        assert_eq!(
            response["diagnostic_report"]["status"],
            "MissingArguments"
        );
        assert!(response["diagnostic_report"]["remediation_prompt"]
            .as_str()
            .unwrap()
            .contains("annual_rate"));
    }
}
