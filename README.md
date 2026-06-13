# Quader Native

Standalone C++20 modeling-suite shell using SDL3, Google Filament, and Dear ImGui docking.

## Build

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug
```

The preset uses vcpkg manifest mode and the Filament SDK at:

```text
C:\Users\Drako\Desktop\_QUADER_NATIVE\thirdy-party\filament-v1.71.5-windows
```

To verify project contracts without the Filament backend:

```powershell
cmake --preset vs2026-x64-no-filament
cmake --build --preset debug-no-filament --target quader_no_filament_contract_tests
ctest --test-dir .\out\build\vs2026-x64-no-filament -C Debug -L no_filament --output-on-failure
```

## Run

```powershell
.\out\build\vs2026-x64\Debug\quader_native.exe
```

For smoke tests, set `QUADER_AUTOCLOSE_MS` to close the app automatically:

```powershell
$env:QUADER_AUTOCLOSE_MS = "3000"
.\out\build\vs2026-x64\Debug\quader_native.exe
```
