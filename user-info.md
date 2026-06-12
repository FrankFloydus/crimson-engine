# Quader Native User Info

This file is the practical runbook for manually building, testing, profiling, and launching the engine.

## Paths

- Project root: `C:\Users\Drako\Desktop\_QUADER_NATIVE\quader-native`
- Filament SDK: `C:\Users\Drako\Desktop\_QUADER_NATIVE\thirdy-party\filament-v1.71.5-windows`
- vcpkg toolchain: `C:\Users\Drako\vcpkg\scripts\buildsystems\vcpkg.cmake`
- Build folder: `out\build\vs2026-x64`

## Manual Build

From the project root:

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
ctest --test-dir .\out\build\vs2026-x64 -C Debug --output-on-failure
.\out\build\vs2026-x64\Debug\quader_native.exe
```

For Release:

```powershell
cmake --build --preset release
ctest --test-dir .\out\build\vs2026-x64 -C Release --output-on-failure
.\out\build\vs2026-x64\Release\quader_native.exe
```

For smoke tests that close automatically:

```powershell
$env:QUADER_AUTOCLOSE_MS = "2000"
.\out\build\vs2026-x64\Debug\quader_native.exe
Remove-Item Env:\QUADER_AUTOCLOSE_MS -ErrorAction SilentlyContinue
```

## One-Click Build Scripts

Build Debug:

```powershell
.\tools\build-engine.ps1
```

Build Release:

```powershell
.\tools\build-engine.ps1 -Configuration Release
```

Build and run tests:

```powershell
.\tools\build-engine.ps1 -Configuration Debug -RunTests
```

Run the engine:

```powershell
.\tools\run-engine.ps1 -Configuration Debug
```

Run a two-second smoke launch:

```powershell
.\tools\run-engine.ps1 -Configuration Debug -AutoCloseMs 2000
```

## Tracy Profiling

The app is built with Tracy client instrumentation when `QUADER_ENABLE_TRACY=ON`. This is the default in CMake.

The vcpkg Tracy package installed for this project provides the Tracy client library and DLL. It does not currently place `tracy-profiler.exe` in the project build tree. Install or build the Tracy profiler GUI separately, then either put `tracy-profiler.exe` on `PATH` or pass its full path to the profiling script.

Manual Tracy build:

```powershell
cmake --preset vs2026-x64 -DQUADER_ENABLE_TRACY=ON
cmake --build --preset release
```

Launch Tracy and the engine when `tracy-profiler.exe` is on `PATH`:

```powershell
.\tools\profile-tracy.ps1
```

Launch Tracy from an explicit path:

```powershell
.\tools\profile-tracy.ps1 -TracyProfilerExe "C:\Tools\tracy\tracy-profiler.exe"
```

Launch a short profiling smoke run:

```powershell
.\tools\profile-tracy.ps1 -Configuration Release -AutoCloseMs 5000
```

## Script Contents

The repository includes these PowerShell helpers:

- `tools\build-engine.ps1`: configures and builds Debug or Release, optionally running CTest.
- `tools\run-engine.ps1`: launches a built engine binary, optionally with `QUADER_AUTOCLOSE_MS`.
- `tools\profile-tracy.ps1`: configures with Tracy enabled, builds, starts Tracy profiler if found, then launches the engine.

If PowerShell script execution is blocked on the machine, run:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

This changes policy only for the current PowerShell process.
