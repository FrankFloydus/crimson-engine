#pragma once
#include "ResourceManager.h"
#include "TextureMipCache.h"
#include <bgfx/bgfx.h>
namespace crimson {
struct PreparedTexture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    std::uint32_t width{},height{};
    std::uint64_t signature{},sourceFingerprint{};
    std::shared_ptr<const void> lease;
    std::uint32_t sourceWidth{},sourceHeight{};
    std::string diagnostic;
    bool complete{false};
    bool sourceReady{false};
};
// Owned by AssetLibrary. All calls and every GPU destructor run on the application thread.
class TextureStreamer {
public:
    TextureStreamer(ResourceManager&,std::filesystem::path projectRoot);
    ~TextureStreamer();
    PreparedTexture request(const std::string& id,const std::filesystem::path& source,
        streaming::TextureRole role,bool mipmapped,std::uint32_t edge,
        ResourcePriority priority=ResourcePriority::Visible,bool thumbnail=false);
    // Content-keyed generated linear RGBA8 pixels, independent of managed asset
    // identities/files. Thumbnail views encode RGB for direct UI display.
    PreparedTexture requestGenerated(const std::string& key,std::function<DecodedImage()> generator,
        std::uint32_t sourceEdge,bool mipmapped,std::uint32_t edge,
        ResourcePriority priority=ResourcePriority::Visible,bool thumbnail=false);
    void beginFrame();
    void advance(std::size_t bytes=4*1024*1024,double milliseconds=2);
    void invalidate(const std::string& id);
    void reload(const std::string& id);
    void trim(std::size_t targetBytes);
    std::size_t residentBytes() const;
    std::string diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
