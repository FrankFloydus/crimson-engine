#pragma once
#include <functional>
#include <memory>
#include <crimson/renderer/AntiAliasing.hpp>
namespace quader {class RenderHost;}
namespace crimson::integration {
class AssetDocuments;
class LevelSession;
// Specialized transient previews remain children of the canonical native
// asset-document windows. GPU execution belongs to their shared RenderHost.
class LevelDocumentPreviews final {
public:
    LevelDocumentPreviews(AssetDocuments&,LevelSession&,quader::RenderHost&,std::function<void()> requestFrame);
    ~LevelDocumentPreviews();
    void update(double seconds,bool active);
    void setAntiAliasing(AntiAliasing);
    bool continuous()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
