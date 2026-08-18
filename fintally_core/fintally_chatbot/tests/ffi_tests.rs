#[cfg(test)]
mod tests {
    use fintally_chatbot::core::vision::ffi::*;

    #[test]
    fn test_raw_ffi_engine_lifecycle() {
        unsafe {
            let ctx = fin_engine_create();
            assert!(!ctx.is_null(), "Engine allocation failed");
            fin_engine_destroy(ctx);
        }
    }

    #[test]
    fn test_raw_ffi_pdf_binarization() {
        unsafe {
            let ctx = fin_engine_create();
            assert!(!ctx.is_null());

            let input_bytes: Vec<u8> = vec![50, 100, 150, 200, 250];

            let buf_ptr = fin_process_document_bytes(
                ctx,
                input_bytes.as_ptr(),
                input_bytes.len(),
                FinInputType_FIN_INPUT_PDF_PAGE,
                1920,
                1080,
                3,
            );

            assert!(!buf_ptr.is_null());
            let buf = &*buf_ptr;

            assert!(!buf.data.is_null());
            let data_slice = std::slice::from_raw_parts(buf.data, buf.data_len);

            for &val in data_slice {
                assert!(
                    val == 0 || val == 255,
                    "Expected binarized value (0 or 255), got {}",
                    val
                );
            }

            fin_free_processed_buffer(buf_ptr);
            fin_engine_destroy(ctx);
        }
    }

    #[test]
    fn test_raw_ffi_chart_contrast_boost() {
        unsafe {
            let ctx = fin_engine_create();
            assert!(!ctx.is_null());

            let width = 100;
            let height = 100;
            let channels = 3;
            let buffer_size = (width * height * channels) as usize;

            let input_bytes: Vec<u8> = vec![100; buffer_size];

            let buf_ptr = fin_process_document_bytes(
                ctx,
                input_bytes.as_ptr(),
                input_bytes.len(),
                FinInputType_FIN_INPUT_FIN_CHART,
                width,
                height,
                channels,
            );

            assert!(!buf_ptr.is_null());
            let buf = &*buf_ptr;
            assert!(!buf.data.is_null());

            let data_slice = std::slice::from_raw_parts(buf.data, buf.data_len);
            assert_eq!(data_slice.len(), buffer_size);

            fin_free_processed_buffer(buf_ptr);
            fin_engine_destroy(ctx);
        }
    }
}
