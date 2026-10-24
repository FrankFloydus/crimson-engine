@echo off
setlocal
pushd "%~dp0"
if not defined VCPKG_ROOT set "VCPKG_ROOT=%USERPROFILE%\vcpkg"
set "CC_VCPKG_ROOT=%VCPKG_ROOT%"
set "CC_CONFIGURATION=Release"
if not "%~1"=="" set "CC_CONFIGURATION=%~1"
if /I "%CC_CONFIGURATION%"=="Debug" (set "CC_CONFIGURATION=Debug") else if /I "%CC_CONFIGURATION%"=="Release" (set "CC_CONFIGURATION=Release") else (
    echo Usage: Build.cmd [Debug^|Release]
    popd
    exit /b 2
)

set "CC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CC_VSWHERE%" (
    echo ERROR: Visual Studio with Desktop development with C++ is required.
    goto :failure
)
for /f "usebackq tokens=*" %%I in (`"%CC_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CC_VS=%%I"
if not defined CC_VS (
    echo ERROR: No MSVC x64 installation was found.
    goto :failure
)
call "%CC_VS%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=amd64 -host_arch=amd64
if errorlevel 1 goto :failure
set "VCPKG_ROOT=%CC_VCPKG_ROOT%"
if exist "%CC_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%CC_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: Set VCPKG_ROOT to your bootstrapped vcpkg installation.
    goto :failure
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake 3.25 or newer is required.
    goto :failure
)

if /I "%~2"=="decal-proof" goto :decal_proof
if /I "%~2"=="decal-host-proof" goto :decal_host_proof
if /I "%~2"=="decal-compile-proof" goto :decal_compile_proof
if /I "%~2"=="ragdoll-proof" goto :ragdoll_proof

if not exist "build\%CC_CONFIGURATION%\build.ninja" (
    cmake --preset windows-%CC_CONFIGURATION%
    if errorlevel 1 goto :failure
)
if /I "%~2"=="configure" (
    cmake --preset windows-%CC_CONFIGURATION%
    if errorlevel 1 goto :failure
)
cmake --build --preset windows-%CC_CONFIGURATION%
if errorlevel 1 goto :failure
if not errorlevel 0 goto :failure
if /I "%CC_CONFIGURATION%"=="Release" (
    "build\%CC_CONFIGURATION%\bin\crimson-material-cache.exe" --project-root "%~dp0." --renderer-bin "%~dp0build\%CC_CONFIGURATION%\bin" --report "%~dp0build\%CC_CONFIGURATION%\material-cache-preparation.json"
    if errorlevel 1 goto :failure
)
if /I "%~2"=="test" (
    ctest --preset windows-%CC_CONFIGURATION%
    if errorlevel 1 goto :failure
    if not errorlevel 0 goto :failure
)
echo crimson-renderer ready: build\%CC_CONFIGURATION%\bin\crimson-renderer.exe
popd
exit /b 0

:decal_proof
cmake -S cmake/DecalProof -B build/DecalProof-%CC_CONFIGURATION% -G Ninja -DCMAKE_BUILD_TYPE=%CC_CONFIGURATION% -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_HOST_TRIPLET=x64-windows -DVCPKG_DOWNLOADS="%~dp0build/vcpkg_downloads" -DVCPKG_BINARY_SOURCES="clear;files,%LOCALAPPDATA%/vcpkg/archives,read;files,%~dp0build/vcpkg_cache,readwrite" -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=%~dp0build/decal-vcpkg-buildtrees;--x-packages-root=%~dp0build/decal-vcpkg-packages"
if errorlevel 1 goto :failure
cmake --build build/DecalProof-%CC_CONFIGURATION% --target decal_proofs --parallel 8
if errorlevel 1 goto :failure
ctest --test-dir build/DecalProof-%CC_CONFIGURATION% -R "^decal_.*_core$" --output-on-failure
if errorlevel 1 goto :failure
echo Decal CPU proof passed. No renderer executable was built or launched.
popd
exit /b 0

:decal_host_proof
cmake --preset windows-%CC_CONFIGURATION%
if errorlevel 1 goto :failure
cmake --build --preset windows-%CC_CONFIGURATION% --target decal_surface_tests decal_model_surface_tests decal_quader_tests decal_asset_tests decal_resolve_tests decal_cache_tests decal_viewport_tests asset_library_deletion_tests preview_dependencies_tests --parallel 8
if errorlevel 1 goto :failure
ctest --preset windows-%CC_CONFIGURATION% -R "^(decal_(surface|model_surface|quader|asset|resolve|cache|viewport|asset_library)_core|asset_library_deletion_core|preview_dependencies_core)$" --output-on-failure
if errorlevel 1 goto :failure
echo Decal host CPU proofs passed. No renderer executable was built or launched.
popd
exit /b 0

:failure
echo ERROR: Renderer build failed. Review the diagnostic above.
popd
exit /b 1

:ragdoll_proof
cmake --preset windows-%CC_CONFIGURATION%
if errorlevel 1 goto :failure
cmake --build --preset windows-%CC_CONFIGURATION% --target ragdoll_asset_tests ragdoll_document_tests ragdoll_physics_tests ragdoll_runtime_tests ragdoll_scene_tests ragdoll_library_tests ragdoll_preview_plan_tests --parallel 8
if errorlevel 1 goto :failure
ctest --preset windows-%CC_CONFIGURATION% -R "^ragdoll_.*_core$" --output-on-failure
if errorlevel 1 goto :failure
echo Ragdoll CPU proofs passed. No renderer executable was linked or launched.
popd
exit /b 0

:decal_compile_proof
cmake --preset windows-%CC_CONFIGURATION%
if errorlevel 1 goto :failure
cmake --build --preset windows-%CC_CONFIGURATION% --target decal_host_compile --parallel 8
if errorlevel 1 goto :failure
echo Decal host compile checks passed. No renderer executable was linked or launched.
popd
exit /b 0
