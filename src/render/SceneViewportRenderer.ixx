module;

export module quader.render.scene_viewport_renderer;

export import quader.render.engine;
export import quader.render.types;
export import quader.scene.scene;

export namespace quader {

class ISceneViewportRenderer : public IRenderEngine {
public:
    virtual ~ISceneViewportRenderer() = default;

    [[nodiscard]] virtual bool request_scene_viewport_frame(
        const Scene& scene,
        const RenderCamera& camera,
        RenderExtent extent) = 0;
    [[nodiscard]] virtual const RenderViewportFrame* try_consume_scene_viewport_frame() = 0;
};

} // namespace quader
