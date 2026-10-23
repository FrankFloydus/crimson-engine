#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/ModelAsset.h"
#include "editor/DecalResources.h"
#include "editor/EditorRenderFrame.h"
#include "editor/EditorSession.h"
#include "editor/ResourceRuntime.h"
#include "editor/ViewportDecalDrop.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "scene/SceneDocument.h"
#include "math/Json.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

// The real coordinator is used without constructing editor UI. These endpoints
// are required by its translation unit and must remain unreachable here.
namespace crimson {
void EditorSession::filesChanged(std::span<const std::filesystem::path>){throw std::logic_error("Acceptance fixture entered editor UI.");}
void EditorSession::prepareFrame(AssetLibrary&,Renderer&,std::uint32_t){throw std::logic_error("Acceptance fixture constructed editor UI.");}
}
namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr std::uint16_t Width=640,Height=480;
constexpr const char* TextureId="737de3e1-9ff3-498f-93f0-f0b6207fad13";
constexpr const char* DecalId="db6e9555-b494-48fc-9715-a78208e0e8eb";
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string readText(const fs::path& file){std::ifstream in(file,std::ios::binary);check(bool(in),"Cannot read "+file.string());return {std::istreambuf_iterator<char>(in),{}};}
struct Project {
    fs::path root=fs::temp_directory_path()/("dh-"+std::to_string(Clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code ec;const auto target=fs::weakly_canonical(root,ec),owner=fs::weakly_canonical(fs::temp_directory_path(),ec);
        if(!ec&&target.parent_path()==owner&&target.filename().string().starts_with("dh-"))fs::remove_all(target,ec);}
};
struct Texture{bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
struct ImGuiOwner{ImGuiOwner(){ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}~ImGuiOwner(){ImGui::DestroyContext();}};
void saveImage(const fs::path& file,const DecodedImage& image){fs::create_directories(file.parent_path());check(lodepng::encode(file.string(),image.rgba,image.width,image.height)==0,"PNG output failed.");}
std::size_t changed(const DecodedImage& a,const DecodedImage& b){std::size_t n{};for(std::size_t i=0;i<a.rgba.size();i+=4)
    if(std::abs(int(a.rgba[i])-int(b.rgba[i]))>2||std::abs(int(a.rgba[i+1])-int(b.rgba[i+1]))>2||std::abs(int(a.rgba[i+2])-int(b.rgba[i+2]))>2)++n;return n;}
Json stats(const DecalRenderStats& value){return {{"requested",value.requested},{"submitted",value.submitted},{"drawCalls",value.drawCalls},{"receiverDraws",value.receiverDraws},
    {"roi",{value.roiX,value.roiY,value.roiWidth,value.roiHeight}},{"rasterBytes",value.rasterBytes},{"atlasBytes",value.atlasBytes}};}
bool onTriangle(Vec3 point,const std::array<Vec3,3>& p,double& error){
    const auto a=p[1]-p[0],b=p[2]-p[0],q=point-p[0];const double aa=math::dot(a,a),ab=math::dot(a,b),bb=math::dot(b,b),qa=math::dot(q,a),qb=math::dot(q,b);
    const auto denominator=aa*bb-ab*ab;if(std::abs(denominator)<1e-18)return false;
    const double v=(bb*qa-ab*qb)/denominator,w=(aa*qb-ab*qa)/denominator,u=1-v-w;
    if(std::min({u,v,w})<-.0001||std::max({u,v,w})>1.0001)return false;
    const auto reconstructed=p[0]*float(u)+p[1]*float(v)+p[2]*float(w);error=math::length(reconstructed-point);return error<.00005;
}
void exportGeometry(Json& report,const decals::ModelSurface& source,const Mat4& world,const DecalPatchData* patch,const Mat4& placement) {
    report["entityWorld"]=world;report["placement"]=placement;report["sourceFingerprint"]=source.fingerprint;report["sourceDraws"]=Json::array();
    for(const auto& draw:source.draws){Json triangles=Json::array();for(const auto& t:draw.surface.triangles)triangles.push_back({{"id",t.triangle},{"positions",t.positions},{"normals",t.normals}});
        report["sourceDraws"].push_back({{"draw",draw.surface.id.draw},{"primitive",draw.surface.id.primitive},{"localToWorld",draw.surface.localToWorld},{"triangles",std::move(triangles)}});}
    if(!patch)return;
    report["patch"]["fingerprint"]=patch->fingerprint;report["patch"]["vertices"]=Json::array();report["patch"]["indices"]=patch->indices;
    for(const auto& v:patch->vertices)report["patch"]["vertices"].push_back({v.position[0],v.position[1],v.position[2],v.normal[0],v.normal[1],v.normal[2],v.uv[0],v.uv[1]});
    std::set<std::uint32_t> touched;double maximumError{},maximumPlaneDistance{};std::size_t checked{};
    const auto placementNormal=math::normalized(math::transformDirection(placement,{0,0,1})),center=math::transformPoint(placement,{});
    for(const auto& span:patch->receivers){report["patch"]["receivers"].push_back({{"entity",span.entityId},{"draw",span.drawIndex},{"primitive",span.primitive},{"revision",span.resourceRevision},{"firstIndex",span.firstIndex},{"indexCount",span.indexCount}});
        const auto draw=std::find_if(source.draws.begin(),source.draws.end(),[&](const auto& value){return value.surface.id.draw==span.drawIndex&&value.surface.id.primitive==span.primitive;});
        check(draw!=source.draws.end(),"Published patch lost its original source draw.");const auto toWorld=world*draw->surface.localToWorld;
        std::set<std::uint32_t> vertices;for(std::size_t i=span.firstIndex;i<span.firstIndex+span.indexCount;++i)vertices.insert(patch->indices[i]);
        for(const auto index:vertices){const auto point=patch->vertices[index].position;bool matched=false;
            for(const auto& triangle:draw->surface.triangles){std::array<Vec3,3> positions;for(unsigned c=0;c<3;++c)positions[c]=math::transformPoint(toWorld,triangle.positions[c]);double e{};
                if(onTriangle(point,positions,e)){matched=true;maximumError=std::max(maximumError,e);touched.insert(triangle.triangle);break;}}
            check(matched,"Published decal vertex is detached from original cylinder geometry.");++checked;
            maximumPlaneDistance=std::max(maximumPlaneDistance,double(std::abs(math::dot(point-center,placementNormal))));
        }
    }
    report["provenance"]={{"checkedVertices",checked},{"sourceTriangles",touched.size()},{"maximumWorldError",maximumError},{"maximumDistanceFromPlacementPlane",maximumPlaneDistance}};
    check(checked>10&&touched.size()>3&&maximumPlaneDistance>.01,"Committed cylinder decal remained a flat placement plane.");
}

void exercise(SDL_Window* window,const fs::path& sourceRoot,const fs::path& output) {
    Project project;fs::create_directories(output);
    const auto sourceImage=sourceRoot/"Content/dirt_03.png",sourceAsset=sourceRoot/(std::string("Content/")+DecalId+".crimson-decal"),sourceScene=sourceRoot/"Content/Main.crimson-scene";
    const auto sceneBytes=readText(sourceScene),assetBytes=readText(sourceAsset);const auto sourceImageFingerprint=fingerprintFiles({sourceImage});
    const auto imageFile=project.root/"Content/dirt_03.png";
    fs::copy_file(sourceImage,imageFile);registerAssetPath(project.root,TextureId,imageFile);
    const auto copyMaterial=[&](const MaterialAsset& material){
        for(const auto& id:materialTextureReferences(material)){
            if(id==TextureId||isEngineAssetId(id))continue;
            const auto source=resolveAssetPath(sourceRoot,id);check(fs::is_regular_file(source),"Missing authored material image "+id);
            const auto target=project.root/"Content/dependencies"/(id+source.extension().string());fs::create_directories(target.parent_path());
            if(!fs::exists(target))fs::copy_file(source,target);registerAssetPath(project.root,id,target);
        }
        saveMaterialAsset(project.root,material);
    };
    auto surface=deserializeDecalAsset(assetBytes,DecalId);
    if(const auto pending=stagedDecalMaterial(sourceRoot,surface))surface.stagedMaterial=pending;
    else {copyMaterial(loadMaterialAsset(sourceRoot,surface.material.id));surface.stagedMaterial.reset();}
    saveDecalAsset(project.root,surface);
    writeFileAtomically(output/"rendered-decal-material.json",serializeMaterialAsset(loadMaterialAsset(project.root,surface.material.id)));
    writeFileAtomically(output/"actual-source-decal.json",assetBytes);
    auto isolated=Json::parse(serializeSceneSnapshot({newAssetId(),"Actual saved cylinder",{},{}}));
    isolated["entities"]=Json::array();const auto authored=Json::parse(sceneBytes);
    EntityId actualReceiver{},actualDecal{};
    for(const auto& entity:authored.at("entities")){
        if(!entity.contains("decal")||entity.at("decal").at("asset")!=DecalId)continue;
        const auto receiver=entity.at("decal").at("seed").at("receiver").get<std::string>();
        const auto model=std::find_if(authored.at("entities").begin(),authored.at("entities").end(),[&](const auto& value){return value.at("id")==receiver&&value.contains("model")&&value.at("model")==BuiltinCylinderModelId;});
        if(model==authored.at("entities").end())continue;
        if(model->contains("modelMaterials"))for(const auto& material:model->at("modelMaterials"))if(!material.get<std::string>().empty())copyMaterial(loadMaterialAsset(sourceRoot,material.get<std::string>()));
        isolated["entities"].push_back(*model);isolated["entities"].push_back(entity);
        actualReceiver=std::stoull(receiver,nullptr,16);actualDecal=std::stoull(entity.at("id").get<std::string>(),nullptr,16);break;
    }
    check(isolated["entities"].size()==2,"Actual saved Cylinder/decal pair is unavailable.");
    const auto isolatedId=isolated.at("id").get<std::string>();const auto isolatedFile=project.root/"Content/actual.crimson-scene";
    writeFileAtomically(isolatedFile,isolated.dump(2));registerAssetPath(project.root,isolatedId,isolatedFile);
    writeFileAtomically(output/"actual-source-scene-pair.json",isolated.dump(2));

    // Readback buffers outlive Renderer even if a bounded wait throws.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources models(assets,renderer,resources,project.root);DecalResources decals(assets,resources);TestGpuContext gpu;
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);std::uint32_t frame{};
    PreparedEnvironment environment;environment.key=0x444543414343;environment.settings={2,8,8,16,16,32};
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.08f,.08f,.08f})},environment.settings);check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    const auto tick=[&]{runtime.preFrame(nullptr,frame);models.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();};
    const auto until=[&](const auto& action,const std::string& message){const auto end=Clock::now()+std::chrono::seconds{90};
        do{tick();if(action())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(Clock::now()<end);throw std::runtime_error(message+"\n"+assets.status()+"\n"+models.status());};
    const auto read=[&](bgfx::TextureHandle source){Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Host readback staging allocation failed.");auto data=std::make_shared<DecodedImage>(DecodedImage{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)});destinations.push_back(data);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,Width,Height);const auto ready=bgfx::readTexture(staging.handle,data->rgba.data());check(ready!=UINT32_MAX,"Host readback unsupported.");
        for(unsigned tries=0;tries<120;++tries){tick();if(picking::frameReady(frame,ready))return *data;}throw std::runtime_error("Host readback timed out.");};
    Json report{{"sourceProject",sourceRoot.string()},{"disposableProject",project.root.string()},{"dirtFingerprint",sourceImageFingerprint},{"backend",renderer.backendName()},
        {"renderedDecalDefinition",Json::parse(serializeDecalAsset(surface))}};
    const auto run=[&](SceneDocument& document,EntityId receiver,EntityId decalId,const std::string& name,bool commit) {
        const auto receiverWorld=*document.scene().worldTransform(receiver);Vec3 localPoint{-.0367676169f,.04416942596f,-.4963787198f},localNormal{-.07386933f,0,-.99726796f};
        if(decalId){localPoint=document.scene().find(decalId)->decal->seed.localPoint;localNormal=document.scene().find(decalId)->decal->seed.localNormal;}
        const auto point=math::transformPoint(receiverWorld,localPoint),normal=math::normalized(math::transformDirection(receiverWorld,localNormal));
        Camera camera;camera.position=point+normal*2.5f;camera.target=point;camera.fovAxis=CameraFovAxis::Vertical;camera.fov=45;camera.nearPlane=.05f;camera.farPlane=100;
        const auto render=[&](const Camera& view,bool includeDecal,DecalPreparedScene* result=nullptr) {
            auto extraction=extractScene(document.scene());DecalPreparedScene prepared;if(includeDecal)prepared=decals.prepare(document.scene(),extraction,ResourcePriority::Placement);
            DirectionalLight light;light.direction=normal*-1.f+Vec3{0,-.3f,0};light.color={1,1,1};light.intensity=2;extraction.directional={light};
            EditorRenderFrameInput input;input.extracted=std::move(extraction);input.settings.environment.enabled=true;input.settings.environment.background.show=false;
            input.settings.environment.ambientLight.source=AmbientLightSource::Disabled;input.settings.environment.toneMapping=ToneMapping::Linear;
            input.sceneCamera=view;input.gameCamera=view;input.sceneWidth=input.gameWidth=Width;input.sceneHeight=input.gameHeight=Height;input.sceneAspect=float(Width)/Height;
            input.sceneVisible=input.gameVisible=true;input.surfacePickActive=true;input.decals=prepared.decals;input.decalReceivers=prepared.receivers;input.worldId=41;input.shadowResolution=256;
            EditorRenderFramePreparationStats ready;const auto packet=prepareEditorRenderFrame(std::move(input),assets,models,&ready);renderer.renderFrame(packet);models.endFrame();if(result)*result=std::move(prepared);return ready.ready&&ready.refined;
        };
        until([&]{return render(camera,false);},name+": host receiver never became ready.");
        const auto baselineScene=read(renderer.viewportTexture()),baselineGame=read(renderer.gameViewportTexture());saveImage(output/(name+"-baseline-scene.png"),baselineScene);saveImage(output/(name+"-baseline-game.png"),baselineGame);
        if(commit) {
            ViewportDecalDrop release;release.update(AssetRef<DecalAsset>{surface.id},{Width/2,Height/2},true);release.request(123);
            PickRequest pick;pick.generation=document.documentGeneration();pick.revision=document.scene().revision();pick.serial=123;pick.mode=PickMode::Surface;pick.x=Width/2;pick.y=Height/2;
            check(renderer.requestPick(pick),name+": GPU Surface pick rejected.");render(camera,false);std::optional<PickResult> hit;
            until([&]{hit=renderer.pollPick(frame);return bool(hit);},name+": GPU Surface pick did not complete.");
            const auto intent=release.resolve(*hit);check(intent&&intent->hit.entityId==receiver,name+": release did not resolve the visible Cylinder occurrence.");
            DecalPlacementResult placement;until([&]{placement=decals.resolvePlacement(document.scene(),hit->entityId,hit->drawIndex,hit->primitive,hit->resourceRevision,hit->worldPosition);check(!placement.failed,placement.diagnostic);return placement.ready;},name+": source placement never became ready.");
            SceneDecal value;value.asset.id=surface.id;value.seed=placement.seed;decalId=document.createDecal(value,"dirt_03 host placement",placement.frame);check(decalId!=0,document.status());
        }
        DecalPreparedScene prepared;const auto end=Clock::now()+std::chrono::seconds{30};bool ready=false;
        do{tick();render(camera,true,&prepared);ready=prepared.ready&&prepared.refined&&!prepared.decals.empty();if(ready)break;
            if(!prepared.status.empty()&&prepared.status.find("Preparing")==std::string::npos&&prepared.status.find("Compiling")==std::string::npos&&prepared.status.find("Queued")==std::string::npos)break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(Clock::now()<end);
        auto extraction=extractScene(document.scene());const auto model=std::find_if(extraction.models.begin(),extraction.models.end(),[&](const auto& m){return m.id==receiver;});check(model!=extraction.models.end(),"Receiver extraction lost Cylinder.");
        DecalSourceResult source;until([&]{source=decals.modelSurface(*model);return source.current;},"Cylinder source never became ready.");
        Json caseReport{{"ready",ready},{"status",prepared.status},{"receiver",receiver},{"decal",decalId}};const auto patch=decals.patch(decalId);const auto placement=*document.scene().worldTransform(decalId);
        report["cases"][name]=caseReport;writeFileAtomically(output/"host-report.json",report.dump(2));
        try{exportGeometry(caseReport,*source.surface,receiverWorld,patch.get(),placement);}
        catch(const std::exception& e){caseReport["geometryError"]=e.what();ready=false;}
        if(ready) {
            for(unsigned view=0;view<3;++view) {
                auto selected=camera;std::string suffix="close";
                if(view==1){selected.position=point+math::normalized(normal+Vec3{.4f,0,.2f})*2.5f;suffix="oblique";}
                if(view==2){selected.position=point+normal*20.f;selected.fov=5.5f;suffix="distant";}
                render(selected,true,&prepared);const auto sceneImage=read(renderer.viewportTexture()),gameImage=read(renderer.gameViewportTexture());
                saveImage(output/(name+"-"+suffix+"-scene.png"),sceneImage);saveImage(output/(name+"-"+suffix+"-game.png"),gameImage);
                caseReport["views"][suffix]={{"camera",{{"position",selected.position},{"target",selected.target},{"fov",selected.fov}}},{"scene",stats(renderer.decalStats(RenderViewPurpose::Scene))},{"game",stats(renderer.decalStats(RenderViewPurpose::Game))}};
                caseReport["views"][suffix]["diagnostic"]=renderer.decalStatus();
                for(const auto& d:prepared.decals)if(d.material&&d.material->graph)caseReport["views"][suffix]["preparedMaterials"].push_back({{"channels",d.material->graph->decalChannels},{"signature",d.material->graph->signature},{"uniforms",d.material->graph->uniforms},{"sourceCount",d.material->graph->sources.size()}});
                if(view==0)caseReport["changedPixels"]=changed(baselineScene,sceneImage);
                report["cases"][name]=caseReport;writeFileAtomically(output/"host-report.json",report.dump(2));
                check(sceneImage.rgba==gameImage.rgba,"The same host decal differs between Scene and Game.");
                if(view==0){caseReport["changedPixels"]=changed(baselineScene,sceneImage);check(changed(baselineScene,sceneImage)>100,"The actual host dirt decal did not visibly affect the Cylinder.");}
            }
            // The ordinary close framing above is 2.5 m from the surface.
            // Exercise the actual FPS lens and near-plane intersections too.
            struct FpsView {const char* name;float distance,side,vertical;};
            constexpr std::array<FpsView,9> fpsViews{{
                {"fps-0055",.055f,0,0},{"fps-006",.06f,0,0},{"fps-010",.10f,0,0},{"fps-020",.20f,0,0},
                {"fps-040",.40f,0,0},{"fps-080",.80f,0,0},
                {"fps-grazing-left",.08f,-.24f,.03f},{"fps-grazing-right",.08f,.24f,-.03f},
                {"fps-near-crossing",.03f,.16f,.025f}
            }};
            auto tangent=math::cross(Vec3{0,1,0},normal);
            if(math::length(tangent)<1e-5)tangent={1,0,0};else tangent=math::normalized(tangent);
            for(const auto& fps:fpsViews){
                auto selected=camera;selected.position=point+normal*fps.distance+tangent*fps.side+Vec3{0,fps.vertical,0};
                selected.fov=80;selected.nearPlane=.05f;selected.farPlane=2048;
                render(selected,true,&prepared);const auto sceneImage=read(renderer.viewportTexture()),gameImage=read(renderer.gameViewportTexture());
                check(sceneImage.rgba==gameImage.rgba,"Close-FPS decal differs between Scene and Game.");
                check(renderer.decalStats(RenderViewPurpose::Scene).submitted>0,"Close-FPS camera dropped the prepared decal occurrence.");
                render(selected,true,&prepared);const auto repeat=read(renderer.viewportTexture());
                check(repeat.rgba==sceneImage.rgba,"Static close-FPS decal changed between frames.");
                saveImage(output/(name+"-"+fps.name+"-scene.png"),sceneImage);
                saveImage(output/(name+"-"+fps.name+"-game.png"),gameImage);
                caseReport["views"][fps.name]={{"camera",{{"position",selected.position},{"target",selected.target},{"fov",selected.fov},{"near",selected.nearPlane},{"far",selected.farPlane}}},
                    {"scene",stats(renderer.decalStats(RenderViewPurpose::Scene))},{"game",stats(renderer.decalStats(RenderViewPurpose::Game))},{"staticRepeatEqual",true}};
            }
        }
        if(ready&&name=="committed-cylinder") {
            const auto original=*document.scene().find(decalId);
            const std::array<std::pair<const char*,Mat4>,7> transforms{{
                {"move",math::translation({.12f,.035f,.02f})*original.local},
                {"rotate-x",original.local*math::eulerRotationXYZ({1.5707963268f,0,0})},
                {"rotate-y",original.local*math::eulerRotationXYZ({0,1.5707963268f,0})},
                {"rotate-z",original.local*math::eulerRotationXYZ({0,0,1.1f})},
                {"nonuniform-scale",original.local*math::scaling({.65f,1.15f,2.f})},
                {"negative-scale",original.local*math::scaling({-.8f,.7f,1.f})},
                {"restore",original.local}
            }};
            for(const auto&[label,transform]:transforms) {
                auto edited=*document.scene().find(decalId);edited.local=transform;check(document.updateEntity(decalId,edited,label),document.status());
                until([&]{render(camera,true,&prepared);return prepared.ready&&prepared.refined&&!prepared.decals.empty();},std::string("Decal disappeared after ")+label+": "+prepared.status);
                const auto transformed=decals.patch(decalId);check(bool(transformed),std::string("No prepared patch after ")+label);
                Json geometry;exportGeometry(geometry,*source.surface,receiverWorld,transformed.get(),transform);geometry["status"]=prepared.status;
                check(document.scene().find(decalId)->local==transform,"Surface reprojection changed the authored transform.");
                const auto sceneImage=read(renderer.viewportTexture()),gameImage=read(renderer.gameViewportTexture());check(sceneImage.rgba==gameImage.rgba,"Transformed decal differs between Scene and Game.");
                saveImage(output/(name+"-"+label+"-scene.png"),sceneImage);saveImage(output/(name+"-"+label+"-game.png"),gameImage);
                report["transformCases"][label]=std::move(geometry);
            }
            auto receiverRecord=*document.scene().find(receiver);receiverRecord.receiveDecals=false;
            check(document.updateEntity(receiver,receiverRecord,"Disable receiving decals"),document.status());render(camera,true,&prepared);
            check(prepared.decals.empty(),"Disabled receiver still published a decal patch.");
            SceneDocument playCopy(project.root);check(playCopy.resetTransientFrom(document)&&!playCopy.scene().find(receiver)->receiveDecals,"Play copy lost the authored receiver setting.");
            auto live=*playCopy.scene().find(receiver);live.receiveDecals=true;check(playCopy.updateEntity(receiver,live)&&!document.scene().find(receiver)->receiveDecals,"Play receiver edits leaked into Edit.");
            check(document.undo()&&document.scene().find(receiver)->receiveDecals,"Receive Decals did not undo.");
            until([&]{render(camera,true,&prepared);return prepared.ready&&!prepared.decals.empty();},"Undo did not restore decal reception.");
            const auto meshFile=project.root/"ordinary_receiver.gltf";
            const std::array<float,12> positions{-.5f,.5f,-.5f,-.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f,-.5f},normals{0,1,0,0,1,0,0,1,0,0,1,0};
            const std::array<std::uint16_t,6> indices{0,1,2,0,2,3};std::string buffer;
            buffer.append(reinterpret_cast<const char*>(positions.data()),sizeof(positions));buffer.append(reinterpret_cast<const char*>(normals.data()),sizeof(normals));buffer.append(reinterpret_cast<const char*>(indices.data()),sizeof(indices));
            writeFileAtomically(project.root/"ordinary_receiver.bin",buffer);
            const Json mesh{{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0}}}})},{"nodes",Json::array({{{"mesh",0}}})},
                {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"NORMAL",1}}},{"indices",2}}})}}})},
                {"buffers",Json::array({{{"uri","ordinary_receiver.bin"},{"byteLength",buffer.size()}}})},
                {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",48}},{{"buffer",0},{"byteOffset",48},{"byteLength",48}},{{"buffer",0},{"byteOffset",96},{"byteLength",12}}})},
                {"accessors",Json::array({{{"bufferView",0},{"componentType",5126},{"count",4},{"type","VEC3"},{"min",{-.5,.5,-.5}},{"max",{.5,.5,.5}}},{{"bufferView",1},{"componentType",5126},{"count",4},{"type","VEC3"}},{{"bufferView",2},{"componentType",5123},{"count",6},{"type","SCALAR"}}})}};
            writeFileAtomically(meshFile,mesh.dump());
            const auto ordinary=importModelPackage(meshFile,project.root);
            const auto left=document.createModel(ordinary->id,"Ordinary left receiver"),right=document.createModel(ordinary->id,"Ordinary right receiver");
            auto leftRecord=*document.scene().find(left);leftRecord.local=math::translation({3.5f,0,0});check(document.updateEntity(left,leftRecord),document.status());
            auto rightRecord=*document.scene().find(right);rightRecord.local=math::translation({4.5f,0,0});check(document.updateEntity(right,rightRecord),document.status());
            DecalPlacementResult ordinaryPlacement;until([&]{ordinaryPlacement=decals.resolvePlacement(document.scene(),left,0,0,0,{3.85f,.5f,0});check(!ordinaryPlacement.failed,ordinaryPlacement.diagnostic);return ordinaryPlacement.ready;},"Ordinary mesh seed did not resolve.");
            SceneDecal stain;stain.asset.id=surface.id;stain.seed=ordinaryPlacement.seed;const auto across=document.createDecal(stain,"Across ordinary meshes",ordinaryPlacement.frame);check(across!=0,document.status());
            until([&]{render(camera,true,&prepared);return prepared.ready&&bool(decals.patch(across));},"Ordinary cross-mesh patch did not become ready.");
            const auto crosses=[&]{const auto patch=decals.patch(across);return patch&&std::any_of(patch->receivers.begin(),patch->receivers.end(),[&](const auto&span){return span.entityId==right;});};
            check(crosses(),"Default decals did not cross a contact between ordinary mesh entities.");
            auto moved=*document.scene().find(across);moved.local=math::translation({.7f,0,0})*moved.local;check(document.updateEntity(across,moved,"Move decal onto adjacent mesh"),document.status());
            until([&]{render(camera,true,&prepared);return prepared.ready&&crosses();},"Moving onto a different receiver lost the decal.");
            const auto movedPatch=decals.patch(across);check(std::all_of(movedPatch->receivers.begin(),movedPatch->receivers.end(),[&](const auto&span){return span.entityId==right;}),"Moved decal remained anchored to its previous mesh.");
            check(document.scene().find(across)->decal->seed.receiver==left,"Ordinary movement rewrote the authored provenance hint.");
            check(document.undo(),document.status());until([&]{render(camera,true,&prepared);return prepared.ready&&crosses();},"Undo did not restore the cross-mesh footprint.");
            rightRecord.receiveDecals=false;check(document.updateEntity(right,rightRecord,"Exclude ordinary receiver"),document.status());
            until([&]{render(camera,true,&prepared);return prepared.ready&&bool(decals.patch(across));},"Opt-out patch did not finish rebuilding.");check(!crosses(),"Receive Decals=false did not exclude an adjacent mesh.");
            check(document.undo(),document.status());until([&]{render(camera,true,&prepared);return prepared.ready&&crosses();},"Undo did not restore the adjacent receiver.");
            report["genericReceivers"]={{"ordinaryMeshContact",true},{"moveOntoAdjacentMesh",true},{"receiverOptOut",true},{"undo",true},{"playIsolation",true}};
        }
        caseReport["ready"]=ready;report["cases"][name]=std::move(caseReport);writeFileAtomically(output/"host-report.json",report.dump(2));
        std::cout<<name<<": ready="<<ready<<", status="<<prepared.status<<", vertices="<<(patch?patch->vertices.size():0)<<", sceneSubmitted="<<renderer.decalStats(RenderViewPurpose::Scene).submitted<<'\n';
        return ready;
    };
    SceneDocument builtin(project.root);const auto cylinder=builtin.createCylinder("Real host Cylinder");const bool freshReady=run(builtin,cylinder,0,"committed-cylinder",true);
    SceneDocument actual(project.root);check(actual.load(isolatedId),actual.status());const bool actualReady=run(actual,actualReceiver,actualDecal,"actual-saved-cylinder",false);
    check(readText(sourceScene)==sceneBytes&&readText(sourceAsset)==assetBytes&&fingerprintFiles({sourceImage})==sourceImageFingerprint,"Read-only source project changed during acceptance.");
    runtime.stop();resources.stop();renderer.drainPending(frame);check(freshReady&&actualReady,"Host Cylinder surface wrapping failed; see host-report.json and baseline screenshots.");
}
}
int main(int argc,char** argv){try {
    check(argc==6,"Pass shaderc/source/include, source project root and artifact directory.");
    check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson real host decal acceptance",Width,Height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    check(bool(window),SDL_GetError());exercise(window.get(),fs::absolute(argv[4]),fs::absolute(argv[5]));return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
