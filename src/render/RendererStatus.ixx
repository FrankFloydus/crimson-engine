module;

#include <string>

export module quader.render.status;

export namespace quader {

struct RendererStatus {
    bool initialized = false;
    std::string backend_name = "Unavailable";
    std::string message = "Renderer has not been initialized.";
};

} // namespace quader
