//! Native audio decoder.
//!
//! Current source:
//!     MP3
//!
//! Pipeline:
//!
//!     MP3
//!       ↓
//!     Symphonia
//!       ↓
//!     interleaved f32
//!       ↓
//!     mono f32
//!
//! This module belongs to the offline builder only.
//! Runtime microphone decoding remains outside this module.

use std::fs::File;
use std::path::Path;

use anyhow::{anyhow, bail, Context, Result};

use symphonia::core::codecs::audio::AudioDecoderOptions;
use symphonia::core::errors::Error as SymphoniaError;
use symphonia::core::formats::probe::Hint;
use symphonia::core::formats::{FormatOptions, TrackType};
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;

// =============================================================================
// DECODED AUDIO
// =============================================================================

#[derive(Debug)]
pub struct DecodedAudio {
    /// Mono floating-point PCM samples.
    pub samples: Vec<f32>,

    /// Original decoded sample rate.
    pub sample_rate: u32,

    /// Number of source channels before downmixing.
    pub source_channels: usize,
}

// =============================================================================
// PUBLIC API
// =============================================================================

/// Decode a source audio file into mono f32 samples.
///
/// The source sample rate is preserved in the returned structure.
/// Resampling to 16 kHz is handled by `resampler.rs`.
pub fn decode_to_mono_f32(path: &Path) -> Result<DecodedAudio> {
    // -------------------------------------------------------------------------
    // Open source file.
    // -------------------------------------------------------------------------

    let file = File::open(path)
        .with_context(|| format!("failed to open audio file: {}", path.display()))?;

    let media_source_stream = MediaSourceStream::new(Box::new(file), Default::default());

    // -------------------------------------------------------------------------
    // Create a format hint.
    // -------------------------------------------------------------------------

    let mut hint = Hint::new();

    if let Some(extension) = path.extension().and_then(|ext| ext.to_str()) {
        hint.with_extension(extension);
    }

    // -------------------------------------------------------------------------
    // Probe the source.
    //
    // Symphonia 0.6.1 returns the format reader directly from `probe()`.
    // There is no `.format` field here.
    // -------------------------------------------------------------------------

    let format_options = FormatOptions::default();

    let metadata_options = MetadataOptions::default();

    let mut format = symphonia::default::get_probe()
        .probe(&hint, media_source_stream, format_options, metadata_options)
        .with_context(|| format!("failed to probe audio file: {}", path.display()))?;

    // -------------------------------------------------------------------------
    // Find the default audio track.
    // -------------------------------------------------------------------------

    let track = format
        .default_track(TrackType::Audio)
        .ok_or_else(|| anyhow!("no audio track found: {}", path.display()))?;

    let track_id = track.id;

    // -------------------------------------------------------------------------
    // Get audio codec parameters.
    //
    // In Symphonia 0.6.1, `codec_params` is optional.
    // We must unwrap that Option before calling `.audio()`.
    // -------------------------------------------------------------------------

    let codec_params = track
        .codec_params
        .as_ref()
        .ok_or_else(|| anyhow!("audio codec parameters missing: {}", path.display()))?
        .audio()
        .ok_or_else(|| anyhow!("selected track is not an audio codec: {}", path.display()))?;

    // -------------------------------------------------------------------------
    // Create decoder.
    // -------------------------------------------------------------------------

    let decoder_options = AudioDecoderOptions::default();

    let mut decoder = symphonia::default::get_codecs()
        .make_audio_decoder(codec_params, &decoder_options)
        .with_context(|| format!("failed to create decoder for: {}", path.display()))?;

    // -------------------------------------------------------------------------
    // Output state.
    // -------------------------------------------------------------------------

    let mut output_samples = Vec::<f32>::new();

    let mut packet_samples = Vec::<f32>::new();

    let mut source_sample_rate: Option<u32> = None;

    let mut source_channels: Option<usize> = None;

    // -------------------------------------------------------------------------
    // Decode packets.
    // -------------------------------------------------------------------------

    loop {
        let packet = match format.next_packet() {
            Ok(Some(packet)) => packet,

            Ok(None) => {
                // End of stream.
                break;
            }

            Err(SymphoniaError::ResetRequired) => {
                bail!("decoder reset required for {}", path.display());
            }

            Err(error) => {
                bail!("failed reading packet from {}: {}", path.display(), error);
            }
        };

        // ---------------------------------------------------------------------
        // Ignore packets from unrelated tracks.
        // ---------------------------------------------------------------------

        if packet.track_id != track_id {
            continue;
        }

        // ---------------------------------------------------------------------
        // Decode packet.
        // ---------------------------------------------------------------------

        let decoded = match decoder.decode(&packet) {
            Ok(audio_buffer) => audio_buffer,

            Err(SymphoniaError::IoError(error)) => {
                bail!("I/O error while decoding {}: {}", path.display(), error);
            }

            Err(SymphoniaError::DecodeError(error)) => {
                bail!("decode error in {}: {}", path.display(), error);
            }

            Err(SymphoniaError::ResetRequired) => {
                bail!("decoder reset required while decoding {}", path.display());
            }

            Err(error) => {
                bail!("decoder error in {}: {}", path.display(), error);
            }
        };

        // ---------------------------------------------------------------------
        // Read signal specification directly.
        //
        // IMPORTANT:
        //
        // We intentionally do not import `SignalSpec`.
        // `decoded.spec()` gives us the appropriate spec object for the
        // decoded buffer, and its methods are:
        //
        //     rate()
        //     channels()
        // ---------------------------------------------------------------------

        let spec = decoded.spec();

        let sample_rate = spec.rate();

        let channels = spec.channels().count();

        if sample_rate == 0 {
            bail!("decoder returned zero sample rate for {}", path.display());
        }

        if channels == 0 {
            bail!("decoder returned zero channels for {}", path.display());
        }

        // ---------------------------------------------------------------------
        // Verify stream consistency.
        // ---------------------------------------------------------------------

        match source_sample_rate {
            Some(expected) if expected != sample_rate => {
                bail!(
                    "sample rate changed inside {}: {} Hz -> {} Hz",
                    path.display(),
                    expected,
                    sample_rate
                );
            }

            None => {
                source_sample_rate = Some(sample_rate);
            }

            _ => {}
        }

        match source_channels {
            Some(expected) if expected != channels => {
                bail!(
                    "channel count changed inside {}: {} -> {}",
                    path.display(),
                    expected,
                    channels
                );
            }

            None => {
                source_channels = Some(channels);
            }

            _ => {}
        }

        // ---------------------------------------------------------------------
        // Convert Symphonia's generic audio buffer to interleaved f32.
        //
        // This is the generic API provided by Symphonia 0.6.1.
        // ---------------------------------------------------------------------

        let required_samples = decoded.samples_interleaved();

        if required_samples == 0 {
            continue;
        }

        packet_samples.resize(required_samples, 0.0_f32);

        decoded.copy_to_slice_interleaved(&mut packet_samples);

        // ---------------------------------------------------------------------
        // Downmix to mono.
        // ---------------------------------------------------------------------

        if channels == 1 {
            for &sample in &packet_samples {
                if !sample.is_finite() {
                    bail!("non-finite decoded sample in {}", path.display());
                }
            }

            output_samples.extend_from_slice(&packet_samples);

            continue;
        }

        // Multi-channel source.
        for frame in packet_samples.chunks_exact(channels) {
            let mut sum = 0.0_f32;

            for &sample in frame {
                if !sample.is_finite() {
                    bail!("non-finite decoded sample in {}", path.display());
                }

                sum += sample;
            }

            let mono = sum / channels as f32;

            if !mono.is_finite() {
                bail!("non-finite mono sample in {}", path.display());
            }

            output_samples.push(mono);
        }
    }

    // -------------------------------------------------------------------------
    // Final validation.
    // -------------------------------------------------------------------------

    let sample_rate = source_sample_rate
        .ok_or_else(|| anyhow!("no decodable audio packets found: {}", path.display()))?;

    let source_channels = source_channels.ok_or_else(|| {
        anyhow!(
            "could not determine source channel count: {}",
            path.display()
        )
    })?;

    if output_samples.is_empty() {
        bail!("decoder produced zero samples: {}", path.display());
    }

    if output_samples.iter().any(|sample| !sample.is_finite()) {
        bail!(
            "decoded audio contains non-finite samples: {}",
            path.display()
        );
    }

    Ok(DecodedAudio {
        samples: output_samples,
        sample_rate,
        source_channels,
    })
}

// =============================================================================
// TESTS
// =============================================================================

#[cfg(test)]
mod tests {

    #[test]
    fn decoder_module_is_available() {
        assert!(true);
    }
}
