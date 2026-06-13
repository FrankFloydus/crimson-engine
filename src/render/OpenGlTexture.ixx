module;

#include <cstdint>

export module quader.render.opengl_texture;

export import quader.render.viewport_types;

export namespace quader {

enum class OpenGlSharedContextApi {
    none,
    windows_wgl,
};

struct OpenGlSharedContextHandle {
    OpenGlSharedContextApi api = OpenGlSharedContextApi::none;
    void* native_handle = nullptr;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return api != OpenGlSharedContextApi::none && native_handle != nullptr;
    }
};

struct OpenGlTexture2D {
    uint32_t id = 0;
    uint64_t resource_id = 0;
    uint64_t generation = 0;
    RenderExtent extent;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return id != 0 && resource_id != 0 && generation != 0 && extent.is_valid();
    }
};

class IOpenGlTexture2DAllocator {
public:
    IOpenGlTexture2DAllocator() = default;
    IOpenGlTexture2DAllocator(const IOpenGlTexture2DAllocator&) = delete;
    IOpenGlTexture2DAllocator& operator=(const IOpenGlTexture2DAllocator&) = delete;

    virtual ~IOpenGlTexture2DAllocator() = default;

    [[nodiscard]] virtual OpenGlTexture2D create_color_texture(RenderExtent extent) = 0;
    virtual void destroy_texture(OpenGlTexture2D texture) noexcept = 0;
};

} // namespace quader
