# Tasks

- Re-evaluate whether `imgui.ini` should remain in the repo/worktree once Quader Native has an in-app user settings system. The current workflow intentionally keeps the generated ImGui docking/layout state for now; replace this with a proper user-settings storage path when that system exists.
- Replace the temporary CPU RGBA8 viewport readback path with GPU-native viewport texture presentation. The current transport lives behind `TemporaryRgba8ReadbackFrame`, `IViewportRenderer`, and `ViewportTextureStore` so the UI can already address textures by `ViewportId`, but Filament still renders each requested viewport to CPU memory before uploading it back to Vulkan for ImGui.
