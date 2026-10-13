@echo off
setlocal
pushd "%~dp0"
call "%~dp0Build.cmd" Debug
if errorlevel 1 (
    pause
    popd
    exit /b 1
)
start "" "%~dp0build\Debug\bin\crimson-renderer.exe"
popd
exit /b 0
