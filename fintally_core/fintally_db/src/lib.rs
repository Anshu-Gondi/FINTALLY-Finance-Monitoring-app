pub mod models;
pub mod chat_service;

use sqlx::postgres::{PgPoolOptions, PgConnectOptions};
use sqlx::PgPool;
use std::env;
use std::str::FromStr;

#[derive(Clone)]
pub struct DbContext {
    pub pool: PgPool,
}

impl DbContext {
    pub async fn init() -> Result<Self, Box<dyn std::error::Error>> {
        // Load the .env file if it exists (fails silently if file is missing in prod)
        let _ = dotenvy::dotenv();

        // Safely extract the connection string
        let database_url = env::var("DATABASE_URL")
            .map_err(|_| "DATABASE_URL environment variable is not set in your .env file")?;

        // Parse connection options and strictly disable cached prepared statements 
        // to make it 100% compatible with Supabase's transaction pooler.
        let connection_options = PgConnectOptions::from_str(&database_url)?
            .statement_cache_capacity(0);

        // Tailor the connection pool boundaries specifically for your N4020 CPU profile
        let pool = PgPoolOptions::new()
            .max_connections(5) // Low limit prevents context-switching thrashing on 2 cores
            .min_connections(1) // Avoids keeping idle memory allocated
            .connect_with(connection_options)
            .await?;

        Ok(Self { pool })
    }
}