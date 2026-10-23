find_package(glm CONFIG REQUIRED)
add_library(decal-proof-math STATIC
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/Math.cpp"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/src/forward/MeshNormals.cpp")
target_include_directories(decal-proof-math PUBLIC
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/include"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/include/crimson/renderer/forward")
target_link_libraries(decal-proof-math PRIVATE glm::glm)
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
