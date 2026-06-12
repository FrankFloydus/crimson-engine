module;

#include <optional>

export module quader.render.viewport_renderer;

export import quader.render.engine;
export import quader.render.viewport_types;

export namespace quader {

class IViewportRenderer : public IRenderEngine {
public:
    ~IViewportRenderer() override = default;

    [[nodiscard]] virtual std::optional<ViewportId> try_create_viewport() = 0;
    [[nodiscard]] virtual ViewportId create_viewport()
    {
        return try_create_viewport().value_or(ViewportId {});
    }
    virtual void release_viewport(ViewportId viewport_id) = 0;
    [[nodiscard]] virtual bool request_viewport_frame(const ViewportRenderRequest& request) = 0;
    [[nodiscard]] virtual std::optional<ViewportRenderFrame> try_consume_viewport_frame(ViewportId viewport_id) = 0;
};

} // namespace quader
