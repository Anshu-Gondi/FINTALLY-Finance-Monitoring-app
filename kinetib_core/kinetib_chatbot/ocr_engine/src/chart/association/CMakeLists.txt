target_sources(fin_ocr_native PRIVATE
    association_engine.cpp
    association_geometry.cpp
    association_storage.cpp
)
add_subdirectory(object)
add_subdirectory(stacked)
add_subdirectory(dual_axis)
add_subdirectory(label)
