#pragma once
#include "render/crimson/crimson_scene_projection.hpp"
#include "scene/components.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <crimson/renderer/Renderer.hpp>
#include <filesystem>
#include <memory>
#include <string>

namespace quader {
struct CrimsonRichWorld;
struct CrimsonRichFrame final {
    crimson::SceneSettings settings;
    std::uint64_t source_generation{},camera_id{};
    std::shared_ptr<const crimson::ModelingPresentation> presentation;
    [[nodiscard]] crimson::RenderScene scene()const;
private:
    friend class CrimsonRichScene;
    std::shared_ptr<const CrimsonRichWorld> world_;
};

// Resource preparation uses the retained Engine worker/cache owner. Geometry
// jobs have no GPU or document access; the GUI publishes original LODs and
// immutable material packets before the host preflights its shared submission.
class CrimsonRichScene final {
public:
    CrimsonRichScene(crimson::shared::Renderer&,std::filesystem::path resources);
    ~CrimsonRichScene();
    bool prepare(std::shared_ptr<const CrimsonSceneProjection>);
    bool prepare_environment(const std::string& id);
    std::shared_ptr<const CrimsonRichFrame> view(const SceneSettings&,
        const ViewportCameraState&,const ViewportCameraPose&,crimson::AntiAliasing,std::uint64_t camera_id,
        const crimson::ModelingPresentation& controls={},bool scene_lighting=true);
    [[nodiscard]] bool pending()const;
    [[nodiscard]] const std::string& status()const;
    void clear();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
