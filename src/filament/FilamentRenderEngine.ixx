module;
#include <memory>
#include <optional>

export module quader.filament.render_engine;

export import quader.render.engine;
export import quader.render.viewport_renderer;

export namespace quader {

class FilamentRenderEngine final : public IViewportRenderer {
public:
    FilamentRenderEngine();

    FilamentRenderEngine(const FilamentRenderEngine&) = delete;
    FilamentRenderEngine& operator=(const FilamentRenderEngine&) = delete;

    ~FilamentRenderEngine() override;

    [[nodiscard]] RendererStatus status() const override;

    [[nodiscard]] std::optional<ViewportId> try_create_viewport() override;
    void release_viewport(ViewportId viewport_id) override;
    [[nodiscard]] bool request_viewport_frame(const ViewportRenderRequest& request) override;
    [[nodiscard]] std::optional<ViewportRenderFrame> try_consume_viewport_frame(ViewportId viewport_id) override;

private:
    struct Resources;

    std::unique_ptr<Resources> resources_;
    RendererStatus status_;
};

} // namespace quader
