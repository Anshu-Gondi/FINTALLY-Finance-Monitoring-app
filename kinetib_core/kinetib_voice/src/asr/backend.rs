//! ASR backend contract and the optional Sherpa-ONNX streaming backend.
//!
//! The recognizer worker owns the backend. A backend is constructed *inside* that
//! worker so native model/session handles never cross worker boundaries.
//! Model weights and native ONNX Runtime allocations are intentionally tracked
//! separately from the bounded Rust-side runtime working-set estimate.

use std::path::PathBuf;
use thiserror::Error;

pub const ASR_SAMPLE_RATE_HZ: u32 = 16_000;
pub const ASR_FRAME_SAMPLES: usize = 320; // 20 ms at 16 kHz
pub const MAX_TRANSCRIPT_BYTES: usize = 512;
pub const SHERPA_MAX_THREADS: i32 = 4;

#[derive(Debug, Error)]
pub enum AsrError {
    #[error("invalid ASR configuration: {0}")]
    InvalidConfig(&'static str),
    #[error("ASR backend feature is disabled; enable `asr-sherpa-onnx`")]
    BackendFeatureDisabled,
    #[error("required ASR model file does not exist: {0:?}")]
    ModelFileMissing(PathBuf),
    #[error("failed to create the Sherpa-ONNX streaming recognizer")]
    BackendCreateFailed,
    #[error("unsupported sample rate {actual}; this pipeline requires {expected} Hz")]
    UnsupportedSampleRate { expected: u32, actual: u32 },
    #[error("audio frame has {actual} samples; expected exactly {expected}")]
    InvalidFrameLength { expected: usize, actual: usize },
    #[error("ASR runtime estimate {estimated} bytes exceeds budget {budget} bytes")]
    MemoryBudgetExceeded { estimated: usize, budget: usize },
    #[error("ASR worker channel is disconnected")]
    ChannelDisconnected,
    #[error("ASR worker is stopping")]
    WorkerStopping,
    #[error("ASR worker thread failed: {0}")]
    WorkerThread(String),
    #[error("transcript has {actual} UTF-8 bytes; fixed output capacity is {capacity}")]
    TranscriptTooLong { actual: usize, capacity: usize },
    #[error("Sherpa-ONNX did not accept the configured model")]
    ModelRejected,
    #[error("streaming decoder exceeded its bounded decode-step budget")]
    DecodeStepLimitExceeded,
}

/// Streaming decode update kind. Final means endpointed or explicitly flushed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TranscriptKind {
    Partial,
    Final,
}

/// Borrowed, short-lived result passed from a backend into the bounded event sink.
/// Consumers must copy it before returning from the callback.
#[derive(Debug, Clone, Copy)]
pub struct BackendTranscript<'a> {
    pub text: &'a str,
    pub kind: TranscriptKind,
}

/// Object-safe callback used to move transcript updates into the runtime's
/// fixed-capacity output event. It is invoked on the ASR worker, never the audio
/// capture/DSP thread.
pub type UpdateSink<'a> =
    dyn for<'text> FnMut(BackendTranscript<'text>) -> Result<(), AsrError> + 'a;

/// All methods are called by one ASR worker thread. No Send/Sync promise is made
/// for backend implementations because native model/session ownership is local.
pub trait AsrBackend: 'static {
    /// Accept exactly one bounded PCM16 frame and perform a bounded number of
    /// incremental decode steps.
    fn push_audio(
        &mut self,
        pcm16: &[i16],
        sample_rate_hz: u32,
        sink: &mut UpdateSink<'_>,
    ) -> Result<(), AsrError>;

    /// Discard the active utterance and reset decoder state.
    fn cancel(&mut self) -> Result<(), AsrError>;

    /// Flush trailing context and emit any remaining transcript as final.
    fn finish(&mut self, sink: &mut UpdateSink<'_>) -> Result<(), AsrError>;
}

#[derive(Debug, Clone)]
pub enum AsrBackendConfig {
    SherpaOnnxOnlineTransducer(SherpaOnlineTransducerConfig),
}

/// Configuration for a Sherpa-ONNX streaming transducer (encoder/decoder/joiner).
/// The model must be compatible with the 16 kHz input contract.
#[derive(Debug, Clone)]
pub struct SherpaOnlineTransducerConfig {
    pub encoder: PathBuf,
    pub decoder: PathBuf,
    pub joiner: PathBuf,
    pub tokens: PathBuf,
    pub provider: String,
    pub num_threads: i32,
    pub enable_endpoint: bool,
    pub decoding_method: String,
}

impl SherpaOnlineTransducerConfig {
    pub fn new(
        encoder: impl Into<PathBuf>,
        decoder: impl Into<PathBuf>,
        joiner: impl Into<PathBuf>,
        tokens: impl Into<PathBuf>,
    ) -> Self {
        Self {
            encoder: encoder.into(),
            decoder: decoder.into(),
            joiner: joiner.into(),
            tokens: tokens.into(),
            provider: "cpu".to_owned(),
            num_threads: 1,
            enable_endpoint: true,
            decoding_method: "greedy_search".to_owned(),
        }
    }

    pub fn validate(&self) -> Result<(), AsrError> {
        if self.num_threads < 1 || self.num_threads > SHERPA_MAX_THREADS {
            return Err(AsrError::InvalidConfig(
                "Sherpa-ONNX num_threads must be in 1..=4 for the constrained profile",
            ));
        }
        if self.provider.trim().is_empty() {
            return Err(AsrError::InvalidConfig(
                "Sherpa-ONNX provider cannot be empty",
            ));
        }
        if !matches!(
            self.decoding_method.as_str(),
            "greedy_search" | "modified_beam_search"
        ) {
            return Err(AsrError::InvalidConfig(
                "decoding_method must be greedy_search or modified_beam_search",
            ));
        }
        for path in [&self.encoder, &self.decoder, &self.joiner, &self.tokens] {
            if !path.is_file() {
                return Err(AsrError::ModelFileMissing(path.clone()));
            }
        }
        Ok(())
    }
}

/// Construct a backend. The caller should invoke this from the dedicated ASR
/// worker, not from the audio capture or native DSP thread.
pub fn create_backend(config: AsrBackendConfig) -> Result<Box<dyn AsrBackend>, AsrError> {
    #[cfg(feature = "asr-sherpa-onnx")]
    {
        match config {
            AsrBackendConfig::SherpaOnnxOnlineTransducer(config) => {
                Ok(Box::new(SherpaOnlineTransducer::create(config)?))
            }
        }
    }

    #[cfg(not(feature = "asr-sherpa-onnx"))]
    {
        let _ = config;
        Err(AsrError::BackendFeatureDisabled)
    }
}

#[cfg(feature = "asr-sherpa-onnx")]
const MAX_DECODE_STEPS_PER_FRAME: usize = 32;
#[cfg(feature = "asr-sherpa-onnx")]
const MAX_FINISH_DECODE_STEPS: usize = 256;
#[cfg(feature = "asr-sherpa-onnx")]
const TAIL_PADDING_FRAMES: usize = 15; // 300 ms, fed as bounded 20 ms chunks

#[cfg(feature = "asr-sherpa-onnx")]
#[repr(align(64))]
struct AlignedFloatFrame([f32; ASR_FRAME_SAMPLES]);

#[cfg(feature = "asr-sherpa-onnx")]
struct SherpaOnlineTransducer {
    recognizer: sherpa_onnx::OnlineRecognizer,
    stream: sherpa_onnx::OnlineStream,
    sample_rate_hz: u32,
    // Reused every frame: no application-owned audio Vec is allocated per push.
    pcm_f32: AlignedFloatFrame,
    // Track hypotheses without maintaining a heap-backed transcript copy.
    last_hash: u64,
    last_len: usize,
    has_last: bool,
}

#[cfg(feature = "asr-sherpa-onnx")]
impl SherpaOnlineTransducer {
    fn create(config: SherpaOnlineTransducerConfig) -> Result<Self, AsrError> {
        config.validate()?;

        let mut recognizer_config = sherpa_onnx::OnlineRecognizerConfig::default();
        recognizer_config.model_config.transducer.encoder =
            Some(config.encoder.to_string_lossy().into_owned());
        recognizer_config.model_config.transducer.decoder =
            Some(config.decoder.to_string_lossy().into_owned());
        recognizer_config.model_config.transducer.joiner =
            Some(config.joiner.to_string_lossy().into_owned());
        recognizer_config.model_config.tokens = Some(config.tokens.to_string_lossy().into_owned());
        recognizer_config.model_config.provider = Some(config.provider);
        recognizer_config.model_config.num_threads = config.num_threads;
        recognizer_config.enable_endpoint = config.enable_endpoint;
        recognizer_config.decoding_method = Some(config.decoding_method);

        let recognizer = sherpa_onnx::OnlineRecognizer::create(&recognizer_config)
            .ok_or(AsrError::BackendCreateFailed)?;
        let stream = recognizer.create_stream();

        Ok(Self {
            recognizer,
            stream,
            sample_rate_hz: ASR_SAMPLE_RATE_HZ,
            pcm_f32: AlignedFloatFrame([0.0; ASR_FRAME_SAMPLES]),
            last_hash: 0,
            last_len: 0,
            has_last: false,
        })
    }

    fn clear_hypothesis(&mut self) {
        self.last_hash = 0;
        self.last_len = 0;
        self.has_last = false;
    }

    fn remember_hypothesis(&mut self, text: &str) {
        self.last_hash = Self::hash_text(text.as_bytes());
        self.last_len = text.len();
        self.has_last = true;
    }

    fn hash_text(bytes: &[u8]) -> u64 {
        // FNV-1a; only used for change detection, never as identity or security.
        let mut hash = 0xcbf2_9ce4_8422_2325_u64;
        for byte in bytes {
            hash ^= u64::from(*byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
        hash
    }

    fn emit_current(
        &mut self,
        kind: TranscriptKind,
        sink: &mut UpdateSink<'_>,
    ) -> Result<(), AsrError> {
        let Some(result) = self.recognizer.get_result(&self.stream) else {
            return Ok(());
        };
        // Preserve the backend text as returned. Cleanup/normalization belongs
        // in `transcript::cleanup`, not in the ASR boundary.
        let text = result.text.as_str();
        if text.is_empty() {
            return Ok(());
        }

        let hash = Self::hash_text(text.as_bytes());
        let changed = !self.has_last || self.last_hash != hash || self.last_len != text.len();
        if kind == TranscriptKind::Final || changed {
            sink(BackendTranscript { text, kind })?;
            self.remember_hypothesis(text);
        }
        Ok(())
    }

    fn drain_ready(
        &mut self,
        max_steps: usize,
        sink: &mut UpdateSink<'_>,
    ) -> Result<usize, AsrError> {
        let mut steps = 0;
        while steps < max_steps && self.recognizer.is_ready(&self.stream) {
            self.recognizer.decode(&self.stream);
            steps += 1;

            if self.recognizer.is_endpoint(&self.stream) {
                self.emit_current(TranscriptKind::Final, sink)?;
                self.recognizer.reset(&self.stream);
                self.clear_hypothesis();
            } else {
                self.emit_current(TranscriptKind::Partial, sink)?;
            }
        }
        Ok(steps)
    }
}

#[cfg(feature = "asr-sherpa-onnx")]
impl AsrBackend for SherpaOnlineTransducer {
    fn push_audio(
        &mut self,
        pcm16: &[i16],
        sample_rate_hz: u32,
        sink: &mut UpdateSink<'_>,
    ) -> Result<(), AsrError> {
        if sample_rate_hz != self.sample_rate_hz {
            return Err(AsrError::UnsupportedSampleRate {
                expected: self.sample_rate_hz,
                actual: sample_rate_hz,
            });
        }
        if pcm16.len() != ASR_FRAME_SAMPLES {
            return Err(AsrError::InvalidFrameLength {
                expected: ASR_FRAME_SAMPLES,
                actual: pcm16.len(),
            });
        }

        // Convert one 20 ms frame into a reusable normalized float buffer.
        for (out, sample) in self.pcm_f32.0.iter_mut().zip(pcm16.iter().copied()) {
            *out = f32::from(sample) / 32_768.0;
        }
        self.stream
            .accept_waveform(sample_rate_hz as i32, &self.pcm_f32.0);
        // Bound work per input frame to avoid an unbounded decoder loop.
        self.drain_ready(MAX_DECODE_STEPS_PER_FRAME, sink)?;
        Ok(())
    }

    fn cancel(&mut self) -> Result<(), AsrError> {
        self.recognizer.reset(&self.stream);
        self.clear_hypothesis();
        Ok(())
    }

    fn finish(&mut self, sink: &mut UpdateSink<'_>) -> Result<(), AsrError> {
        // Add trailing silence in fixed-size slices; no temporary Vec allocation.
        self.pcm_f32.0.fill(0.0);
        for _ in 0..TAIL_PADDING_FRAMES {
            self.stream
                .accept_waveform(self.sample_rate_hz as i32, &self.pcm_f32.0);
            self.drain_ready(MAX_DECODE_STEPS_PER_FRAME, sink)?;
        }

        self.stream.input_finished();
        self.drain_ready(MAX_FINISH_DECODE_STEPS, sink)?;
        if self.recognizer.is_ready(&self.stream) {
            return Err(AsrError::DecodeStepLimitExceeded);
        }
        self.emit_current(TranscriptKind::Final, sink)?;
        self.recognizer.reset(&self.stream);
        self.clear_hypothesis();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_sherpa_config_uses_constrained_cpu_settings() {
        let config =
            SherpaOnlineTransducerConfig::new("enc.onnx", "dec.onnx", "join.onnx", "tokens.txt");
        assert_eq!(config.provider, "cpu");
        assert_eq!(config.num_threads, 1);
        assert!(config.enable_endpoint);
        assert_eq!(config.decoding_method, "greedy_search");
    }

    #[test]
    fn rejects_excessive_backend_threads_before_loading_models() {
        let mut config = SherpaOnlineTransducerConfig::new("enc", "dec", "join", "tokens");
        config.num_threads = SHERPA_MAX_THREADS + 1;
        assert!(matches!(config.validate(), Err(AsrError::InvalidConfig(_))));
    }

    #[test]
    fn sample_rate_and_frame_contract_are_explicit() {
        assert_eq!(ASR_SAMPLE_RATE_HZ, 16_000);
        assert_eq!(ASR_FRAME_SAMPLES, 320);
        assert_eq!(MAX_TRANSCRIPT_BYTES, 512);
    }
}
