//! Bounded single-producer/single-consumer queue for fixed-size Rust messages.
//!
//! This queue is for Rust-owned `VoiceEvent` values, not the C ABI. Audio sample
//! rings and native DSP buffers should continue to use the existing C API.
//! Construction allocates once; push/pop are bounded, non-blocking, and do not
//! allocate or lock. Exactly one producer endpoint and one consumer endpoint
//! are created, and neither endpoint is clonable.

use core::cell::UnsafeCell;
use core::fmt;
use core::mem::{size_of, MaybeUninit};
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;

#[cfg(test)]
const CACHE_LINE_BYTES: usize = 64;

/// Place independently written metadata on separate cache lines.
#[repr(align(64))]
struct CachePadded<T>(T);

/// Each payload starts on a cache-line boundary. Slot stride is rounded up to
/// a multiple of 64 by `repr(align(64))`, preventing adjacent slots from sharing
/// a cache line on the selected target profile.
#[repr(align(64))]
struct Slot<T>(UnsafeCell<MaybeUninit<T>>);

struct Inner<T, const N: usize> {
    // Each field is independently aligned/padded. `producer_head` is written
    // only by the producer; `consumer_tail` only by the consumer.
    slots: Box<[Slot<T>]>,
    producer_head: CachePadded<AtomicUsize>,
    consumer_tail: CachePadded<AtomicUsize>,
    closed: CachePadded<AtomicBool>,
}

// SAFETY: Endpoint construction creates exactly one producer and one consumer.
// The producer exclusively writes a slot before release-publishing `head`; the
// consumer acquire-loads `head` before reading that slot. The consumer release-
// publishes `tail` only after reading, and the producer acquire-loads `tail`
// before reusing a slot. T is required to be Send and Copy at the endpoint API.
unsafe impl<T: Send, const N: usize> Sync for Inner<T, N> {}

/// The producer owns the only write endpoint. This type intentionally is not
/// `Clone`; do not add cloning without changing the queue's concurrency model.
pub(crate) struct SpscProducer<T: Copy + Send + 'static, const N: usize> {
    inner: Arc<Inner<T, N>>,
}

/// The consumer owns the only read endpoint.
pub(crate) struct SpscConsumer<T: Copy + Send + 'static, const N: usize> {
    inner: Arc<Inner<T, N>>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpscConfigError {
    CapacityTooSmall(usize),
    CapacityNotPowerOfTwo(usize),
}

impl fmt::Display for SpscConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::CapacityTooSmall(capacity) => {
                write!(f, "SPSC capacity must be at least 2; got {capacity}")
            }
            Self::CapacityNotPowerOfTwo(capacity) => write!(
                f,
                "SPSC capacity must be a power of two for mask-based indexing; got {capacity}"
            ),
        }
    }
}

impl std::error::Error for SpscConfigError {}

#[derive(Debug)]
pub(crate) enum SpscPushError<T> {
    Full(T),
    Closed(T),
}

/// Create a bounded SPSC queue. Capacity must be a power of two; no sentinel
/// slot is sacrificed, so the configured capacity is fully usable.
pub(crate) fn bounded_spsc<T: Copy + Send + 'static, const N: usize>(
) -> Result<(SpscProducer<T, N>, SpscConsumer<T, N>), SpscConfigError> {
    if N < 2 {
        return Err(SpscConfigError::CapacityTooSmall(N));
    }
    if !N.is_power_of_two() {
        return Err(SpscConfigError::CapacityNotPowerOfTwo(N));
    }

    // Reserve exactly N slots up front. Any allocations happen during startup,
    // never during push/pop. The boxed slot array avoids putting a large queue
    // on an embedded worker's stack.
    let mut slots = Vec::with_capacity(N);
    for _ in 0..N {
        slots.push(Slot(UnsafeCell::new(MaybeUninit::uninit())));
    }

    let inner = Arc::new(Inner {
        slots: slots.into_boxed_slice(),
        producer_head: CachePadded(AtomicUsize::new(0)),
        consumer_tail: CachePadded(AtomicUsize::new(0)),
        closed: CachePadded(AtomicBool::new(false)),
    });

    Ok((
        SpscProducer {
            inner: Arc::clone(&inner),
        },
        SpscConsumer { inner },
    ))
}

impl<T: Copy + Send + 'static, const N: usize> SpscProducer<T, N> {
    /// Try to publish one item. Returns ownership of the item on backpressure
    /// or closure; callers must choose whether to retry, defer, or count a drop.
    #[inline]
    pub(crate) fn try_push(&mut self, value: T) -> Result<(), SpscPushError<T>> {
        if self.inner.closed.0.load(Ordering::Acquire) {
            return Err(SpscPushError::Closed(value));
        }

        let head = self.inner.producer_head.0.load(Ordering::Relaxed);
        let tail = self.inner.consumer_tail.0.load(Ordering::Acquire);
        if head.wrapping_sub(tail) >= N {
            return Err(SpscPushError::Full(value));
        }

        let slot = &self.inner.slots[head & (N - 1)];
        // SAFETY: this producer exclusively owns `head`; occupancy was checked
        // against the consumer's acquire-loaded tail, so this slot is not live.
        unsafe { (*slot.0.get()).write(value) };

        // Publish the initialized payload only after the write has completed.
        self.inner
            .producer_head
            .0
            .store(head.wrapping_add(1), Ordering::Release);
        Ok(())
    }

    pub(crate) fn close(&mut self) {
        self.inner.closed.0.store(true, Ordering::Release);
    }

    #[inline]
    pub(crate) fn len_approx(&self) -> usize {
        let head = self.inner.producer_head.0.load(Ordering::Relaxed);
        let tail = self.inner.consumer_tail.0.load(Ordering::Acquire);
        head.wrapping_sub(tail).min(N)
    }

    pub(crate) const fn capacity(&self) -> usize {
        N
    }

    /// Queue payload + inner-structure estimate; excludes allocator headers and
    /// the Arc control block. Use this in the engine's explicit memory ledger.
    pub(crate) fn storage_bytes_estimate(&self) -> usize {
        N.saturating_mul(size_of::<Slot<T>>())
            .saturating_add(size_of::<Inner<T, N>>())
    }
}

impl<T: Copy + Send + 'static, const N: usize> Drop for SpscProducer<T, N> {
    fn drop(&mut self) {
        self.close();
    }
}

impl<T: Copy + Send + 'static, const N: usize> SpscConsumer<T, N> {
    /// Pop one item without blocking. `None` means empty at this instant; check
    /// `is_closed()` as well to distinguish a drained, finished producer.
    #[inline]
    pub(crate) fn try_pop(&mut self) -> Option<T> {
        let tail = self.inner.consumer_tail.0.load(Ordering::Relaxed);
        let head = self.inner.producer_head.0.load(Ordering::Acquire);
        if tail == head {
            return None;
        }

        let slot = &self.inner.slots[tail & (N - 1)];
        // SAFETY: acquire-loading head observes the producer's published write.
        // Only this consumer reads this slot; the producer cannot reuse it until
        // the release-store of the advanced tail below becomes visible.
        let value = unsafe { (*slot.0.get()).assume_init_read() };

        self.inner
            .consumer_tail
            .0
            .store(tail.wrapping_add(1), Ordering::Release);
        Some(value)
    }

    pub(crate) fn close(&mut self) {
        self.inner.closed.0.store(true, Ordering::Release);
    }

    pub(crate) fn is_closed(&self) -> bool {
        self.inner.closed.0.load(Ordering::Acquire)
    }

    #[inline]
    pub(crate) fn len_approx(&self) -> usize {
        let head = self.inner.producer_head.0.load(Ordering::Acquire);
        let tail = self.inner.consumer_tail.0.load(Ordering::Relaxed);
        head.wrapping_sub(tail).min(N)
    }

    pub(crate) const fn capacity(&self) -> usize {
        N
    }

    pub(crate) fn storage_bytes_estimate(&self) -> usize {
        N.saturating_mul(size_of::<Slot<T>>())
            .saturating_add(size_of::<Inner<T, N>>())
    }
}

impl<T: Copy + Send + 'static, const N: usize> Drop for SpscConsumer<T, N> {
    fn drop(&mut self) {
        // A producer should stop publishing if the consumer endpoint disappears.
        self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capacity_is_fully_usable_and_full_returns_original_value() {
        let (mut producer, mut consumer) = bounded_spsc::<u64, 4>().unwrap();
        for value in 0..4 {
            assert!(producer.try_push(value).is_ok());
        }
        assert!(matches!(
            producer.try_push(99),
            Err(SpscPushError::Full(99))
        ));
        for value in 0..4 {
            assert_eq!(consumer.try_pop(), Some(value));
        }
        assert_eq!(consumer.try_pop(), None);
    }

    #[test]
    fn close_is_observed_after_queued_items_are_drained() {
        let (mut producer, mut consumer) = bounded_spsc::<u32, 2>().unwrap();
        producer.try_push(7).unwrap();
        producer.close();
        assert_eq!(consumer.try_pop(), Some(7));
        assert_eq!(consumer.try_pop(), None);
        assert!(consumer.is_closed());
        assert!(matches!(
            producer.try_push(8),
            Err(SpscPushError::Closed(8))
        ));
    }

    #[test]
    fn rejects_invalid_capacity() {
        assert!(matches!(
            bounded_spsc::<u8, 0>(),
            Err(SpscConfigError::CapacityTooSmall(0))
        ));
        assert!(matches!(
            bounded_spsc::<u8, 3>(),
            Err(SpscConfigError::CapacityNotPowerOfTwo(3))
        ));
    }

    #[test]
    fn every_slot_starts_on_a_64_byte_boundary() {
        assert_eq!(core::mem::align_of::<Slot<u64>>(), CACHE_LINE_BYTES);
        assert_eq!(core::mem::size_of::<Slot<u64>>() % CACHE_LINE_BYTES, 0);
    }
}
