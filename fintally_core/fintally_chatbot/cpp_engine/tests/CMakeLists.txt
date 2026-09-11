add_executable(fin_ocr_tests
    test_dataset.cpp
    test_engine.cpp
    test_tensor_ops.cpp
)

target_link_libraries(
    fin_ocr_tests
    PRIVATE
        fin_ocr_native
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(
    fin_ocr_tests
)
