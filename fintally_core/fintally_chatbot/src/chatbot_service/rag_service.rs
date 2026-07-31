use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;
use tokio::time::interval;

// Native Google Drive ecosystem imports
use google_drive3::hyper_rustls::HttpsConnectorBuilder;
use google_drive3::hyper_util::rt::TokioExecutor;
use google_drive3::{api::File as DriveFile, DriveHub};
use http_body_util::BodyExt;
use yup_oauth2::{InstalledFlowAuthenticator, InstalledFlowReturnMethod};

use crate::core::utils::errors::AppError;
use crate::native_layer::rag::RagEngine;
use unicode_normalization::UnicodeNormalization;

use lopdf::Document;

/// Helper function to sanitize raw text for safe embedding generation.
/// Normalizes Unicode (NFKC) and strips out unprintable control characters,
/// orphan bytes, and invalid symbols that cause tokenizer index panics.
fn sanitize_text_for_embedding(raw_text: &str) -> String {
    raw_text
        .nfkc()
        .filter(|c| {
            c.is_alphanumeric()
                || c.is_ascii_punctuation()
                || *c == ' '
                || *c == '\n'
                || *c == '\t'
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<&str>>()
        .join(" ")
}

/// Helper function to extract text directly from PDF binary bytes using lopdf.
/// Pure Rust parsing that bypasses color-space graphics panics.
fn extract_text_with_lopdf(pdf_bytes: &[u8]) -> Result<String, AppError> {
    let doc = Document::load_mem(pdf_bytes)
        .map_err(|e| AppError::InferenceError(format!("Failed parsing PDF binary structure: {e}")))?;

    let mut full_text = String::new();
    let page_numbers: Vec<u32> = doc.get_pages().keys().cloned().collect();

    for page_num in page_numbers {
        if let Ok(page_text) = doc.extract_text(&[page_num]) {
            full_text.push_str(&page_text);
            full_text.push('\n');
        }
    }

    Ok(full_text)
}

/// Structure encapsulating your FinTally RAG Core Subsystem components natively.
pub struct RagService {
    /// Protected instance of the HNSW Vector Index and embedded chunk database.
    pub engine: Arc<RwLock<RagEngine>>,
    /// Path tracking your local cached filesystem repository for Gov tax parameters.
    pub storage_dir: PathBuf,
}

impl RagService {
    /// Instantiates a thread-safe implementation of the RAG Service layer.
    pub fn new(storage_path: &str, embedding_model_path: &str) -> Result<Self, AppError> {
        let storage_dir = PathBuf::from(storage_path);
        let model_dir = PathBuf::from(embedding_model_path);

        // 1. Resolve path to absolute
        let absolute_model = std::fs::canonicalize(&model_dir).map_err(|e| {
            AppError::InferenceError(format!(
                "Failed to resolve path for embedding model directory '{embedding_model_path}': {e}"
            ))
        })?;

        // 2. Strip Windows UNC prefix if present
        let model_path_str = absolute_model.to_string_lossy();
        let mut clean_model_path = if model_path_str.starts_with(r"\\?\") {
            model_path_str[4..].to_string()
        } else {
            model_path_str.into_owned()
        };

        // 3. FIX: Convert all '\' to '/' to prevent '\b' (backspace) and '\U' (unicode) escape corruption in Python/C++
        clean_model_path = clean_model_path.replace('\\', "/");

        println!(
            "🚀 [usearch-bridge] Passing sanitised path to native engine: {}",
            clean_model_path
        );

        // 4. HARD CHECK: Check if config.json is physically there BEFORE calling the FFI engine
        let config_file_path = std::path::Path::new(&clean_model_path).join("config.json");
        if !config_file_path.exists() {
            return Err(AppError::InferenceError(format!(
                "CRITICAL: Rust confirmed 'config.json' is physically MISSING from the directory: {}",
                clean_model_path
            )));
        }

        // Ensure database storage directory exists
        std::fs::create_dir_all(&storage_dir).map_err(|e| {
            AppError::InferenceError(format!("Failed establishing storage nodes: {e}"))
        })?;

        // 5. Instantiate native Rust RagEngine across the cxxbridge
        let engine_instance = RagEngine::new(
            clean_model_path,
            120, // chunk_size
            20,  // chunk_overlap
            384, // dimensions
            0.5, // alpha
            2,   // default_top_k
            Some(
                storage_dir
                    .join("hnsw_index.bin")
                    .to_string_lossy()
                    .to_string(),
            ),
            Some(
                storage_dir
                    .join("text_chunks.db")
                    .to_string_lossy()
                    .to_string(),
            ),
        )
        .map_err(|e| {
            AppError::InferenceError(format!("Could not instantiate native RagEngine: {e}"))
        })?;

        Ok(Self {
            engine: Arc::new(RwLock::new(engine_instance)),
            storage_dir,
        })
    }

    /// Primary execution sequence ingestion handler mapping multi-extension payloads natively.
    pub async fn ingest_document(&self, file_path: &str) -> Result<(), AppError> {
        let path_obj = Path::new(file_path);
        let ext = path_obj
            .extension()
            .and_then(|s| s.to_str())
            .unwrap_or_default()
            .to_lowercase();

        match ext.as_str() {
            "txt" | "json" | "csv" => {
                let content = tokio::fs::read_to_string(path_obj).await.map_err(|e| {
                    AppError::InferenceError(format!("Payload read failure on {file_path}: {e}"))
                })?;

                if content.trim().is_empty() {
                    return Err(AppError::InferenceError(
                        "Document contains no readable text layers.".into(),
                    ));
                }

                // Sanitize unicode ligatures and invalid characters
                let normalized_content = sanitize_text_for_embedding(&content);
                tokio::fs::write(path_obj, &normalized_content)
                    .await
                    .map_err(|e| {
                        AppError::InferenceError(format!(
                            "Failed writing sanitized text back to file: {e}"
                        ))
                    })?;

                let mut engine_guard = self.engine.write().await;
                engine_guard.ingest_file(file_path).map_err(|e| {
                    AppError::InferenceError(format!(
                        "Native core embedding extraction error: {e}"
                    ))
                })?;
            }
            "pdf" => {
                println!(
                    "[RAG-SERVICE] Extracting text layer from PDF layout via lopdf: {}",
                    path_obj.display()
                );

                let pdf_bytes = tokio::fs::read(path_obj).await.map_err(|e| {
                    AppError::InferenceError(format!("Failed reading PDF binary: {e}"))
                })?;

                let file_path_clone = file_path.to_string();

                // Isolate PDF extraction on a blocking task and catch potential unwinds safely
                let extraction_result = tokio::task::spawn_blocking(move || {
                    std::panic::catch_unwind(|| extract_text_with_lopdf(&pdf_bytes))
                })
                .await;

                let raw_extracted_text = match extraction_result {
                    Ok(Ok(Ok(text))) => text,
                    Ok(Ok(Err(e))) => {
                        return Err(AppError::InferenceError(format!(
                            "PDF text parser encountered error on {file_path_clone}: {e}"
                        )));
                    }
                    Ok(Err(_)) | Err(_) => {
                        eprintln!(
                            "[RAG-SERVICE] WARNING: Unhandled exception during PDF layout parsing for {file_path_clone}. Skipping file safely."
                        );
                        return Err(AppError::InferenceError(format!(
                            "PDF structural parser exception isolated for: {file_path_clone}"
                        )));
                    }
                };

                if raw_extracted_text.trim().is_empty() {
                    return Err(AppError::InferenceError(format!(
                        "PDF file contains no parseable text layers: {file_path}"
                    )));
                }

                // Strictly sanitize text before saving to remove hidden symbols / out-of-bounds tokens
                let cleaned_text = sanitize_text_for_embedding(&raw_extracted_text);

                let txt_counterpart = path_obj.with_extension("txt");
                tokio::fs::write(&txt_counterpart, &cleaned_text)
                    .await
                    .map_err(|e| {
                        AppError::InferenceError(format!(
                            "Failed caching extracted PDF text: {e}"
                        ))
                    })?;

                let mut engine_guard = self.engine.write().await;
                let txt_path_str = txt_counterpart.to_string_lossy().to_string();

                if let Err(e) = engine_guard.ingest_file(&txt_path_str) {
                    eprintln!("[RAG-SERVICE] ERROR: Ingestion failed on {txt_path_str}: {e}");
                    return Err(AppError::InferenceError(format!(
                        "Native core embedding extraction error: {e}"
                    )));
                }
            }
            _ => {
                return Err(AppError::InferenceError(format!(
                    "Unsupported document format: .{ext}"
                )));
            }
        }

        Ok(())
    }

    /// Queries the vector store across multidimensional hyperplanes to extract relevant strings.
    pub async fn search_knowledge(&self, query_text: &str, limit: usize) -> String {
        let engine_guard = self.engine.read().await;
        // Pass None to allow default threshold, or Some(0.75) for standard distance bounds
        match engine_guard.query(query_text, Some(limit), Some(0.75), None) {
            Ok(query_response) => {
                let context = query_response.context_block.into_string();
                println!("[RAG-SERVICE] Retrieved context length: {} chars", context.len());
                context
            }
            Err(e) => {
                eprintln!("[RAG-SERVICE] Warning: Vector search sequence fault occurred: {e}");
                String::new()
            }
        }
    }

    /// Helper function to generate a secure path for the token cache outside public paths
    fn secure_token_store_path() -> PathBuf {
        let mut path = std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."));
        path.push(".secure_vault");
        path
    }

    /// Enforces strict filesystem file permissions (Owner Read/Write Only: 0600) on Unix environments
    fn enforce_file_boundary_lock(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            if let Ok(metadata) = fs::metadata(path) {
                let mut perms = metadata.permissions();
                perms.set_mode(0o600); // Blocks unauthorized machine reads
                let _ = fs::set_permissions(path, perms);
            }
        }
    }

    /// Setup safe non-interactive client channel infrastructure connected to remote Google endpoints
    async fn build_secure_drive_client(
        &self,
    ) -> Result<
        DriveHub<
            google_drive3::hyper_rustls::HttpsConnector<
                google_drive3::hyper_util::client::legacy::connect::HttpConnector,
            >,
        >,
        AppError,
    > {
        // Look up client secret file path located at your core project workspace root
        let secret_path = Path::new("../../client_secret.json");
        let active_secret_path = if secret_path.exists() {
            secret_path
        } else {
            Path::new("client_secret.json")
        };

        let app_secret = yup_oauth2::read_application_secret(active_secret_path)
            .await
            .map_err(|e| {
                AppError::InferenceError(format!(
                    "Failed reading application client_secret.json layout: {e}"
                ))
            })?;

        let secure_vault_dir = Self::secure_token_store_path();
        fs::create_dir_all(&secure_vault_dir).unwrap();
        let token_storage_file = secure_vault_dir.join("token_vault.json");

        // Use the Local Redirect mechanism. First run requests a terminal authentication click;
        // subsequent loops use the encrypted token store silently and automatically.
        let auth = InstalledFlowAuthenticator::builder(
            app_secret,
            InstalledFlowReturnMethod::HTTPRedirect,
        )
        .persist_tokens_to_disk(&token_storage_file)
        .build()
        .await
        .map_err(|e| {
            AppError::InferenceError(format!("OAuth Authenticator build process failed: {e}"))
        })?;

        Self::enforce_file_boundary_lock(&token_storage_file);

        let https_connector = HttpsConnectorBuilder::new()
            .with_native_roots()
            .map_err(|e| {
                AppError::InferenceError(format!("TLS Trust Store Initialization error: {e}"))
            })?
            .https_only()
            .enable_http2()
            .build();

        let client = hyper_util::client::legacy::Client::builder(
            google_drive3::hyper_util::rt::TokioExecutor::new(),
        )
        .build(https_connector);

        Ok(DriveHub::new(client, auth))
    }

    /// Synchronizes Indian Government Tax Amendments from your storage interface without Python barriers.
    pub async fn sync_and_reindex_trusted_sources(&self) -> Result<(), AppError> {
        let target_dir = Path::new("./trusted_docs_source");
        std::fs::create_dir_all(target_dir)
            .map_err(|e| AppError::InferenceError(format!("Local file setup violation: {e}")))?;

        println!(
            "[RAG-SYNC] Requesting context manifests from authenticated cloud directory tree..."
        );

        let hub = self.build_secure_drive_client().await?;

        // Step 1: Scan for target Parent Container Folder Name matching 'Llm docs'
        let folder_query =
            "name contains 'Llm docs' and (mimeType = 'application/vnd.google-apps.folder' or mimeType = 'application/vnd.google-apps.shortcut') and trashed = false";
        let (_, folder_results) = hub
            .files()
            .list()
            .add_scope("https://www.googleapis.com/auth/drive.readonly")
            .q(folder_query)
            .doit()
            .await
            .map_err(|e| {
                AppError::InferenceError(format!("Failed looking up target folder metadata: {e}"))
            })?;

        let folder_match = folder_results.files.unwrap_or_default();
        if folder_match.is_empty() {
            println!(
                "[RAG-SYNC] Target folder 'Llm docs' not found on remote drive. Synchronization canceled."
            );
            return Ok(());
        }

        let target_folder_id = folder_match[0].id.as_deref().unwrap_or_default();

        // Step 2: List all content assets present directly inside this parent container folder
        let objects_query = format!("'{}' in parents and trashed = false", target_folder_id);
        let (_, items_results) = hub
            .files()
            .list()
            .add_scope("https://www.googleapis.com/auth/drive.readonly")
            .q(&objects_query)
            .doit()
            .await
            .map_err(|e| {
                AppError::InferenceError(format!(
                    "Failed reading remote directory asset trees: {e}"
                ))
            })?;

        let discovered_assets = items_results.files.unwrap_or_default();
        let mut downloaded_paths: Vec<PathBuf> = Vec::new();

        for file_meta in discovered_assets {
            let item_id = match file_meta.id.as_deref() {
                Some(id) => id,
                None => {
                    continue;
                }
            };
            let item_name = match file_meta.name.as_deref() {
                Some(name) => name,
                None => {
                    continue;
                }
            };

            let local_destination = target_dir.join(item_name);

            // Deduplication optimization check: avoids processing identical historical runs
            if local_destination.exists() {
                downloaded_paths.push(local_destination);
                continue;
            }

            println!(
                "[RAG-SYNC] Extracting binary media stream down to node workspace: {item_name}"
            );

            let (stream_response, _) = hub
                .files()
                .get(item_id)
                .add_scope("https://www.googleapis.com/auth/drive.readonly")
                .param("alt", "media")
                .doit()
                .await
                .map_err(|e| {
                    AppError::InferenceError(format!(
                        "Network download processing fault on object {item_name}: {e}"
                    ))
                })?;

            let raw_bytes = stream_response
                .into_body()
                .collect()
                .await
                .map_err(|e| {
                    AppError::InferenceError(format!(
                        "Failed formatting binary chunk sequences: {e}"
                    ))
                })?
                .to_bytes();

            let mut out_file = File::create(&local_destination).map_err(|e| {
                AppError::InferenceError(format!("Disk write allocation error: {e}"))
            })?;
            out_file.write_all(&raw_bytes).unwrap();

            downloaded_paths.push(local_destination);
        }

        if downloaded_paths.is_empty() {
            println!(
                "[RAG-SYNC] Google Drive folders matched fully. System RAG layers up-to-date."
            );
            return Ok(());
        }

        for file_path in downloaded_paths {
            let path_str = file_path.to_string_lossy().to_string();
            match self.ingest_document(&path_str).await {
                Ok(_) => {
                    println!(
                        "[RAG-SYNC] Updated embedding records for: {:?}",
                        file_path.file_name()
                    )
                }
                Err(e) => eprintln!("[RAG-SYNC] Failed indexing sequence asset {path_str}: {e}"),
            }
        }

        Ok(())
    }

    /// Replaces APScheduler configuration with a native, zero-overhead background tokio worker.
    pub fn start_periodic_sync_job(self: Arc<Self>, interval_minutes: u64) {
        tokio::spawn(async move {
            println!(
                "[RAG-CRON] Background RAG Scheduler Initialized. Cycle configuration: Every {interval_minutes} minutes."
            );
            let mut ticker = interval(Duration::from_secs(interval_minutes * 60));
            ticker.tick().await;

            loop {
                ticker.tick().await;
                println!(
                    "[RAG-CRON] Launching daily automated Google Drive RAG synchronization sweep..."
                );
                if let Err(e) = self.sync_and_reindex_trusted_sources().await {
                    eprintln!(
                        "[RAG-CRON] Critical background execution context synchronizer fault: {e}"
                    );
                }
            }
        });
    }

    /// Checks if persistent index files (`hnsw_index.bin` and `text_chunks.db`) exist and are populated.
    pub fn has_indexes(&self) -> bool {
        let hnsw_path = self.storage_dir.join("hnsw_index.bin");
        let db_path = self.storage_dir.join("text_chunks.db");

        let hnsw_exists = hnsw_path.exists()
            && fs::metadata(&hnsw_path)
                .map(|m| m.len() > 0)
                .unwrap_or(false);
        let db_exists = db_path.exists()
            && fs::metadata(&db_path)
                .map(|m| m.len() > 0)
                .unwrap_or(false);

        hnsw_exists && db_exists
    }
}
