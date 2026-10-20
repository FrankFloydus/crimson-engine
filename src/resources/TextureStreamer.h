#pragma once
#include "ResourceManager.h"
#include "TextureMipCache.h"
#include <bgfx/bgfx.h>
#include <optional>
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
    bool failed{false};
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
    PreparedTexture requestGeneratedFloat(const std::string& key,std::function<FloatImage()> generator,
        std::uint32_t width,std::uint32_t height,bool mipmapped,std::uint32_t edge,
        ResourcePriority priority=ResourcePriority::Visible);
    PreparedTexture requestGeneratedFloat(const std::string& key,std::function<FloatImage(std::stop_token)> generator,
        std::uint32_t width,std::uint32_t height,bool mipmapped,std::uint32_t edge,
        ResourcePriority priority=ResourcePriority::Visible);
    // Demand an existing immutable generated generation without retaining or
    // decoding its original inputs. Empty means a CPU bake is required.
    std::optional<PreparedTexture> cachedGeneratedFloat(const std::string& key,std::uint32_t width,std::uint32_t height,
        bool mipmapped,std::uint32_t edge,ResourcePriority priority=ResourcePriority::Visible);
    // Ready immutable CPU pixels for an already demanded generated image. This
    // neither starts work nor reads back a GPU texture; an absent descriptor is
    // expected after the source's CPU demand has expired.
    ResourceSnapshot<streaming::TextureMipCache> generatedFloatSnapshot(const std::string& key,
        std::uint32_t width,std::uint32_t height,bool mipmapped) const;
    void beginFrame();
    void advance(std::size_t bytes=4*1024*1024,double milliseconds=2);
    void invalidate(const std::string& id);
    void reload(const std::string& id);
    // Forget a transient generated recipe; outstanding UI/draw leases remain valid.
    void forgetGenerated(const std::string& key);
    void trim(std::size_t targetBytes);
    std::size_t residentBytes() const;
    std::string diagnostic() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
