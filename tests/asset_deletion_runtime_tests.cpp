#include "editor/AssetPreviews.h"
#include "editor/EditorResources.h"
#include "editor/EditorSession.h"
#include "editor/ResourceRuntime.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include "resources/AssetId.h"
#include "scene/SceneDocument.h"
#include "GpuTestBackend.h"
#include "quader_bundle_fixture.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

// This executable runs the actual resource coordinator with a null editor and
// the actual preview/resource owners directly. No EditorSession is constructed;
// its document/UI integration is outside this regression's boundary. These two
// link endpoints must remain unreachable, rather than replacing any resource,
// watcher, graphics, thumbnail or deletion implementation with a test double.
namespace crimson {
void EditorSession::filesChanged(std::span<const std::filesystem::path>) {
    throw std::logic_error("The deletion resource regression unexpectedly entered editor UI integration.");
}
void EditorSession::prepareFrame(AssetLibrary&,Renderer&,std::uint32_t) {
    throw std::logic_error("The deletion resource regression unexpectedly constructed an editor session.");
}
}

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("ad-"+std::to_string(Clock::now().time_since_epoch().count()));
    Project() {fs::create_directories(root);}
    ~Project() {
        std::error_code error;
        const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error);
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("ad-"))fs::remove_all(target,error);
    }
};
struct SdlOwner {
    SdlOwner() {require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}
    ~SdlOwner() {SDL_Quit();}
};
struct ImGuiOwner {
    ImGuiOwner() {ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}
    ~ImGuiOwner() {ImGui::DestroyContext();}
};
void image(const fs::path& file,const std::array<std::uint8_t,4>& pixel) {
    std::vector<std::uint8_t> pixels;
    for(unsigned index=0;index<16*16;++index)pixels.insert(pixels.end(),pixel.begin(),pixel.end());
    std::vector<unsigned char> encoded;
    require(lodepng::encode(encoded,pixels,16,16)==0,"Could not encode deletion fixture image.");
    writeFileAtomically(file,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
}
MaterialAsset graph(const fs::path& root,const std::string& name,const std::string& color,const std::string& normal) {
    auto material=createMaterialAsset(root,name);
    const auto output=materialOutputNode(material.graph).id;
    // Mask coverage deliberately uses the generated graph packet path, with
    // independent color/normal roles and the same texture shared by both models.
    materialOutputNode(material.graph).output.blend=MaterialBlend::Mask;
    const auto base=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,base)->texture.image.id=color;
    connectMaterialNodes(material.graph,base,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(material.graph,base,MaterialPin::A,output,MaterialPin::OpacityMask);
    const auto mappedNormal=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,mappedNormal)->texture.image.id=normal;
    findMaterialNode(material.graph,mappedNormal)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(material.graph,mappedNormal,MaterialPin::Normal,output,MaterialPin::Normal);
    canonicalizeMaterialAsset(material);saveMaterialAsset(root,material);return material;
}
bool catalogContains(const AssetLibrary& assets,const std::string& id) {
    return std::any_of(assets.catalog().begin(),assets.catalog().end(),[&](const auto& entry){return entry.id==id;});
}
void exercise(SDL_Window* window) {
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    const auto colorId=newAssetId(),normalId=newAssetId();
    const auto colorFile=project.root/"Content/Color.png",normalFile=project.root/"Content/Normal.png";
    image(colorFile,{180,100,60,255});image(normalFile,{128,128,255,255});
    const std::array<StoredAssetPath,2> images{{{colorId,colorFile},{normalId,normalFile}}};
    registerAssetPaths(project.root,images);
    const auto removedMaterial=graph(project.root,"Remove surface",colorId,normalId);
    const auto survivingMaterial=graph(project.root,"Keep surface",colorId,normalId);
    auto manifest=nlohmann::json::parse(fixture.bundle.models.front().manifest);
    manifest["materialAssets"]=nlohmann::json::array({removedMaterial.id});
    fixture.bundle.models.front().manifest=manifest.dump();
    auto sibling=fixture.bundle.models.front();sibling.legacyManifest="assets/models/quader-runtime-sibling/manifest.json";
    const auto siblingBase=fs::path(sibling.legacyManifest).parent_path();
    const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    for(std::size_t index=0;index<members.size();++index)sibling.memberIds[index]=canonicalAssetId((siblingBase/members[index]).generic_string());
    sibling.id=sibling.memberIds[0];manifest["materialAssets"]=nlohmann::json::array({survivingMaterial.id});sibling.manifest=manifest.dump();
    fixture.bundle.models.push_back(sibling);
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));initializeQuaderSceneBundles(project.root);
    const auto deletedModelFile=resolveAssetPath(project.root,fixture.modelId);

    SceneDocument saved(project.root);const auto savedMesh=saved.createModel(fixture.modelId,"Saved replacement consumer");
    require(saved.save("Retained scene"),saved.status());const auto savedId=saved.assetId();
    SceneDocument active(project.root);const auto owner=active.createQuader("Removed Quader node");
    const auto mesh=active.createModel(fixture.modelId,"Removed mesh");active.select({mesh},mesh);
    require(active.reparentSelection(owner),active.status());active.select({owner},owner);require(active.eraseSelection(),active.status());

    ResourceManager resources;
    Renderer renderer(window,nullptr,selectedTestGraphicsBackend());
    ImGuiOwner context;ImGuiBgfx tools;
    AssetLibrary assets(project.root,&resources);
    const auto bundleFile=resolveAssetPath(project.root,fixture.bundle.id);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);
    EditorResources prepared(assets,renderer,resources,project.root);
    AssetPreviews previews(project.root,assets,resources,renderer,prepared);
    TestGpuContext gpu;
    renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    // The environment is unrelated to deletion. Seed a small complete CPU bake
    // into the ordinary ibl resource record to keep this regression focused.
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x44454c455445;
    environment->settings={2,8,8,16,16,16};
    environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions environmentOptions;environmentOptions.variant="ibl";
    resources.seed<PreparedEnvironment>(AssetRef<PreparedEnvironment>{StudioEnvironmentId},
        {environment,sizeof(PreparedEnvironment),environment->key,{}},environmentOptions);

    std::uint32_t frame{};
    const auto tick=[&] {
        runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();
        frame=bgfx::frame();gpu.flush();
        require(runtime.status().empty(),"Resource coordinator: "+runtime.status());
    };
    const auto until=[&](const auto& action,const std::string& failure,std::chrono::seconds timeout=std::chrono::seconds{90}) {
        const auto deadline=Clock::now()+timeout;
        do {tick();if(action())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(Clock::now()<deadline);
        throw std::runtime_error(failure+"\nAssets: "+assets.status()+"\nPreviews: "+previews.status()+"\nModels: "+prepared.status());
    };
    Camera camera;camera.position={2,2,3};camera.target={.5f,.5f,0};
    std::shared_ptr<const PreparedEditorModel> removedPacket,survivorPacket;
    until([&] {
        removedPacket=prepared.prepare(fixture.modelId,Mat4{},camera,64,true);
        survivorPacket=prepared.prepare(sibling.id,Mat4{},camera,64,true);
        prepared.endFrame();
        return removedPacket&&survivorPacket&&removedPacket->refined&&survivorPacket->refined&&
            removedPacket->textures.front().graph&&survivorPacket->textures.front().graph;
    },"The deletion fixture never published complete generated graph/texture packets.");
    const auto oldGraph=survivorPacket->textures.front().graph;
    require(oldGraph->sources.size()>=2,"The retained graph packet has no color/normal sources.");
    require(assets.find(colorId)&&assets.find(normalId),"The deletion fixture has no streamed texture thumbnails.");

    const std::array pendingVisible{fixture.modelId};
    until([&] {
        previews.pump(frame,pendingVisible,32);prepared.endFrame();return renderer.thumbnailBusy();
    },"The deleted model never queued its real thumbnail readback.");
    require(!bgfx::isValid(renderer.thumbnailTexture(fixture.modelId)),"The deletion fixture has no pending unpublished thumbnail.");
    const auto plan=assets.planDeletion({fixture.modelId,removedMaterial.id,colorId,normalId},&active);
    require(plan.error.empty(),plan.error);
    const std::unordered_map<std::string,std::string> replacements{{fixture.modelId,sibling.id}};
    bool deleted=assets.deleteAssets(plan,replacements,&active);
    until([&] {if(!deleted)deleted=assets.pumpDeletion(&active);return deleted;},"Mixed-asset deletion did not commit.");
    require(assets.references().resolve(fixture.modelId)==sibling.id,"The saved model reference lost its replacement.");
    const auto snapshot=readSceneSnapshot(project.root,savedId);
    const auto found=std::find_if(snapshot.records.begin(),snapshot.records.end(),[&](const auto& record){return record.id==savedMesh;});
    require(found!=snapshot.records.end()&&found->model&&found->model->id==fixture.modelId,
        "The regression must keep the authored deleted UUID in its saved scene.");
    require(!fs::exists(deletedModelFile),"The deletion fixture retained its removed model source.");
    bool unregistered=false;try{(void)resolveAssetPath(project.root,fixture.modelId);}catch(const std::exception&){unregistered=true;}
    require(unregistered,"The deleted bundled UUID still has a live path; this would hide the fallback-preview regression.");
    previews.invalidate(fixture.modelId);previews.invalidate(removedMaterial.id);
    previews.invalidate(colorId);previews.invalidate(normalId);

    // Keep the old prepared graph alive while the actual coordinator consumes
    // delayed native notifications and previews drain the deleted readback.
    // Stale previous-frame visible IDs are intentional, as in the real browser.
    const std::array visible{fixture.modelId,removedMaterial.id,sibling.id,savedId};
    until([&] {
        previews.pump(frame,visible,32);
        const auto current=prepared.prepare(sibling.id,Mat4{},camera,64,true);prepared.endFrame();
        require(!catalogContains(assets,fixture.modelId)&&!catalogContains(assets,removedMaterial.id)&&
            !assets.find(colorId)&&!assets.find(normalId)&&!bgfx::isValid(renderer.thumbnailTexture(fixture.modelId)),
            "Post-deletion frame processing revived a removed asset or published its delayed thumbnail.");
        return current&&current->refined&&current->textures.front().graph&&
            current->textures.front().graph->signature!=oldGraph->signature&&
            bgfx::isValid(renderer.thumbnailTexture(sibling.id))&&bgfx::isValid(renderer.thumbnailTexture(savedId));
    },"Surviving model and saved-scene previews did not converge after mixed deletion.",std::chrono::seconds{30});
    require(previews.status().empty(),"A surviving preview retained a deletion error: "+previews.status());
    for(unsigned index=0;index<20;++index) {
        tick();previews.pump(frame,visible,32);prepared.endFrame();
        require(previews.status().empty(),"A later preview frame failed after successful deletion: "+previews.status());
    }

    // Remove the final member too: its authoritative bundle and location are
    // now removed, while this live observer still remembers their former paths.
    const auto finalPlan=assets.planDeletion({sibling.id,survivingMaterial.id},&active);
    require(finalPlan.error.empty(),finalPlan.error);
    bool finalDeleted=assets.deleteAssets(finalPlan,{},&active);
    until([&] {if(!finalDeleted)finalDeleted=assets.pumpDeletion(&active);return finalDeleted;},"Final bundled mesh deletion did not commit.");
    require(!fs::exists(bundleFile),"Deleting the final mesh retained its authoritative bundle file.");
    const auto afterFinal=readSceneSnapshot(project.root,savedId);
    require(std::none_of(afterFinal.records.begin(),afterFinal.records.end(),[](const auto& record){return record.model.has_value();}),
        "The saved deleted-model replacement chain retained a mesh after deleting its final target.");
    previews.invalidate(sibling.id);previews.invalidate(survivingMaterial.id);previews.invalidate(savedId);
    renderer.evictThumbnail(savedId);
    until([&] {
        previews.pump(frame,visible,32);prepared.endFrame();
        require(!catalogContains(assets,sibling.id)&&!catalogContains(assets,survivingMaterial.id)&&
            !bgfx::isValid(renderer.thumbnailTexture(sibling.id)),"Final deletion revived its removed sibling or thumbnail.");
        return bgfx::isValid(renderer.thumbnailTexture(savedId));
    },"The surviving empty scene preview did not converge after final bundle removal.",std::chrono::seconds{15});
    for(unsigned index=0;index<20;++index) {
        tick();previews.pump(frame,visible,32);prepared.endFrame();
        require(previews.status().empty(),"A later frame resolved a removed final-bundle source: "+previews.status());
    }
    runtime.stop();
    frame=renderer.drainPending(frame);
    tools.shutdown();
    std::cout<<"Actual Quader mixed-deletion resource frames, generated texture packets, queued thumbnail readback and saved-scene fallback preview passed.\n";
}
}
int main() {
    try {
        SdlOwner sdl;
        const std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson deletion resource regression",64,64,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),SDL_GetError());exercise(window.get());return 0;
    }catch(const std::exception& error){std::cerr<<"Asset deletion runtime test failed: "<<error.what()<<'\n';return 1;}
}
