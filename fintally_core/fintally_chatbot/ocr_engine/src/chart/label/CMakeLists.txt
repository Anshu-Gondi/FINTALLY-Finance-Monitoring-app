target_sources(fin_ocr_native PRIVATE
    label_recognizer.cpp
    label_engine.cpp
    label_geometry.cpp
    label_filter.cpp
    label_merger.cpp
    label_output.cpp
)

add_subdirectory(text)
add_subdirectory(candidate)
add_subdirectory(category)
add_subdirectory(y_axis)
add_subdirectory(recognition)
