target_sources(fin_ocr_native PRIVATE
    interpreter_engine.cpp
    interpreter_geometry.cpp
    interpreter_lookup.cpp
    axis_value_mapper.cpp
    chart_type_classifier.cpp
    stack_classifier.cpp
)

add_subdirectory(data)
