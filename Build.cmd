@echo off
setlocal
pushd "%~dp0"
if not defined VCPKG_ROOT set "VCPKG_ROOT=%USERPROFILE%\vcpkg"
set "CRIMSON_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%I in (`"%CRIMSON_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CRIMSON_VS=%%I"
if not defined CRIMSON_VS goto :failure
call "%CRIMSON_VS%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=amd64 -host_arch=amd64
if errorlevel 1 goto :failure
if exist "%CRIMSON_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%CRIMSON_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if not exist "build\Release\build.ninja" (
  cmake --preset windows-Release
  if errorlevel 1 goto :failure
)
cmake --build --preset windows-Release
if errorlevel 1 goto :failure
echo crimson-renderer ready: build\Release\crimson-renderer.exe
popd
exit /b 0
:failure
echo ERROR: Crimson Engine build failed. Review diagnostics above.
popd
exit /b 1
