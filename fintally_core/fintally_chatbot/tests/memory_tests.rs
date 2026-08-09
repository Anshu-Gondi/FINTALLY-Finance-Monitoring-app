#[cfg(test)]
mod tests {
    use fintally_chatbot::core::vision::ffi::*;
    use fintally_chatbot::core::vision::memory::SafeFinBuffer;
    use std::ptr;

    #[test]
    fn test_safe_fin_buffer_null_pointer() {
        // Passing a null pointer to SafeFinBuffer::new should return None safely
        let safe_buf = SafeFinBuffer::new(ptr::null_mut());
        assert!(safe_buf.is_none(), "Null pointer should result in None");
    }

    #[test]
    fn test_safe_fin_buffer_valid_allocation() {
        unsafe {
            let engine = fin_engine_create();
            assert!(!engine.is_null());

            let input_bytes = vec![128u8; 100];
            let target_width = 768;
            let target_height = 768;
            let channels = 3;

            // Updated to pass explicit target dimensions matching test assertions
            let raw_ptr = fin_process_document_bytes(
                engine,
                input_bytes.as_ptr(),
                input_bytes.len(),
                FinInputType_FIN_INPUT_RAW_IMAGE,
                target_width,
                target_height,
                channels,
            );

            assert!(!raw_ptr.is_null());

            let safe_buf = SafeFinBuffer::new(raw_ptr).expect("Buffer creation failed");

            // Verify dimensions derived from the C++ pipeline
            let (width, height, ch) = safe_buf.dimensions();
            assert_eq!(width, target_width);
            assert_eq!(height, target_height);
            assert_eq!(ch, channels);

            // Verify slice conversion
            let slice = safe_buf.as_slice();
            assert_eq!(slice.len(), target_width * target_height * channels);

            fin_engine_destroy(engine);
            // `safe_buf` goes out of scope here and automatically triggers `fin_free_processed_buffer`
        }
    }
}
