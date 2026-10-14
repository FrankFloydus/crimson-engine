add_library(crimson-node-editor STATIC
    crude_json.cpp
    imgui_canvas.cpp
    imgui_node_editor.cpp
    imgui_node_editor_api.cpp)
target_include_directories(crimson-node-editor SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(crimson-node-editor PUBLIC imgui::imgui)
target_compile_features(crimson-node-editor PUBLIC cxx_std_20)
target_compile_definitions(crimson-node-editor PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
