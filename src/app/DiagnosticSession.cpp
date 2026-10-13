#include "DiagnosticSession.h"
#include "crimson-renderer/Diagnostics.h"
#include "tools/Tools.h"
#include "ibl_smoke.h"
#include "model_smoke.h"
#include <imgui.h>
#include <lodepng.h>
#include <array>
#include <stdexcept>
namespace crimson {
namespace {
void importSmokeTextures(AssetLibrary& assets, Material& material, const std::filesystem::path& root) {
    const auto folder = root / "build" / "smoke_sources";
    std::filesystem::create_directories(folder);
    const std::array<const char*, 3> names{"Prototype Checker.png", "Roughness Ramp.png", "Metalness Stripes.png"};
    for (size_t slot = 0; slot < names.size(); ++slot) {
    std::vector<uint8_t> pixels(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x) {
    const size_t offset = (y * 64 + x) * 4;
    const bool checker = ((x / 8 + y / 8) % 2) != 0;
    if (slot == 0) {
        pixels[offset] = checker ? 210 : 240;
        pixels[offset + 1] = checker ? 28 : 215;
        pixels[offset + 2] = checker ? 20 : 175;
    } else {
        const auto value = static_cast<uint8_t>(slot == 1 ? 40 + x * 3 : checker ? 255 : 0);
        pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = value;
    }
    pixels[offset + 3] = 255;
    }
    std::vector<uint8_t> png;
    const auto error = lodepng::encode(png, pixels, 64, 64);
    if (error) throw std::runtime_error("Could not create the texture smoke fixture.");
    const auto path = folder / names[slot];
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    output.close();
    const auto* imported = assets.importTexture(path);
    if (!imported) throw std::runtime_error(assets.status());
    const auto kind = slot == 0 ? MaterialSlot::Albedo : slot == 1 ? MaterialSlot::Roughness : MaterialSlot::Metallic;
    if (!assets.assign(material, kind, imported->id)) throw std::runtime_error("Could not assign the smoke texture.");
    }
}
}
DiagnosticSession::DiagnosticSession(const AppOptions& options,const std::filesystem::path& root,AssetLibrary& assets,Renderer& renderer,Diagnostics& diagnostics) : root_(root) {
    settings_ = diagnosticSettings(options.scenario);
    if (options.scenario == "default" || options.scenario == "ibl-reopen" || options.scenario.starts_with("model-")) assets.loadMaterial(settings_.material);
    if (options.scenario == "default" || options.scenario.starts_with("ibl-") || options.scenario.starts_with("model-")) assets.loadEnvironment(settings_.environment);
    if (options.editorSmoke) importSmokeTextures(assets, settings_.material, root);
    if (options.iblSmoke) {
    verifyIblAssets(assets, settings_, root);
    diagnostics.log("PASS: Normal/HDR import, material factors, assignments, legacy loading and persistence.");
    }
    if (options.scenario == "ibl-normal" || options.scenario == "ibl-zero-normal" || options.scenario == "ibl-extreme")
    settings_.material.normalMap = "assets/textures/IBL Detail Normal.png";
    if (options.scenario == "ibl-neutral") settings_.material.normalMap = "assets/textures/IBL Neutral Normal.png";
    if (options.scenario == "ibl-zero-normal") settings_.material.normalStrength = 0;
    if (options.scenario == "ibl-extreme") settings_.environment.intensity = 10;
    if (!renderer.prepareEnvironment(root, {})) throw std::runtime_error(renderer.environmentStatus());
    if (!settings_.environment.map.empty()) {
    const bool prepared = renderer.prepareEnvironment(root, settings_.environment.map);
    if (options.iblSmoke && !prepared) throw std::runtime_error(renderer.environmentStatus());
    }
    if (options.iblSmoke) {
    if (renderer.prepareEnvironment(root, "assets/textures/does-not-exist.hdr"))
        throw std::runtime_error("Missing environment unexpectedly prepared.");
    if (!renderer.prepareEnvironment(root, settings_.environment.map) || renderer.environmentStatus().starts_with("Environment:"))
        throw std::runtime_error("Environment failed to recover after a rejected source.");
    diagnostics.log("PASS: Rejected environment source retained working resources and recovered its status.");
    }
    preparedEnvironment_ = settings_.environment.map;
    diagnostics.log(renderer.environmentStatus());




    if (options.scenario == "default" || options.scenario == "model-reopen") {
    const auto saved = assets.loadPreview();
    if (!saved.empty() && !showModel(assets,renderer,saved,false) && options.scenario == "model-reopen")
        throw std::runtime_error(previewStatus_);
    if (options.scenario == "model-reopen" && !preview_) throw std::runtime_error("No saved model preview to reopen.");
    }
    if (options.scenario == "model-sphere") {
    showSphere(assets,renderer,true);
    if (!assets.loadPreview().empty()) throw std::runtime_error("Return-to-sphere did not clear preview persistence.");
    diagnostics.log("PASS: Returned to the saved sphere material and cleared model preview.");
    }
    if (options.modelSmoke) {
    const auto models = verifyModelAssets(assets,settings_,root);
    const Material savedSphere = settings_.material;
    if (models.size() < 2 || !showModel(assets,renderer,models[0],true) || !showModel(assets,renderer,models[1],true))
        throw std::runtime_error("Model smoke could not prepare and replace both model formats: " + previewStatus_);
    const auto retained = preview_->asset;
    if (showModel(assets,renderer,"assets/models/missing/manifest.json",true) || !preview_ || preview_->asset != retained || assets.loadPreview() != retained->id)
        throw std::runtime_error("Failed preview replacement did not preserve the active model.");
    showSphere(assets,renderer,true);
    if (!assets.loadPreview().empty()) throw std::runtime_error("Sphere action did not persist.");
    Material persistedSphere; assets.loadMaterial(persistedSphere);
    if (!sameModelSmokeMaterial(savedSphere,settings_.material) || !sameModelSmokeMaterial(savedSphere,persistedSphere))
        throw std::runtime_error("Model preview/reset changed the saved sphere factors or texture assignment.");
    if (!showModel(assets,renderer,models[1],true)) throw std::runtime_error(previewStatus_);
    diagnostics.log("PASS: Mixed asset imports, model replacement, failed-drop retention and preview persistence.");
    }
    if (options.scenario == "model-reopen") diagnostics.log("PASS: Managed model preview reopened without original source files.");
}
bool DiagnosticSession::showModel(AssetLibrary& assets,Renderer& renderer,const std::string& id,bool persist) {

    try {
        auto candidate = assets.findModel(id);
        if (!candidate) throw std::runtime_error("Model unavailable: " + id);
        // Texture and mesh preparation both finish before changing the visible instance.
        auto bindings = assets.modelBindings(*candidate);
        if (!renderer.prepareModel(*candidate)) throw std::runtime_error(renderer.modelStatus());

        preview_ = ModelInstance{std::move(candidate), ModelIdentity};
        modelTextures_ = std::move(bindings);
        previewStatus_ = renderer.modelStatus();
        framePreview_ = true;
        if (persist && !assets.savePreview(id)) previewStatus_ = assets.status();

        return true;
    } catch (const std::exception& error) {
        previewStatus_ = error.what();

        return false;
    }
    }
void DiagnosticSession::showSphere(AssetLibrary& assets,Renderer& renderer,bool persist) {


    preview_.reset(); modelTextures_.clear(); renderer.clearModel();
    settings_.camera = Camera{};
    framePreview_ = false;
    previewStatus_.clear();
    if (persist && !assets.savePreview({})) previewStatus_ = assets.status();
    }
void DiagnosticSession::frame(AssetLibrary& assets,Renderer& renderer,Navigation& navigation,SDL_Window* window) {
    const auto editor = drawEditor(settings_, renderer, assets, window, preview_ ? &*preview_ : nullptr, previewStatus_);
    if (editor.showSphere) showSphere(assets,renderer,true);
    if (!editor.modelDropId.empty()) showModel(assets,renderer,editor.modelDropId,true);
    framePreview_ |= editor.frameModel;
    if (framePreview_ && preview_ && editor.viewport.width > 0 && editor.viewport.height > 0) {
        const auto& bounds = preview_->asset->bounds;
        if (!bounds.valid || !settings_.camera.frameBounds(bounds.minimum,bounds.maximum,editor.viewport.width/editor.viewport.height))
            previewStatus_ = "Cannot frame the model: invalid bounds.";
        framePreview_ = false;
    }
    navigation.update(settings_.camera, editor.viewport, ImGui::GetIO().DeltaTime, !preview_);
    if (editor.materialChanged) assets.saveMaterial(settings_.material);
    if (editor.environmentChanged) assets.saveEnvironment(settings_.environment);
    if (preparedEnvironment_ != settings_.environment.map) {
        renderer.prepareEnvironment(root_, settings_.environment.map);
        preparedEnvironment_ = settings_.environment.map;

    }

    if (editor.viewport.width > 0 && editor.viewport.height > 0)
        renderer.render(RenderScene{settings_, preview_ ? MaterialTextures{} : assets.bindings(settings_.material),
            preview_ ? &*preview_ : nullptr, preview_ ? &modelTextures_ : nullptr});
}
}
