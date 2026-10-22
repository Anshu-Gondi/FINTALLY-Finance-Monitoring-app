//! Allocation-free decoding of fixed-size C event records.

use super::{EngineError, EngineEventKind, FixedText, FEATURE_DIMENSION};
use crate::ffi;

pub(super) fn decode_event_kind(
    event: &ffi::FinVoiceEvent,
) -> Result<EngineEventKind, EngineError> {
    match event.type_ as u32 {
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_SPEECH_STARTED => Ok(EngineEventKind::SpeechStarted),
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_SPEECH_ENDED => Ok(EngineEventKind::SpeechEnded),
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_FEATURES_READY => {
            // SAFETY: union variant is selected by the event type discriminator.
            let payload = unsafe { event.payload.features };
            if payload.dimension as usize != FEATURE_DIMENSION {
                return Err(EngineError::InvalidNativeEvent(
                    "feature event has an unexpected dimension",
                ));
            }
            Ok(EngineEventKind::FeaturesReady {
                dimension: payload.dimension,
                values: payload.values,
            })
        }
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_QUANTIZATION_RESULT => {
            // SAFETY: union variant is selected by the event type discriminator.
            let payload = unsafe { event.payload.quantization };
            let no_centroid = ffi::FIN_VOICE_EVENT_NO_CENTROID;
            Ok(EngineEventKind::QuantizationResult {
                best_index: (payload.best_index != no_centroid)
                    .then_some(payload.best_index as usize),
                second_best_index: (payload.second_best_index != no_centroid)
                    .then_some(payload.second_best_index as usize),
                best_distance_squared: payload.best_distance_squared,
                second_best_distance_squared: payload.second_distance_squared,
                relative_gap: payload.relative_gap,
                language_id: payload.language,
            })
        }
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_TEXT_READY => {
            // SAFETY: union variant is selected by the event type discriminator.
            let payload = unsafe { event.payload.text };
            let len = payload.length as usize;
            if len > ffi::FIN_VOICE_EVENT_TEXT_CAPACITY as usize {
                return Err(EngineError::InvalidNativeEvent(
                    "text event length exceeds the fixed payload capacity",
                ));
            }
            let mut bytes = [0u8; ffi::FIN_VOICE_EVENT_TEXT_CAPACITY as usize];
            for (destination, source) in bytes[..len].iter_mut().zip(payload.utf8[..len].iter()) {
                *destination = *source as u8;
            }
            core::str::from_utf8(&bytes[..len])
                .map_err(|_| EngineError::InvalidNativeEvent("text event is not valid UTF-8"))?;
            Ok(EngineEventKind::TextReady {
                text: FixedText { bytes, len },
                is_final: event.flags & (ffi::FIN_VOICE_EVENT_FLAG_FINAL as u16) != 0,
            })
        }
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_ERROR => {
            // SAFETY: union variant is selected by the event type discriminator.
            let payload = unsafe { event.payload.status };
            Ok(EngineEventKind::Error {
                code: payload.code,
                detail: payload.detail,
            })
        }
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_STATUS => {
            // SAFETY: union variant is selected by the event type discriminator.
            let payload = unsafe { event.payload.status };
            Ok(EngineEventKind::Status {
                code: payload.code,
                detail: payload.detail,
            })
        }
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_SHUTDOWN => Ok(EngineEventKind::Shutdown),
        ffi::FinVoiceEventType_FIN_VOICE_EVENT_RESET => Ok(EngineEventKind::Reset),
        raw_type => Ok(EngineEventKind::Unknown {
            raw_type: raw_type as u16,
        }),
    }
}
