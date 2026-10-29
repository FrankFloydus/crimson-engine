@echo off
setlocal
set "CC_TRACY_CONFIGURATION=%~1"
set "CC_TRACY_PYTHON=%~2"
if not defined CC_TRACY_CONFIGURATION set "CC_TRACY_CONFIGURATION=Release"
if not defined CC_TRACY_PYTHON set "CC_TRACY_PYTHON=python"
set "CC_TRACY_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%CC_TRACY_VSWHERE%" exit /b 2
for /f "usebackq tokens=*" %%I in (`"%CC_TRACY_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CC_TRACY_VS=%%I"
if not defined CC_TRACY_VS exit /b 2
call "%CC_TRACY_VS%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
if exist "%CC_TRACY_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%CC_TRACY_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\Prepare-Tracy.ps1" -Configuration "%CC_TRACY_CONFIGURATION%" -PythonExecutable "%CC_TRACY_PYTHON%" -Offline -Build
exit /b %ERRORLEVEL%
