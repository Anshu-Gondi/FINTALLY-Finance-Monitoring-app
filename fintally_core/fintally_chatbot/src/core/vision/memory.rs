use super::ffi::*;
use std::slice;

pub struct SafeFinBuffer {
    pub raw: *mut FinProcessedBuffer,
}

impl SafeFinBuffer {
    pub fn new(ptr: *mut FinProcessedBuffer) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            Some(Self { raw: ptr })
        }
    }

    pub fn as_slice(&self) -> &[u8] {
        unsafe {
            let buf = &*self.raw;
            if buf.data.is_null() || buf.data_len == 0 {
                &[]
            } else {
                slice::from_raw_parts(buf.data, buf.data_len)
            }
        }
    }

    pub fn dimensions(&self) -> (usize, usize, usize) {
        unsafe {
            let buf = &*self.raw;
            (buf.width, buf.height, buf.channels)
        }
    }
}

impl Drop for SafeFinBuffer {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe {
                fin_free_processed_buffer(self.raw);
            }
        }
    }
}
