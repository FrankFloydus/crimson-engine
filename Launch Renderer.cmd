@echo off
setlocal
pushd "%~dp0"
set "CC_CONFIGURATION=Release"
if not "%~1"=="" set "CC_CONFIGURATION=%~1"
if /I "%CC_CONFIGURATION%"=="Debug" (set "CC_CONFIGURATION=Debug") else if /I "%CC_CONFIGURATION%"=="Release" (set "CC_CONFIGURATION=Release") else (
    echo Usage: "Launch Renderer.cmd" [Debug^|Release]
    pause
    popd
    exit /b 2
)
if not exist "%~dp0build\%CC_CONFIGURATION%\bin\crimson-renderer.exe" (
    call "%~dp0Build.cmd" %CC_CONFIGURATION%
    if errorlevel 1 (
        pause
        popd
        exit /b 1
    )
)
start "" "%~dp0build\%CC_CONFIGURATION%\bin\crimson-renderer.exe"
popd
exit /b 0
