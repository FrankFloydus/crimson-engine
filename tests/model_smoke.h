#pragma once

#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "model_fixtures.h"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace crimson {

// Native integration checks explicitly restricted by App to a disposable root.
inline bool sameModelSmokeMaterial(const Material& a, const Material& b) {
    return a.baseColor == b.baseColor && a.metallic == b.metallic && a.roughness == b.roughness &&
        a.normalStrength == b.normalStrength && a.albedoMap == b.albedoMap && a.roughnessMap == b.roughnessMap &&
        a.metallicMap == b.metallicMap && a.normalMap == b.normalMap;
}
inline std::vector<std::string> verifyModelAssets(AssetLibrary& assets, SceneSettings& settings, const std::filesystem::path& root) {
    const auto require = [](bool value, const std::string& message) {
        if (!value) throw std::runtime_error("Model smoke: " + message);
    };
    const auto fileText = [](const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file),{});
    };
    const auto sourceFolder = root / "model_smoke_sources";
    const auto fixtures = model_test::writeModelFixtures(sourceFolder);
    const auto uppercaseModel = sourceFolder / "Mixed Hierarchy.GLTF";
    const auto uppercaseImage = sourceFolder / "Mixed Checker.PNG";
    std::filesystem::copy_file(fixtures.gltf,uppercaseModel,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(fixtures.image,uppercaseImage,std::filesystem::copy_options::overwrite_existing);
    const auto badModel = sourceFolder / "invalid.gltf";
    { std::ofstream file(badModel); file << "{\"asset\":{\"version\":\"2.0\"}}"; }
    Material savedSphere;
    savedSphere.baseColor = {.23f,.41f,.67f}; savedSphere.roughness = .63f;
    savedSphere.metallic = .17f; savedSphere.normalStrength = 1.3f;
    assets.saveMaterial(savedSphere);
    const auto materialFile = root / "assets" / "material.json";
    const auto originalMaterial = fileText(materialFile);
    const size_t imported = assets.importFiles({uppercaseImage,badModel,uppercaseModel,fixtures.glb});
    require(imported == 3,"mixed batch failed: " + assets.status());
    require(assets.status().find("invalid.gltf") != std::string::npos,"batch discarded per-file failure diagnostics");
    require(fileText(materialFile) == originalMaterial,"model import changed the sphere material");

    std::string gltfId, glbId, imageId;
    for (const auto& entry : assets.catalog()) {
        if (entry.kind == AssetKind::Model) {
            const auto model = assets.findModel(entry.id);
            require(model && model->bounds.valid && !model->draws.empty(),"catalog published an invalid model");
            if (model->format == "GLB") glbId = entry.id; else gltfId = entry.id;
        } else if (entry.name == "Mixed Checker.PNG") imageId = entry.id;
    }
    require(!gltfId.empty() && !glbId.empty() && !imageId.empty(),"unified catalog lost a supported asset kind");
    const auto model = assets.findModel(gltfId);
    require(model->draws.size() == 4 && model->primitives.size() == 2,"hierarchy or shared mesh references lost");
    const auto bindings = assets.modelBindings(*model);
    require(bindings.size() == model->materials.size() && bgfx::isValid(bindings[0].handles[0]) &&
        bgfx::isValid(bindings[0].handles[1]) && bgfx::isValid(bindings[0].handles[2]),"model image upload/bindings failed");
    require(bindings[0].handles[1].idx == bindings[0].handles[3].idx,"packed AO/MR image did not share its linear GPU texture");
    Material assigned = savedSphere;
    require(assets.assign(assigned,MaterialSlot::Albedo,imageId),"existing image assignment failed");
    require(bgfx::isValid(assets.bindings(assigned).albedo),"assigned image did not upload");
    assets.clear(assigned,MaterialSlot::Albedo);
    assets.saveMaterial(savedSphere);
    require(fileText(materialFile) == originalMaterial,"texture compatibility check failed to restore saved material");
    savedSphere.albedoMap = imageId;
    assets.saveMaterial(savedSphere);
    settings.material = savedSphere;

    // Only this generated subdirectory is removed, never an authored/source folder.
    const auto relative = std::filesystem::weakly_canonical(sourceFolder).lexically_relative(std::filesystem::weakly_canonical(root));
    require(relative == std::filesystem::path("model_smoke_sources"),"fixture cleanup escaped the disposable project");
    std::filesystem::remove_all(sourceFolder);
    const auto restoredGltf = loadModelPackage(resolveAssetPath(root,gltfId),root);
    const auto restoredGlb = loadModelPackage(resolveAssetPath(root,glbId),root);
    require(restoredGltf->draws.size() == model->draws.size() && restoredGlb->bounds.valid,"managed reopen required original sources");
    // Finish with the multi-node hierarchy visible in the native capture.
    return {glbId,gltfId};
}

} // namespace crimson
