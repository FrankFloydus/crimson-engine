module;

export module quader.render.debug_preview_renderer;

export import quader.render.engine;
export import quader.render.types;

export namespace quader {

class IDebugPreviewRenderer : public IRenderEngine {
public:
    virtual ~IDebugPreviewRenderer() = default;

    [[nodiscard]] virtual PbrMaterialSettings material_settings() const = 0;
    [[nodiscard]] virtual DirectionalLightSettings directional_light_settings() const = 0;

    virtual void set_material_settings(const PbrMaterialSettings& settings) = 0;
    virtual void set_directional_light_settings(const DirectionalLightSettings& settings) = 0;

    [[nodiscard]] virtual bool request_debug_preview(RenderExtent extent) = 0;
    [[nodiscard]] virtual const RenderPreviewFrame* try_consume_debug_preview_frame() = 0;
};

} // namespace quader
