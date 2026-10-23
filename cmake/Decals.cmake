if(NOT DEFINED CC_DECAL_ROOT)
    set(CC_DECAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
add_library(crimson-decals
    "${CC_DECAL_ROOT}/src/decals/DecalSurface.cpp"
    "${CC_DECAL_ROOT}/src/decals/DecalTopology.cpp"
    "${CC_DECAL_ROOT}/src/decals/DecalChart.cpp"
    "${CC_DECAL_ROOT}/src/decals/DecalResolve.cpp"
    "${CC_DECAL_ROOT}/src/decals/DecalCache.cpp")
target_include_directories(crimson-decals PUBLIC "${CC_DECAL_ROOT}/src"
    "${CC_DECAL_ROOT}/third_party/crimson-renderer/include")
target_link_libraries(crimson-decals PRIVATE geometry-central::geometry-central)
target_compile_features(crimson-decals PUBLIC cxx_std_20)
target_compile_definitions(crimson-decals PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(crimson-decals PRIVATE /W4 /permissive- /utf-8)
