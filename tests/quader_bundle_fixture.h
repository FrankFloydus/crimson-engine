#pragma once

#include "assets/AtomicFile.h"
#include "assets/QuaderSceneBundle.h"
#include "assets/MaterialAsset.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cstring>

namespace crimson::bundle_test {
struct Fixture {
    QuaderSceneBundle bundle;
    std::filesystem::path file;
    std::string modelId;
};
inline Fixture writeFixture(const std::filesystem::path& root) {
    using Json=nlohmann::json;
    initializeContentStorage(root);
    Fixture fixture;
    auto& bundle=fixture.bundle;
    bundle.projectId="37ffdfd0-c137-5d33-8630-e4273d8a1c3a";
    bundle.sceneId="c78263a5-e163-5795-bc27-6c294ab722d6";
    bundle.id=quaderSceneBundleId(bundle.projectId,bundle.sceneId);bundle.name="Bundle fixture";
    const std::string legacy="assets/models/quader-bundle-fixture/manifest.json";
    QuaderBundleModel model;model.id=canonicalAssetId(legacy);model.legacyManifest=legacy;
    const auto base=std::filesystem::path(legacy).parent_path().generic_string();
    model.memberIds={model.id,canonicalAssetId(base+"/model.gltf"),canonicalAssetId(base+"/source.gltf"),canonicalAssetId(base+"/buffers/0.bin")};
    const std::array<float,9> positions{0,0,0,1,0,0,0,1,0},normals{0,0,1,0,0,1,0,0,1};
    const std::array<float,6> uv{0,0,1,0,0,1};const std::array<std::uint16_t,3> indices{0,1,2};
    const auto append=[&](const auto& values){model.buffer.append(reinterpret_cast<const char*>(values.data()),sizeof(values));};
    append(positions);append(normals);append(uv);append(indices);
    const Json document{
        {"asset",{{"version","2.0"}}},{"buffers",Json::array({{{"uri","buffers/0.bin"},{"byteLength",model.buffer.size()}}})},
        {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",36}},
            {{"buffer",0},{"byteOffset",72},{"byteLength",24}},{{"buffer",0},{"byteOffset",96},{"byteLength",6}}})},
        {"accessors",Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,1,0}}},
            {{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",2},{"componentType",5126},{"count",3},{"type","VEC2"}},
            {{"bufferView",3},{"componentType",5123},{"count",3},{"type","SCALAR"}}})},
        {"materials",Json::array({{{"name","Only material"}}})},
        {"meshes",Json::array({{{"name","Triangle"},{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2}}},{"indices",3},{"material",0}}})}}})},
        {"nodes",Json::array({{{"mesh",0}}})},{"scenes",Json::array({{{"nodes",{0}}}})},{"scene",0}};
    model.gltf=document.dump();
    model.manifest=Json{{"version",1},{"name","Triangle"},{"format","Quader"},{"document","model.gltf"},{"original","source.gltf"},
        {"dependencies",Json::array({{{"kind","buffer"},{"index",0},{"path","buffers/0.bin"}}})},{"materialAssets",Json::array({DefaultMaterialId})},
        {"quader",{{"project",bundle.projectId},{"scene",bundle.sceneId},{"collision",false},{"content","fixture"}}}}.dump();
    fixture.modelId=model.id;bundle.models.push_back(std::move(model));
    fixture.file=root/"Content/Levels/Bundle fixture.crimson-quader";
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(bundle));initializeQuaderSceneBundles(root);
    return fixture;
}
}
