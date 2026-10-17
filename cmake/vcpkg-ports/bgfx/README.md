# Crimson bgfx overlay

This port is copied from `ports/bgfx` at the project's existing vcpkg builtin baseline,
`d0c191d142b7bc048141ddfb618aa9ae5dba0233`. It retains bgfx.cmake
`1.129.8940-496`, port version 1, the original archive SHA512, dependencies, features,
`fix-dependencies.patch`, and upstream copyright installation. The project manifest's
baseline and the default registry are unchanged.

The Crimson source patch `d3d12-texture-upload.patch` repairs two invalid D3D12
calls exposed by strict GPU validation during streamed texture mip updates:

- The temporary upload-region descriptor has one mip. Keeping the destination's
  complete mip count after reducing Width/Height produces an invalid descriptor
  for small regions.
- Unmap reports the staging buffer's actual byte extent. Row pitch includes
  alignment padding, while the final row need not reserve that trailing padding;
  reporting `numRows * rowPitch` can exceed the buffer allocation.

`d3d12-timer-fence.patch` corrects D3D12 GPU timestamp publication. In the pinned
`bgfx/src/renderer_d3d12.cpp`, `TimerQueryD3D12::end` records `EndQuery` and
`ResolveQueryData` into the current command list. `CommandQueueD3D12::kick` submits
that list, signals `m_currentFence`, then increments the fence value. The timer
therefore waits for `m_currentFence`, rather than the preceding list's fence,
before reading its persistently mapped results. Waiting for the preceding fence
can publish stale timestamps before the current resolve executes. This patch
changes timing readiness only; it adds no render synchronization or threading.

The original MIT-licensed vcpkg recipe and dependency patch are covered by
`VCPKG-LICENSE.txt`. The port preserves the upstream bgfx/bundled-component
BSD-2-Clause/CC0 copyright packaging via `vcpkg_install_copyright`.

API references:

- [GetCopyableFootprints](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints)
- [D3D12_RESOURCE_DESC mip rules](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_resource_desc)
- [Unmap written-range contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-unmap)

Use the project build/configure wrapper to rebuild the overlay dependency. Do not
patch extracted sources or installed binaries under `build/`.
