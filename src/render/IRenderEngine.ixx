module;

export module quader.render.engine;

export import quader.render.capabilities;
export import quader.render.status;

export namespace quader {

class IRenderEngine {
public:
    virtual ~IRenderEngine() = default;

    [[nodiscard]] virtual RendererStatus status() const = 0;
    [[nodiscard]] virtual RendererCapabilities capabilities() const = 0;
};

} // namespace quader
