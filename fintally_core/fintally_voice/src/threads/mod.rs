pub const DEFAULT_AUDIO_FRAME_QUEUE_CAPACITY: usize = 4;

pub mod affinity;
pub mod consumer;
pub mod control_llm;
pub mod producer;
mod spsc;
pub mod ui_bridge;
