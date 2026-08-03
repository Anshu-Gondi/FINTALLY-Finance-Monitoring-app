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
        let raw_ptr = fin_process_document_bytes(
            engine,
            input_bytes.as_ptr(),
            input_bytes.len(),
            FinInputType_FIN_INPUT_RAW_IMAGE,
        );

        assert!(!raw_ptr.is_null());

        let safe_buf = SafeFinBuffer::new(raw_ptr).expect("Buffer creation failed");

        // Verify dimensions derived from the C++ pipeline
        let (width, height, channels) = safe_buf.dimensions();
        assert_eq!(width, 768);
        assert_eq!(height, 768);
        assert_eq!(channels, 3);

        // Verify slice conversion
        let slice = safe_buf.as_slice();
        assert_eq!(slice.len(), 768 * 768 * 3);

        fin_engine_destroy(engine);
        // `safe_buf` goes out of scope here and automatically triggers `fin_free_processed_buffer`
    }
}
