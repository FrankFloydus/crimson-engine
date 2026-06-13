module;

#include <vector>

export module quader.platform.sdl_opengl_texture_allocator;

export import quader.platform.sdl_opengl_context;
export import quader.render.opengl_texture;

export namespace quader {

class SdlOpenGlTextureAllocator final : public IOpenGlTexture2DAllocator {
public:
    explicit SdlOpenGlTextureAllocator(SdlOpenGlContext& context);

    SdlOpenGlTextureAllocator(const SdlOpenGlTextureAllocator&) = delete;
    SdlOpenGlTextureAllocator& operator=(const SdlOpenGlTextureAllocator&) = delete;

    ~SdlOpenGlTextureAllocator() override;

    [[nodiscard]] OpenGlTexture2D create_color_texture(RenderExtent extent) override;
    void destroy_texture(OpenGlTexture2D texture) noexcept override;

    [[nodiscard]] bool has_retired_textures() const noexcept;
    void collect_retired_textures() noexcept;

private:
    SdlOpenGlContext& context_;
    uint64_t next_resource_id_ = 1;
    uint64_t next_generation_ = 1;
    std::vector<OpenGlTexture2D> ready_to_delete_;
    std::vector<OpenGlTexture2D> deferred_delete_;
};

} // namespace quader
