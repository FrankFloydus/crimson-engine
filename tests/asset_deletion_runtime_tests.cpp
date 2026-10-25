#include "editor/AssetPreviews.h"
#include "editor/AssetPreviewCache.h"
#include "editor/EditorResources.h"
#include "editor/EditorSession.h"
#include "editor/ResourceRuntime.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "decals/DecalResolve.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include "resources/AssetId.h"
#include "resources/CacheIO.h"
#include "scene/SceneDocument.h"
#include "GpuTestBackend.h"
#include "quader_bundle_fixture.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_set>

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
void materialThumbnails(SDL_Window* window,const fs::path& sourceRoot={}) {
    Project project;initializeContentStorage(project.root);
    const auto colorId=newAssetId(),normalId=newAssetId();
    const auto colorFile=project.root/"Content"/"Color.png",normalFile=project.root/"Content"/"Normal.png";
    fs::create_directories(colorFile.parent_path());
    image(colorFile,{180,80,40,255});image(normalFile,{128,128,255,255});
    const std::array<StoredAssetPath,2> images{{{colorId,colorFile},{normalId,normalFile}}};registerAssetPaths(project.root,images);
    auto fixed=graph(project.root,"Fixed Quader texture",colorId,normalId);
    materialOutputNode(fixed.graph).output.blend=MaterialBlend::Opaque;canonicalizeMaterialAsset(fixed);saveMaterialAsset(project.root,fixed);
    require(materialGraphFixedSurface(fixed.graph).has_value(),"The Quader-like thumbnail fixture no longer exercises fixed material lowering.");
    const auto generated=graph(project.root,"Generated masked texture",colorId,normalId);
    auto blend=createMaterialAsset(project.root,"Height blend");initializeBlendMaterialGraph(blend.graph);
    std::array<MaterialNodeId,4> layers{};const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
    for(std::size_t i=0;i<pins.size();++i){const auto* link=materialInputLink(blend.graph,materialOutputNode(blend.graph).id,pins[i]);require(link,"Blend fixture omitted a source layer.");layers[i]=link->fromNode;findMaterialNode(blend.graph,layers[i])->material.id=fixed.id;}
    const auto height=addMaterialNode(blend.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(blend.graph,height)->texture.image.id=colorId;findMaterialNode(blend.graph,height)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(blend.graph,height,MaterialPin::R,layers[0],MaterialPin::Height);findMaterialNode(blend.graph,layers[1])->blendLayer.heightBlend=true;
    canonicalizeMaterialAsset(blend);saveMaterialAsset(project.root,blend);
    auto decalMaterial=createMaterialAsset(project.root,"Decal material");materialOutputNode(decalMaterial.graph).output.shading=MaterialShading::Decal;
    const auto decalTexture=addMaterialNode(decalMaterial.graph,MaterialNodeKind::Texture2D).id;findMaterialNode(decalMaterial.graph,decalTexture)->texture.image.id=colorId;
    connectMaterialNodes(decalMaterial.graph,decalTexture,MaterialPin::RGB,materialOutputNode(decalMaterial.graph).id,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(decalMaterial);saveMaterialAsset(project.root,decalMaterial);
    auto decalAsset=createDecalAsset(project.root,"Decal asset");decalAsset.material.id=decalMaterial.id;saveDecalAsset(project.root,decalAsset);
    std::vector<std::string> visible{fixed.id,generated.id,blend.id,decalMaterial.id,decalAsset.id};
    std::vector<fs::path> originalFiles;
    if(!sourceRoot.empty()) {
        // Read actual project material graphs and images into an isolated cache
        // root. Keep every UUID and byte intact; the running editor's project,
        // source files and shader/preview caches are never mutation targets.
        std::unordered_set<std::string> copied;
        const auto copySource=[&](const std::string& id) {
            const auto source=resolveAssetPath(sourceRoot,id);
            require(fs::is_regular_file(source),"The actual-project thumbnail source is missing: "+id);
            const auto target=project.root/"Content"/"ActualInputs"/(id+source.extension().string());
            fs::create_directories(target.parent_path());copyFileAtomically(source,target);
            registerAssetPath(project.root,id,target);originalFiles.push_back(source);
        };
        std::function<void(const std::string&)> copyMaterial=[&](const std::string& id) {
            if(isEngineAssetId(id)||!copied.insert(id).second)return;
            const auto material=loadMaterialAsset(sourceRoot,id);
            for(const auto& texture:materialTextureReferences(material))
                if(!isEngineAssetId(texture)&&copied.insert(texture).second)copySource(texture);
            for(const auto& child:materialMaterialReferences(material))copyMaterial(child);
            copySource(id);
        };
        for(const auto* id:{"a98c9d63-efed-5f84-b13a-9273b13b1734", // imported Quader stone
                            "c3744381-b311-4b32-a277-507ee54a5269", // saved height blend example
                            "61e7eb9b-f476-5edb-8a3d-00a102cc6d3a"}) { // user's current decal material
            copyMaterial(id);visible.emplace_back(id);
        }
    }
    const auto originalFingerprint=fingerprintFiles(originalFiles);

    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources prepared(assets,renderer,resources,project.root);
    TestGpuContext gpu;renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x54484d424d4154;
    environment->settings={2,8,8,16,16,16};environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions environmentOptions;environmentOptions.variant="ibl";
    resources.seed<PreparedEnvironment>(AssetRef<PreparedEnvironment>{StudioEnvironmentId},{environment,sizeof(PreparedEnvironment),environment->key,{}},environmentOptions);
    std::uint32_t frame{};
    const auto tick=[&]{runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();};
    const auto wait=[&](AssetPreviews& previews,const char* phase,bool diskOnly){
        const auto begin=Clock::now(),deadline=begin+std::chrono::seconds{120};auto nextLog=begin;
        for(;;){
            tick();previews.pump(frame,visible,32);prepared.endFrame();
            const bool ready=std::all_of(visible.begin(),visible.end(),[&](const auto& id){return bgfx::isValid(renderer.thumbnailTexture(id));});
            if(ready){const auto stats=previews.stats();std::cout<<phase<<" material thumbnails ready in "<<std::chrono::duration<double,std::milli>(Clock::now()-begin).count()<<"ms; renders="<<stats.renderRequests<<" disk="<<stats.diskHits<<'\n'<<std::flush;
                if(diskOnly)require(stats.renderRequests==0&&stats.diskHits==visible.size(),"Warm material thumbnails did not restore through their persistent source descriptors.");return;}
            if(Clock::now()>=nextLog){nextLog=Clock::now()+std::chrono::seconds{2};for(const auto& id:visible)if(!bgfx::isValid(renderer.thumbnailTexture(id)))std::cout<<phase<<' '<<assets.displayName(id)<<": "<<previews.diagnostic(id)<<'\n'<<std::flush;}
            require(Clock::now()<deadline,std::string(phase)+" material thumbnails failed to converge. Assets: "+assets.status()+"\nPreviews: "+previews.status()+"\nModels: "+prepared.status());
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    };
    {
        AssetPreviews previews(project.root,assets,resources,renderer,prepared);wait(previews,"cold",false);
        // Finish the ordinary async PNG/index writes before discarding the owner.
        const auto deadline=Clock::now()+std::chrono::seconds{15};bool persisted{};
        while(Clock::now()<deadline){tick();previews.pump(frame,visible,32);prepared.endFrame();
            const auto folder=project.root/".crimson"/"cache"/"previews";
            persisted=std::all_of(visible.begin(),visible.end(),[&](const auto& id){return fs::is_regular_file(assetPreviewIndexPath(folder,id,32));});
            if(persisted)break;std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        require(persisted,"Material thumbnail pixels became resident but their persistent source indexes never completed.");
        auto policy=streaming::detail::HashSeed;const auto serialized=assets.references().serialize();streaming::detail::hash(policy,serialized.data(),serialized.size());
        for(const auto& id:visible){const auto cached=readAssetPreviewCache(project.root,project.root/".crimson"/"cache"/"previews",id,32,renderer.shaderSignature(),policy);std::size_t colored{};
            for(std::size_t offset=0;offset<cached.image->rgba.size();offset+=4)colored+=cached.image->rgba[offset]>cached.image->rgba[offset+1]+10&&cached.image->rgba[offset]>cached.image->rgba[offset+2]+10;
            if(std::find(visible.begin(),visible.begin()+5,id)!=visible.begin()+5)
                require(colored>20,"Material thumbnail published a blank/neutral fallback instead of its authored red surface: "+assets.displayName(id));}
    }
    for(const auto& id:visible)renderer.evictThumbnail(id);
    {AssetPreviews reopened(project.root,assets,resources,renderer,prepared);wait(reopened,"warm",true);}
    require(fingerprintFiles(originalFiles)==originalFingerprint,"The actual-project thumbnail probe changed an authored source.");
    runtime.stop();frame=renderer.drainPending(frame);tools.shutdown();
}
// Exercise failure/backoff through the actual thumbnail owner. The material
// dependency changes, while the scene asset and its authored decal stay intact.
void decalThumbnailRetry(SDL_Window* window) {
    Project project;initializeContentStorage(project.root);
    auto receiverMaterial=createMaterialAsset(project.root,"Retry receiver");
    materialOutputNode(receiverMaterial.graph).output.shading=MaterialShading::Unlit;
    canonicalizeMaterialAsset(receiverMaterial);saveMaterialAsset(project.root,receiverMaterial);
    auto decalMaterial=createMaterialAsset(project.root,"Retry decal material");
    materialOutputNode(decalMaterial.graph).output.shading=MaterialShading::Decal;
    materialOutputNode(decalMaterial.graph).output.baseColor={1,.1f,.1f};
    canonicalizeMaterialAsset(decalMaterial);saveMaterialAsset(project.root,decalMaterial);
    auto decalAsset=createDecalAsset(project.root,"Retry decal");decalAsset.material.id=decalMaterial.id;saveDecalAsset(project.root,decalAsset);
    SceneDocument document(project.root);const auto box=document.createBox("Retry receiver");
    auto boxRecord=*document.scene().find(box);boxRecord.modelMaterials={AssetRef<MaterialAsset>{receiverMaterial.id}};
    require(document.updateEntity(box,boxRecord),document.status());
    SceneDecal decal;decal.asset.id=decalAsset.id;decal.seed.receiver=box;decal.seed.modelId=boxRecord.model->id;decal.seed.localPoint={.1f,.5f,.1f};
    const auto placement=decals::surfacePlacementFrame(decal.seed.localPoint,{0,1,0})*math::scaling({.4f,.4f,1});
    document.createDecal(decal,"Retry surface decal",placement);
    require(document.save("Decal thumbnail retry"),document.status());const auto sceneId=document.assetId();
    const auto sceneFile=resolveAssetPath(project.root,sceneId),materialFile=resolveAssetPath(project.root,receiverMaterial.id);
    const auto unchangedScene=fingerprintFiles({sceneFile});

    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources prepared(assets,renderer,resources,project.root);
    AssetPreviews previews(project.root,assets,resources,renderer,prepared);TestGpuContext gpu;
    renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x444543414c525452;
    environment->settings={2,8,8,16,16,16};environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions environmentOptions;environmentOptions.variant="ibl";
    resources.seed<PreparedEnvironment>(AssetRef<PreparedEnvironment>{StudioEnvironmentId},{environment,sizeof(PreparedEnvironment),environment->key,{}},environmentOptions);
    const std::array visible{sceneId};std::uint32_t frame{};
    const auto tick=[&]{runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();
        previews.pump(frame,visible,32);prepared.endFrame();require(runtime.status().empty(),"Decal thumbnail coordinator: "+runtime.status());};
    const auto until=[&](const auto& condition,const char* reason,std::chrono::seconds timeout){const auto deadline=Clock::now()+timeout;
        do{tick();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(Clock::now()<deadline);
        throw std::runtime_error(std::string(reason)+"\nThumbnail: "+previews.diagnostic(sceneId)+"\nAssets: "+assets.status());};
    until([&]{return previews.stats().decalPreparationFailures>0;},"Unlit receiver did not reach actual terminal thumbnail failure.",std::chrono::seconds{15});
    require(previews.diagnostic(sceneId).find("Unlit")!=std::string::npos,"Terminal thumbnail fixture failed for an unrelated reason.");
    const auto failed=previews.stats();const auto retryBegin=Clock::now();
    constexpr unsigned Requests=256;
    for(unsigned index=0;index<Requests;++index)tick();
    const auto after=previews.stats();
    // The retry policy is time based. This bound tolerates a slow test machine
    // without assuming a particular CPU/GPU frame duration or sleeping a second.
    const auto maximumRetries=static_cast<std::uint64_t>(std::ceil(std::chrono::duration<double>(Clock::now()-retryBegin).count()))+1;
    require(after.requests>=failed.requests+Requests,"The regression did not issue repeated actual thumbnail-owner requests.");
    require(after.decalPreparations-failed.decalPreparations<=maximumRetries&&after.decalPreparationFailures-failed.decalPreparationFailures<=maximumRetries,
        "A terminal decal thumbnail repeated full preparation on ordinary visible-card requests.");
    require(after.renderRequests==0&&!bgfx::isValid(renderer.thumbnailTexture(sceneId)),"An invalid decal scene was published as a finished thumbnail.");

    // This is a dependent material file, not the scene's own identity. Pending
    // failure descriptors must make this event reset the owner's real backoff.
    materialOutputNode(receiverMaterial.graph).output.shading=MaterialShading::StandardPbr;
    canonicalizeMaterialAsset(receiverMaterial);saveMaterialAsset(project.root,receiverMaterial);
    assets.publishMaterial(std::make_shared<const MaterialAsset>(receiverMaterial));
    const auto beforeInvalidate=previews.stats();previews.invalidateFile(materialFile);
    require(previews.stats().invalidations>beforeInvalidate.invalidations,"A terminal thumbnail lost its material dependency invalidation route.");
    const auto failuresBeforeRepair=previews.stats().decalPreparationFailures;
    bool sawWorkerPending=false,sawMaterialPending=false;
    until([&]{const auto stats=previews.stats();
        require(stats.decalPreparationFailures==failuresBeforeRepair,"An active decal worker/material preparation acquired terminal thumbnail backoff.");
        const auto diagnostic=previews.diagnostic(sceneId);
        sawWorkerPending|=diagnostic.find("Preparing decal surface")!=std::string::npos||diagnostic.find("Preparing surface geometry")!=std::string::npos;
        sawMaterialPending|=diagnostic.find("Compiling")!=std::string::npos||diagnostic.find("Preparing decal material")!=std::string::npos;
        return bgfx::isValid(renderer.thumbnailTexture(sceneId));
    },"A repaired material dependency did not resume the actual decal thumbnail.",std::chrono::seconds{90});
    const auto repaired=previews.stats();
    require(repaired.decalPreparations>after.decalPreparations+1&&repaired.renderRequests>0,
        "Dependency invalidation did not resume repeated nonterminal decal preparation and publication.");
    require(sawWorkerPending||sawMaterialPending,"The repaired fixture stopped exercising asynchronous decal readiness.");
    require(fingerprintFiles({sceneFile})==unchangedScene,"Dependent-material thumbnail repair changed the authored scene or decal.");
    std::cout<<"Actual decal thumbnail retry: requests="<<after.requests-failed.requests
        <<" terminal_preparations="<<after.decalPreparations-failed.decalPreparations
        <<" repaired_preparations="<<repaired.decalPreparations-after.decalPreparations
        <<" worker_pending="<<sawWorkerPending<<" material_pending="<<sawMaterialPending<<" published=true\n";
    runtime.stop();frame=renderer.drainPending(frame);tools.shutdown();
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
    Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);
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
        frame=renderer.advance_frame();gpu.flush();
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
int main(int argc,char** argv) {
    try {
        SdlOwner sdl;
        const std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson deletion resource regression",64,64,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),SDL_GetError());
        if(argc>=2&&std::string_view(argv[1])=="--decal-thumbnail-retry")decalThumbnailRetry(window.get());
        else if(argc>=2&&std::string_view(argv[1])=="--material-thumbnails")materialThumbnails(window.get(),argc>=3?fs::path(argv[2]):fs::path{});
        else exercise(window.get());
        return 0;
    }catch(const std::exception& error){std::cerr<<"Asset deletion runtime test failed: "<<error.what()<<'\n';return 1;}
}
