#pragma once

#include "crimson-renderer/RenderTypes.h"
#include <array>
#include <filesystem>
#include <string>

namespace crimson {
inline constexpr std::array<int,6> BrowserPreviewSizes{16,32,64,128,256,512};
inline constexpr std::array<int,3> PointShadowResolutions{256,512,1024};
struct EditorPreferenceValues {
    ViewportAppearance appearance{};
    int browserPreviewSize{128};
    int pointShadowResolution{1024};
};
// Application preferences are independent from authored scenes and dock state.
class EditorPreferences {
public:
    explicit EditorPreferences(std::filesystem::path file = {});
    const EditorPreferenceValues& values() const { return values_; }
    bool setValues(const EditorPreferenceValues& values);
    bool setBrowserPreviewSize(int size);
    void open() { open_=true; focusRequested_=true; }
    bool isOpen() const { return open_; }
    void close() { open_=false; focusRequested_=false; }
    void draw();
    const std::string& status() const { return status_; }
    bool save();
    static int previewSizeIndex(int size);
private:
    std::filesystem::path file_;
    EditorPreferenceValues values_;
    bool open_{},focusRequested_{};
    std::string status_;
};
} // namespace crimson
