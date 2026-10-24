find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
add_library(decal-proof-math STATIC
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/Math.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MeshNormals.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MeshPaint.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MaterialGraph.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MaterialGraphImageBake.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MaterialProcedural.cpp")
target_include_directories(decal-proof-math PUBLIC
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/include"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/include/crimson/renderer/forward")
target_link_libraries(decal-proof-math PRIVATE glm::glm nlohmann_json::nlohmann_json)
target_compile_definitions(decal-proof-math PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(decal-proof-math PRIVATE /W4 /permissive- /utf-8)
add_executable(decal_model_surface_tests
    "${CC_DECAL_ROOT}/tests/decal_model_surface_tests.cpp"
    "${CC_DECAL_ROOT}/src/decals/DecalModelSurface.cpp")
target_link_libraries(decal_model_surface_tests PRIVATE crimson-decals decal-proof-math)
target_compile_definitions(decal_model_surface_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(decal_model_surface_tests PRIVATE /W4 /permissive- /utf-8)
add_test(NAME decal_model_surface_core COMMAND decal_model_surface_tests)
add_dependencies(decal_proofs decal_model_surface_tests)
find_package(nlohmann_json CONFIG REQUIRED)
add_library(decal-paint-surface-proof STATIC "${CC_DECAL_ROOT}/src/decals/DecalPaintSurface.cpp" "${CC_DECAL_ROOT}/src/decals/DecalModelSurface.cpp")
target_link_libraries(decal-paint-surface-proof PUBLIC crimson-decals decal-proof-math)
add_executable(decal_paint_surface_tests "${CC_DECAL_ROOT}/tests/decal_paint_surface_tests.cpp")
target_link_libraries(decal_paint_surface_tests PRIVATE decal-paint-surface-proof)
add_test(NAME decal_paint_surface_core COMMAND decal_paint_surface_tests)
add_executable(decal_room_probe "${CC_DECAL_ROOT}/tests/decal_room_probe.cpp")
target_link_libraries(decal_room_probe PRIVATE decal-paint-surface-proof nlohmann_json::nlohmann_json)
add_dependencies(decal_proofs decal_paint_surface_tests decal_room_probe)
