set(VCPKG_LIBRARY_LINKAGE static)
vcpkg_download_distfile(GC_ARCHIVE
    URLS "https://codeload.github.com/nmwsharp/geometry-central/tar.gz/019669ddabda05e0f71fa3587cfb3c1dadf19cb8"
    FILENAME "geometry-central-019669ddabda05e0f71fa3587cfb3c1dadf19cb8.tar.gz"
    SHA512 d39c1b23e8fd2da1bb06a0741ad876ba04f0b11a953b73c5cf1dbee8d493a2f5472c4d7851809c809008215b640381f6259a6b6d7b368ee0ffaac49ee62fcd75)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${GC_ARCHIVE}" PATCHES no-configure-download.patch)
vcpkg_download_distfile(HAPPLY_ARCHIVE
    URLS "https://codeload.github.com/nmwsharp/happly/tar.gz/88f79725d32d78e0e637ebf42d8432d519e1c7d0"
    FILENAME "happly-88f79725d32d78e0e637ebf42d8432d519e1c7d0.tar.gz"
    SHA512 79f2be2d4f5583bc4ea63d92af142fc1a59c91610dee5d2bbbd3fbfb6ff6cd697cd6cc6d740ac3878278257f550c739832b2ecf10887b0607045e9f09a35c92c)
vcpkg_extract_source_archive(HAPPLY_PATH ARCHIVE "${HAPPLY_ARCHIVE}")
file(COPY "${HAPPLY_PATH}/happly.h" DESTINATION "${SOURCE_PATH}/deps/happly")
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS
    -DBUILD_SHARED_LIBS=OFF -DSUITESPARSE=OFF -DGC_ALWAYS_DOWNLOAD_EIGEN=OFF)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME GeometryCentral CONFIG_PATH lib/cmake/GeometryCentral)
vcpkg_copy_pdbs()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${HAPPLY_PATH}/LICENSE")
