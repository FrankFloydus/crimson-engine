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
if /I "%~2"=="test" (
    ctest --preset windows-%CC_CONFIGURATION%
    if errorlevel 1 goto :failure
)
echo crimson-renderer ready: build\%CC_CONFIGURATION%\bin\crimson-renderer.exe
popd
exit /b 0

:failure
echo ERROR: Renderer build failed. Review the diagnostic above.
popd
exit /b 1
