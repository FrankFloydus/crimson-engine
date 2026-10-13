#pragma once
#include "App.h"
#include "assets/AssetLibrary.h"
#include "input/Navigation.h"
#include <optional>
namespace crimson {
class Diagnostics;
SceneSettings diagnosticSettings(const std::string& scenario);
// Legacy manual diagnostics kept separate from authoring and the application loop.
class DiagnosticSession {
public:
    DiagnosticSession(const AppOptions&,const std::filesystem::path&,AssetLibrary&,Renderer&,Diagnostics&);
    void frame(AssetLibrary&,Renderer&,Navigation&,SDL_Window*);
private:
    std::filesystem::path root_;
    SceneSettings settings_;
    std::optional<ModelInstance> preview_;
    std::vector<ModelMaterialTextures> modelTextures_;
    std::string previewStatus_,preparedEnvironment_;
    bool framePreview_{};
    bool showModel(AssetLibrary&,Renderer&,const std::string&,bool);
    void showSphere(AssetLibrary&,Renderer&,bool);
};
}
