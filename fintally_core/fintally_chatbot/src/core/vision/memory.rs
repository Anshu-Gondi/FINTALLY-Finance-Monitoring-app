use super::ffi::*;
use std::ffi::CStr;
use std::slice;

pub struct SafeFinBuffer {
    raw: *mut FinProcessedBuffer, // Made private to protect RAII invariant
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

    pub fn is_binarized(&self) -> bool {
        unsafe {
            let buf = &*self.raw;
            buf.is_binarized != 0
        }
    }

    pub fn extracted_text(&self) -> Option<&str> {
        unsafe {
            let buf = &*self.raw;
            if buf.extracted_text.is_null() {
                None
            } else {
                CStr::from_ptr(buf.extracted_text).to_str().ok()
            }
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

// Sound because moving ownership between threads does not affect C++ heap allocations
unsafe impl Send for SafeFinBuffer {}
