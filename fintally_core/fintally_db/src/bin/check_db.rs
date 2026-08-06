use fintally_db::DbContext;
use fintally_db::chat_service::ChatHistoryService;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("🔌 Attempting to load .env and connect to Supabase...");

    // 1. Test Connection Initialization
    let db_ctx = match DbContext::init().await {
        Ok(ctx) => {
            println!("✅ Success! Securely connected to Supabase Postgres pool.");
            ctx
        }
        Err(e) => {
            eprintln!("❌ Connection Failed! Check your DATABASE_URL or network.");
            eprintln!("Error details: {}", e);
            std::process::exit(1);
        }
    };

    // 2. Test basic SQL execution (Raw ping)
    println!("🛰️ Pinging database instance directly...");
    let row: (i32,) = sqlx::query_as("SELECT 1")
        .fetch_one(&db_ctx.pool)
        .await?;
    println!("✅ Database responded live (Ping value: {}).", row.0);

    // 3. Test your tables and Service logic
    println!("📝 Testing read/write operations on chat log tables...");
    let chat_service = ChatHistoryService::new(db_ctx);

    // Define test integer IDs matching `i64`
    let test_user_id: i64 = 12345;
    let test_session_id: Option<i64> = Some(101);

    // Write a mock entry
    chat_service.append_message(
        test_user_id,
        "user",
        "Hello Supabase from my N4020 machine!",
        test_session_id,
        None
    ).await?;
    println!("✅ Wrote test message to `chat_messages` table.");

    // Read it back
    let history = chat_service.get_history(test_user_id, test_session_id, 5).await?;
    println!("✅ Read history back safely. Record count: {}", history.len());
    if let Some(msg) = history.first() {
        println!("    Message content: \"{}\"", msg.content);
    }

    // Clean up our test footprint
    chat_service.clear_history(test_user_id, test_session_id).await?;
    println!("🗑️ Cleaned up test rows successfully.");

    println!("\n🎉 Everything is fully operational! Database is ready for Axum routing.");
    Ok(())
}
