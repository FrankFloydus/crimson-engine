// Explicit hidden saved-project diagnostic. Included by the existing room
// benchmark so it exercises production resource preparation without app input.
#include "assets/ContentStorage.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/RenderPassPlan.h"
#include <unordered_map>

namespace {
void copyGiProject(const fs::path& source,const fs::path& destination) {
    for(const char* name:{"Content","Engine","assets",".crimson"}) {
        const auto from=source/name;if(!fs::is_directory(from))continue;
        fs::create_directories(destination/name);
        for(const auto& item:fs::directory_iterator(from)) {
            const auto filename=item.path().filename();
            if(filename=="branding"||filename==".resource-transactions"||filename.string().starts_with(".staging"))continue;
            if(filename=="cache") {
                fs::create_directories(destination/name/filename);
                for(const char* cache:{"mesh-lods","texture-mips","shaders"})
                    if(fs::is_directory(item.path()/cache))fs::copy(item.path()/cache,destination/name/filename/cache,fs::copy_options::recursive);
            } else fs::copy(item.path(),destination/name/filename,fs::copy_options::recursive);
        }
    }
}
int screenLightingEditorDiagnostic(int argc,char** argv) {
    require(argc==7||(argc==8&&std::string(argv[7])=="--inspect"),"Usage: forward_plus_room_benchmark --gi-project|--gi-point-project PROJECT SHADERC SHADER_SOURCES SHADER_INCLUDES OUTPUT_DIRECTORY [--inspect]");
    const bool pointOnly=std::string(argv[1])=="--gi-point-project";
    const bool inspectOnly=argc==8;
    const fs::path original=fs::weakly_canonical(argv[2]),output=fs::absolute(argv[6]);Temporary temporary;
    std::cout<<"Staging read-only saved project at "<<temporary.root<<std::endl;copyGiProject(original,temporary.root);
    const auto project=temporary.root;initializeContentStorage(project);fs::create_directories(output);
    Json session;{std::ifstream file(project/".crimson/editor-session.json");if(!file)file.open(project/"assets/editor-session.json");file>>session;}
    const std::string sceneId=session.at("scene").get<std::string>();SceneDocument document(project);require(document.load(sceneId),document.status());
    auto extracted=extractScene(document.scene());Camera camera;
    const auto& saved=session.at("cameras").at(sceneId);camera.position=saved.at("position").get<Vec3>();camera.target=saved.at("target").get<Vec3>();camera.up=saved.at("up").get<Vec3>();
    camera.fov=saved.value("fov",45.f);camera.nearPlane=saved.value("near",.01f);camera.farPlane=saved.value("far",2048.f);
    const Camera gameCamera=resolveGameCamera(document.scene()).value_or(camera);
    std::optional<PointLight> centerPoint;
    if(pointOnly) {
        const auto records=document.scene().records();
        std::erase_if(extracted.points,[&](const auto& point) {
            return std::none_of(records.begin(),records.end(),[&](const auto& entity){return entity.id==point.entityId&&entity.point.has_value();});
        });
        extracted.directional.clear();extracted.proceduralSun={};
        extracted.particles.clear();
        Vec3 center{};unsigned coffins{};
        for(const auto& entity:records)if(entity.model&&(entity.name.find("tomb")!=std::string::npos||entity.name.find("coffin")!=std::string::npos))
            if(const auto transform=document.scene().worldTransform(entity.id)){center=center+math::transformPoint(*transform,{});++coffins;}
        if(coffins) {
            PointLight point;point.position=center/double(coffins)+Vec3{0,2,0};point.intensity=35;point.radius=6;point.entityId=UINT64_MAX;
            point.castShadows=true;centerPoint=point;
        }
    }
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson saved-project screen lighting diagnostic",Width,Height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);require(bool(window),SDL_GetError());
    ResourceManager resources;Renderer renderer(window.get(),nullptr,selectedTestGraphicsBackend());
    ShaderCompileConfig config{argv[3],argv[4],argv[5],resolveAssetPath(project,"assets/cache/shaders")};MaterialGraphPrograms programs(resources,config);
    AssetLibrary assets(project,&resources);assets.setMaterialGraphPrograms(&programs);EditorResources prepared(assets,renderer,resources,project);
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);bgfx::reset(Width,Height,BGFX_RESET_NONE);
    TestGpuContext context;
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x474944494147;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    // Point-only variants preserve geometry, graph shading, coverage and texture
    // bindings. Only emission is zeroed in copied immutable render packets.
    struct EmissionlessGraph {
        std::shared_ptr<const PreparedGraphMaterial> source,replacement;
        std::optional<MaterialGraphShader> shader;
    };
    std::unordered_map<const PreparedGraphMaterial*,EmissionlessGraph> emissionlessGraphs;
    struct EmissionlessPackets {
        std::vector<ModelMaterial> materials;
        std::vector<ModelMaterialTextures> textures;
        std::shared_ptr<const PreparedRenderFrame> original;
    };
    using PacketKey=std::pair<const void*,const void*>;
    std::map<PacketKey,std::shared_ptr<const EmissionlessPackets>> emissionlessPackets;
    const auto emissionless=[&](const std::shared_ptr<const PreparedRenderFrame>& source,bool& ready) {
        auto copy=std::make_shared<PreparedRenderFrame>(*source);
        const auto overrideInstance=[&](RenderModelInstance& instance) {
            if(!instance.materials||!instance.textures)return;
            const PacketKey key{instance.materials,instance.textures};
            if(const auto found=emissionlessPackets.find(key);found!=emissionlessPackets.end()) {
                instance.materials=&found->second->materials;instance.textures=&found->second->textures;return;
            }
            auto packets=std::make_shared<EmissionlessPackets>();
            packets->materials=*instance.materials;packets->textures=*instance.textures;packets->original=source;
            bool complete=true;
            for(std::size_t slot=0;slot<packets->materials.size();++slot) {
                auto& material=packets->materials[slot];material.emissive={};
                if(slot>=packets->textures.size()||!packets->textures[slot].graph)continue;
                const auto originalGraph=packets->textures[slot].graph;
                auto& state=emissionlessGraphs[originalGraph.get()];
                if(!state.source) {
                    state.source=originalGraph;
                    auto graph=std::make_shared<PreparedGraphMaterial>(*originalGraph);
                    require(graph->uniforms.size()>=3,"Generated material lacks output emission parameters.");
                    graph->uniforms[2][0]=graph->uniforms[2][1]=graph->uniforms[2][2]=0;
                    if(material.graph) {
                        const auto& outputNode=materialOutputNode(*material.graph);
                        const auto* emissionLink=materialInputLink(*material.graph,outputNode.id,MaterialPin::Emission);
                        bool zeroEmission=false;
                        if(emissionLink&&materialExpressionUniform(*material.graph,emissionLink->fromNode,emissionLink->fromPin)&&
                            !materialExpressionTimeDependent(*material.graph,emissionLink->fromNode,emissionLink->fromPin)) {
                            const auto value=evaluateMaterialUniform(*material.graph,emissionLink->fromNode,emissionLink->fromPin);
                            zeroEmission=value.value[0]==0&&value.value[1]==0&&value.value[2]==0;
                        }
                        if(emissionLink&&!zeroEmission) {
                            MaterialAsset asset;asset.graph=*material.graph;asset.surface=material;asset.shading=graph->shading;asset.blend=graph->blend;
                            state.shader=generateMaterialGraphShader(asset);
                            require(state.shader->uniforms.size()==graph->uniforms.size(),"Emission override changed graph parameter layout.");
                            require(state.shader->textures.size()==graph->sources.size(),"Emission override changed graph texture layout.");
                            for(std::size_t i=0;i<graph->sources.size();++i)
                                require(state.shader->textures[i].nodeId==graph->sources[i].nodeId&&state.shader->textures[i].slot==graph->sources[i].slot,
                                    "Emission override changed graph texture slots.");
                            // Preserve the complete original topology and its
                            // parameter/texture slots, replacing only this output.
                            for(auto& variant:state.shader->variants) {
                                const auto begin=variant.source.find("result.emission = ");
                                if(begin!=std::string::npos) {
                                    const auto end=variant.source.find(';',begin);require(end!=std::string::npos,"Cannot isolate generated emission output.");
                                    variant.source.replace(begin,end-begin,"result.emission = vec3_splat(0.0)");
                                }
                            }
                            state.shader->topologyKey+="-point-gi-no-emission";
                        }
                    }
                    state.replacement=std::move(graph);
                }
                if(state.shader) {
                    const auto result=programs.request(*state.shader,ResourcePriority::Visible);
                    if(result.current&&result.programs) {
                        auto graph=std::make_shared<PreparedGraphMaterial>(*state.replacement);graph->programs=result.programs;
                        state.replacement=std::move(graph);state.shader.reset();
                    } else {
                        require(result.compiling||result.diagnostic.empty(),"Emission-free graph failed: "+result.diagnostic);
                        complete=false;continue;
                    }
                }
                packets->textures[slot].graph=state.replacement;
            }
            if(!complete){ready=false;return;}
            emissionlessPackets.emplace(key,packets);instance.materials=&packets->materials;instance.textures=&packets->textures;
        };
        for(auto& view:copy->views)for(auto& instance:view.instances)overrideInstance(instance);
        auto world=std::make_shared<RenderWorldSnapshot>(*source->world);
        for(auto& instance:world->shadowInstances)overrideInstance(instance);
        copy->world=std::move(world);return std::shared_ptr<const PreparedRenderFrame>{std::move(copy)};
    };
    std::uint32_t frame{};std::shared_ptr<const PreparedRenderFrame> last;
    bool submittedGi{},submittedAo{},pointsEnabled=true,useCenterPoint{},useHighBudget{},useObliqueCamera{},useLinearTone{};
    Camera obliqueCamera=camera;
    if(centerPoint){obliqueCamera.position=centerPoint->position+Vec3{4,1,4};obliqueCamera.target=centerPoint->position-Vec3{0,1,0};obliqueCamera.up={0,1,0};obliqueCamera.nearPlane=.01f;}
    const auto tick=[&](bool giEnabled,bool aoEnabled=false) {
        submittedGi=giEnabled;submittedAo=aoEnabled;
        resources.pump();assets.updateResources(frame);renderer.advanceStreaming();prepared.beginFrame();
        EditorRenderFrameInput input;input.extracted=extracted;input.settings.environment=extracted.environment.environment;
        input.settings.exposure=extracted.environment.exposure;input.settings.proceduralSun=extracted.proceduralSun;
        auto& gi=input.settings.environment.ssgi;gi.enabled=giEnabled;
        if(!pointOnly){gi.strength=1;gi.maxDistance=3;gi.thickness=.1f;gi.quality=SsgiQuality::Medium;gi.temporalStability=.85f;gi.bounces=2;}
        input.settings.environment.ssao={aoEnabled,1,.5f,SsaoQuality::Medium};
        if(pointOnly) {
            input.settings.environment.enabled=false;input.settings.environment.intensity=0;
            input.settings.environment.ambientLight.source=AmbientLightSource::Disabled;
            input.settings.environment.ambientLight.energy=0;input.settings.environment.ambientLight.skyContribution=0;
            input.settings.environment.background.show=false;input.settings.environment.glow.enabled=false;input.settings.environment.fog.enabled=false;
            input.settings.proceduralSun={};
            if(!pointsEnabled)for(auto& point:input.extracted.points)point.enabled=false;
            if(useCenterPoint){input.extracted.points.clear();if(centerPoint)input.extracted.points.push_back(*centerPoint);}
            if(useHighBudget){gi.quality=SsgiQuality::Custom;gi.rays=16;gi.steps=64;}
            if(useLinearTone)input.settings.environment.toneMapping=ToneMapping::Linear;
        }
        input.sceneCamera=useObliqueCamera?obliqueCamera:camera;input.gameCamera=gameCamera;input.sceneAspect=Aspect;
        input.sceneWidth=input.gameWidth=Width;input.sceneHeight=input.gameHeight=Height;input.sceneVisible=input.gameVisible=true;
        input.worldId=input.giWorldId=input.environmentId=1;input.gameCameraId=2;input.shadowResolution=1024;
        EditorRenderFramePreparationStats preparation;last=prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation);
        if(pointOnly)last=emissionless(last,preparation.ready);
        renderer.renderFrame(last);prepared.endFrame();frame=bgfx::frame();context.flush();
        return preparation.ready&&last->views.size()==2;
    };
    std::vector<std::shared_ptr<std::vector<std::uint8_t>>> destinations;
    const auto read=[&](unsigned view,const std::string& name) {
        const auto staging=bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        require(bgfx::isValid(staging),"Cannot allocate saved-project GI readback.");auto pixels=std::make_shared<std::vector<std::uint8_t>>(Width*Height*4);destinations.push_back(pixels);
        bgfx::blit(254,staging,0,0,view?renderer.gameViewportTexture():renderer.viewportTexture(),0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging,pixels->data());require(ready!=UINT32_MAX,"Saved-project GI readback is unsupported.");
        unsigned waited{};while(static_cast<std::int32_t>(frame-ready)<0&&waited++<120)tick(submittedGi,submittedAo);
        if(static_cast<std::int32_t>(frame-ready)<0){renderer.drainPending(frame);bgfx::destroy(staging);throw std::runtime_error("Saved-project GI readback timed out.");}
        bgfx::destroy(staging);std::vector<std::uint8_t> encoded;require(lodepng::encode(encoded,*pixels,Width,Height)==0,"Cannot encode saved-project GI image.");
        std::ofstream file(output/name,std::ios::binary);file.write(reinterpret_cast<const char*>(encoded.data()),std::streamsize(encoded.size()));require(bool(file),"Cannot save GI image.");return pixels;
    };
    Json report={{"scene",sceneId},{"models",extracted.models.size()},{"points",extracted.points.size()},{"directionals",extracted.directional.size()},
        {"scene_camera",{{"position",camera.position},{"target",camera.target},{"up",camera.up}}},{"game_camera",{{"position",gameCamera.position},{"target",gameCamera.target},{"up",gameCamera.up}}}};
    report["point_lights"]=Json::array();for(const auto& point:extracted.points)report["point_lights"].push_back({{"position",point.position},{"radius",point.radius},{"intensity",point.intensity},
        {"depth_bias",point.shadowDepthBias},{"normal_bias",point.shadowNormalBias},{"source_radius",point.sourceRadius},{"shadows",point.castShadows},
        {"receiver_mask",point.roles.receiverMask},{"enabled",point.enabled}});
    constexpr std::array<const char*,4> names{"off","ssgi","ssao","combined"};
    const auto difference=[&](const auto& a,const auto& b) {
        double absolute{};std::size_t changed{},brightened{},darkened{};unsigned maximum{};
        for(std::size_t i=0;i<a.size();i+=4) {
            unsigned delta{};int light{};for(unsigned c=0;c<3;++c){const auto d=int(b[i+c])-int(a[i+c]);delta+=std::abs(d);light+=d;maximum=std::max(maximum,unsigned(std::abs(d)));}
            absolute+=delta;changed+=delta>=3;brightened+=light>=3;darkened+=light<=-3;
        }
        return Json{{"mean_absolute_rgb",absolute/(Width*Height*3)},{"maximum_channel",maximum},{"changed_pixels",changed},{"brightened_pixels",brightened},{"darkened_pixels",darkened},{"pixels",Width*Height}};
    };
    if(pointOnly) {
        report["diagnostic"]="ordinary point lights only; no model emission, sky/ambient, directional lights, character rigs, particles, fog, glow or SSAO";
        const auto& gi=extracted.environment.environment.ssgi;
        report["settings"]={{"ssgi_strength",gi.strength},{"ssgi_distance",gi.maxDistance},{"ssgi_thickness",gi.thickness},{"ssgi_bounces",gi.bounces},
            {"ssgi_quality",int(gi.quality)},{"ssgi_rays",gi.rays},{"ssgi_steps",gi.steps},{"ssgi_temporal_stability",gi.temporalStability},
            {"ssgi_ray_bias",gi.rayBias},{"ssgi_denoise_radius",gi.denoiseRadius},{"ssgi_denoise_strength",gi.denoiseStrength},{"ssgi_radiance_clamp",gi.radianceClamp},
            {"exposure",extracted.environment.exposure},{"authored_tone_mapping",int(extracted.environment.environment.toneMapping)}};
        struct Phase {const char* name;bool gi,points,center;bool highBudget{},oblique{},linear{};};
        std::vector<Phase> phases{{"off",false,true,false},{"ssgi",true,true,false},{"no-points-ssgi",true,false,false}};
        if(centerPoint) {
            phases.push_back({"center-off",false,true,true});phases.push_back({"center-ssgi",true,true,true});
            phases.push_back({"center-high-ssgi",true,true,true,true});
            phases.push_back({"oblique-center-off",false,true,true,false,true});
            phases.push_back({"oblique-center-ssgi",true,true,true,false,true});
            phases.push_back({"oblique-center-high-ssgi",true,true,true,true,true});
            report["center_test_light"]={{"position",centerPoint->position},{"intensity",centerPoint->intensity},{"range",centerPoint->radius},{"shadows",centerPoint->castShadows},
                {"note","One extra diagnostic light above the average coffin entity origin; replaces authored points only in center phases."}};
            report["oblique_scene_camera"]={{"position",obliqueCamera.position},{"target",obliqueCamera.target},{"up",obliqueCamera.up}};
        }
        if(inspectOnly) {
            phases={{"authored-tone-ssgi",true,true,false},{"authored-linear-ssgi",true,true,false,false,false,true}};
            if(centerPoint) {
                phases.push_back({"center-tone-ssgi",true,true,true});
                phases.push_back({"center-linear-ssgi",true,true,true,false,false,true});
                phases.push_back({"center-high-ssgi",true,true,true,true});
                phases.push_back({"oblique-center-off",false,true,true,false,true});
                phases.push_back({"oblique-center-ssgi",true,true,true,false,true});
                phases.push_back({"oblique-center-high-ssgi",true,true,true,true,true});
                phases.push_back({"oblique-center-linear-ssgi",true,true,true,false,true,true});
            }
        }
        constexpr std::array modes{RenderDebugView::Lit,RenderDebugView::DirectLighting,RenderDebugView::IndirectLighting};
        constexpr std::array<const char*,3> modeNames{"lit","direct","indirect"};
        using Image=std::shared_ptr<std::vector<std::uint8_t>>;
        std::vector<std::array<std::array<Image,2>,3>> images(phases.size());
        const auto energy=[&](const auto& values) {
            double sum{};unsigned peak{};std::size_t nonzero{};
            for(std::size_t i=0;i<values.size();i+=4) {
                unsigned light{};for(unsigned channel=0;channel<3;++channel){light+=values[i+channel];peak=std::max(peak,unsigned(values[i+channel]));}
                sum+=light;nonzero+=light>=3;
            }
            return Json{{"mean_rgb",sum/(Width*Height*3)},{"peak",peak},{"nonzero_pixels",nonzero}};
        };
        for(unsigned phase=0;phase<phases.size();++phase) {
            const auto& definition=phases[phase];const bool giEnabled=definition.gi;
            pointsEnabled=definition.points;useCenterPoint=definition.center;useHighBudget=definition.highBudget;useObliqueCamera=definition.oblique;useLinearTone=definition.linear;
            for(unsigned mode=0;mode<modes.size();++mode) {
                if(inspectOnly&&mode==0)continue;
                renderer.setDebugView(RenderViewPurpose::Scene,modes[mode]);renderer.setDebugView(RenderViewPurpose::Game,modes[mode]);
                const auto deadline=Clock::now()+std::chrono::seconds{180};unsigned warm{},settled{};
                do {
                    if(tick(giEnabled,false))++settled;else settled=0;++warm;
                    if(warm%120==0)std::cout<<definition.name<<" "<<modeNames[mode]<<" warm="<<warm<<" ready="<<settled<<" ssgi="<<renderer.ssgiStatus()
                        <<" models="<<prepared.status()<<" assets="<<assets.status()<<" queued="<<resources.stats().queued<<" running="<<resources.stats().running<<std::endl;
                    if(settled<120)std::this_thread::sleep_for(std::chrono::milliseconds{2});
                }while(settled<120&&Clock::now()<deadline);
                require(settled>=120,"Point-light saved-project diagnostic did not finish material preparation.");
                auto& phaseReport=report[definition.name];phaseReport["ssgi_status"]=renderer.ssgiStatus();phaseReport["warmup_frames"][modeNames[mode]]=warm;
                phaseReport["ssgi_bytes"]=renderer.ssgiResidentBytes();phaseReport["model_status"]=prepared.status();
                for(unsigned view=0;view<2;++view) {
                    tick(giEnabled,false);
                    const std::string viewName=view?"game":"scene";
                    auto values=read(view,std::string(definition.name)+"-"+modeNames[mode]+"-"+viewName+".png");
                    images[phase][mode][view]=values;phaseReport[modeNames[mode]][viewName]=energy(*values);
                    const char* baselineName=definition.oblique?"oblique-center-off":definition.center?"center-off":"off";
                    const auto baseline=std::find_if(phases.begin(),phases.end(),[&](const auto& candidate){return std::string(candidate.name)==baselineName;});
                    if(baseline!=phases.end())if(const auto baselineImage=images[std::size_t(baseline-phases.begin())][mode][view])
                        report["difference"][definition.name][modeNames[mode]][viewName]=difference(*baselineImage,*values);
                }
            }
            for(unsigned view=0;view<2;++view) {
                const auto& direct=*images[phase][1][view];const auto& indirect=*images[phase][2][view];double sum{};std::size_t litPixels{},indirectPixels{};
                for(std::size_t i=0;i<direct.size();i+=4) {
                    const unsigned light=unsigned(direct[i])+direct[i+1]+direct[i+2];if(light<3)continue;
                    ++litPixels;const unsigned bounce=unsigned(indirect[i])+indirect[i+1]+indirect[i+2];sum+=bounce;indirectPixels+=bounce>=3;
                }
                report[definition.name]["directly_lit_region"][view?"game":"scene"]={{"pixels",litPixels},{"indirect_nonzero_pixels",indirectPixels},{"indirect_mean_rgb",litPixels?sum/(litPixels*3):0.0}};
                auto& coffins=report[definition.name]["coffin_regions"][view?"game":"scene"];coffins=Json::array();
                for(const auto& instance:last->views[view].instances) {
                    const auto entity=document.scene().find(instance.entityId);
                    if(!entity||!entity->model||(entity->name.find("tomb")==std::string::npos&&entity->name.find("coffin")==std::string::npos)||!instance.model.asset)continue;
                    Json region{{"name",entity->name},{"entity",entity->id},{"instance_opacity",instance.opacity},{"direct_receiver_mask",instance.lightingScopes.directReceiverMask}};
                    const auto bounds=math::transformBounds(instance.model.asset->bounds,instance.model.transform);
                    const auto& viewCamera=last->views[view].settings.camera;
                    const auto matrix=viewCamera.projectionMatrix(Aspect,ClipDepth::ZeroToOne)*viewCamera.viewMatrix();
                    Vec2 minimum{float(Width),float(Height)},maximum{};bool projected{};
                    for(unsigned corner=0;corner<8;++corner) {
                        const Vec3 world{corner&1?bounds.maximum[0]:bounds.minimum[0],corner&2?bounds.maximum[1]:bounds.minimum[1],corner&4?bounds.maximum[2]:bounds.minimum[2]};
                        if(const auto pixel=math::project(world,matrix,{0,0,float(Width),float(Height)},ClipDepth::ZeroToOne)) {
                            for(unsigned axis=0;axis<2;++axis){minimum[axis]=std::min(minimum[axis],(*pixel)[axis]);maximum[axis]=std::max(maximum[axis],(*pixel)[axis]);}projected=true;
                        }
                    }
                    if(projected) {
                        const unsigned x0=unsigned(std::clamp(int(std::floor(minimum[0])),0,int(Width))),y0=unsigned(std::clamp(int(std::floor(minimum[1])),0,int(Height)));
                        const unsigned x1=unsigned(std::clamp(int(std::ceil(maximum[0])),0,int(Width))),y1=unsigned(std::clamp(int(std::ceil(maximum[1])),0,int(Height)));
                        double directSum{},indirectSum{};std::size_t nonzero{},pixels{};unsigned peak{};
                        for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x) {
                            const auto at=(std::size_t(y)*Width+x)*4;unsigned bounce{};
                            for(unsigned c=0;c<3;++c){directSum+=direct[at+c];indirectSum+=indirect[at+c];bounce+=indirect[at+c];peak=std::max(peak,unsigned(indirect[at+c]));}
                            ++pixels;nonzero+=bounce>=3;
                        }
                        region["projected_bounds"]={x0,y0,x1,y1};region["rect_pixels"]=pixels;
                        region["direct_mean_rgb"]=pixels?directSum/(pixels*3):0;region["indirect_mean_rgb"]=pixels?indirectSum/(pixels*3):0;
                        region["indirect_nonzero_pixels"]=nonzero;region["indirect_peak"]=peak;
                    }
                    coffins.push_back(std::move(region));
                }
            }
            std::cout<<definition.name<<" "<<report[definition.name].dump()<<std::endl;
        }
        report["coffins"]=Json::array();
        for(const auto& entity:document.scene().records())if(entity.model&&(entity.name.find("tomb")!=std::string::npos||entity.name.find("coffin")!=std::string::npos)) {
            Json modelReport{{"name",entity.name},{"entity",entity.id},{"asset",entity.model->id}};
            const auto found=std::find_if(last->world->shadowInstances.begin(),last->world->shadowInstances.end(),[&](const auto& instance){return instance.entityId==entity.id;});
            if(found!=last->world->shadowInstances.end()&&found->model.asset) {
                const auto bounds=math::transformBounds(found->model.asset->bounds,found->model.transform);
                modelReport["bounds"]={{"minimum",bounds.minimum},{"maximum",bounds.maximum}};modelReport["materials"]=Json::array();
                modelReport["used_material_slots"]=Json::array();for(const auto& draw:found->model.asset->draws)
                    if(draw.primitive<found->model.asset->primitives.size())modelReport["used_material_slots"].push_back(found->model.asset->primitives[draw.primitive].material);
                if(found->materials)for(std::size_t slot=0;slot<found->materials->size();++slot) {
                    const auto& material=found->materials->at(slot);
                    Json values{{"name",material.name},{"metallic",material.metallic},{"roughness",material.roughness},{"opacity",material.baseColor[3]},
                        {"shading",int(found->shading)},{"blend",int(found->blend)}};
                    if(material.graph) {
                        const auto& node=materialOutputNode(*material.graph);values["graph_shading"]=int(node.output.shading);values["graph_blend"]=int(node.output.blend);
                        values["graph_metallic"]=node.output.metallic;values["metallic_linked"]=materialInputLink(*material.graph,node.id,MaterialPin::Metallic)!=nullptr;
                    }
                    modelReport["materials"].push_back(std::move(values));
                }
            }
            for(unsigned view=0;view<2;++view)modelReport[view?"visible_in_game":"visible_in_scene"]=std::any_of(last->views[view].instances.begin(),last->views[view].instances.end(),[&](const auto& instance){return instance.entityId==entity.id;});
            report["coffins"].push_back(std::move(modelReport));
        }
        resources.stop();renderer.drainPending(frame);
        std::ofstream(output/(inspectOnly?"point-gi-inspect.json":"point-gi-project.json"))<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<std::endl;return 0;
    }
    std::array<std::array<std::shared_ptr<std::vector<std::uint8_t>>,2>,4> images;
    report["settings"]={{"ssgi_strength",1},{"ssgi_distance",3},{"ssgi_thickness",.1},{"ssgi_bounces",2},{"quality","Medium"},{"ssao_radius",.5},{"ssao_strength",1},
        {"ambient_energy",extracted.environment.environment.ambientLight.energy},{"ambient_source",int(extracted.environment.environment.ambientLight.source)}};
    for(unsigned phase=0;phase<4;++phase) {
        const bool gi=phase==1||phase==3,ao=phase>=2;const auto deadline=Clock::now()+std::chrono::seconds{120};unsigned warm{},settled{};
        do {
            if(tick(gi,ao))++settled;else settled=0;++warm;
            if(warm%120==0)std::cout<<names[phase]<<" warm="<<warm<<" ready="<<settled<<" ssgi="<<renderer.ssgiStatus()<<" ssao="<<renderer.ssaoStatus()
                <<" models="<<prepared.status()<<" assets="<<assets.status()<<" queued="<<resources.stats().queued<<" running="<<resources.stats().running<<std::endl;
            if(settled<120)std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }while(settled<120&&Clock::now()<deadline);
        const std::string name=names[phase];report[name]["warmup_frames"]=warm;report[name]["ready"]=settled>=120;
        report[name]["ssgi_status"]=renderer.ssgiStatus();report[name]["ssao_status"]=renderer.ssaoStatus();report[name]["model_status"]=prepared.status();report[name]["asset_status"]=assets.status();
        report[name]["ssgi_bytes"]=renderer.ssgiResidentBytes();report[name]["ssao_bytes"]=renderer.ssaoResidentBytes();report[name]["environment_bytes"]=renderer.environmentResidentBytes();
        std::vector<double> times;std::uint32_t lastGpuFrame{};
        for(unsigned i=0;i<60;++i){tick(gi,ao);const auto* stats=bgfx::getStats();if(stats->gpuFrameNum&&stats->gpuFrameNum!=lastGpuFrame&&stats->gpuTimerFreq>0&&stats->gpuTimeEnd>=stats->gpuTimeBegin){times.push_back(double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000/stats->gpuTimerFreq);lastGpuFrame=stats->gpuFrameNum;}}
        if(!times.empty()){std::sort(times.begin(),times.end());report[name]["gpu_ms_mean"]=std::accumulate(times.begin(),times.end(),0.0)/times.size();report[name]["gpu_ms_p95"]=times[times.size()*95/100];report[name]["gpu_samples"]=times.size();}
        for(unsigned view=0;view<2;++view) {
            report[name][view?"game_models":"scene_models"]=last->views[view].instances.size();
            tick(gi,ao);images[phase][view]=read(view,name+(view?"-game.png":"-scene.png"));
            if(phase)report["difference"][name][view?"game":"scene"]=difference(*images[0][view],*images[phase][view]);
        }
        std::cout<<name<<" "<<report[name].dump()<<std::endl;
    }
    for(const auto mode:{RenderDebugView::Albedo,RenderDebugView::DirectLighting,RenderDebugView::IndirectLighting}) {
        renderer.setDebugView(RenderViewPurpose::Scene,mode);renderer.setDebugView(RenderViewPurpose::Game,mode);
        const std::string prefix=mode==RenderDebugView::Albedo?"albedo":mode==RenderDebugView::DirectLighting?"direct":"indirect";
        for(unsigned phase=0;phase<4;++phase) {
            if(mode!=RenderDebugView::IndirectLighting&&phase!=3)continue;
            const bool gi=phase==1||phase==3,ao=phase>=2;
            for(unsigned i=0;i<(gi?120u:8u);++i)tick(gi,ao);
            for(unsigned view=0;view<2;++view) {
                tick(gi,ao);const auto values=read(view,prefix+"-"+names[phase]+(view?"-game.png":"-scene.png"));
                double sum{};unsigned peak{};std::size_t nonzero{};
                for(std::size_t i=0;i<values->size();i+=4){unsigned light{};for(unsigned c=0;c<3;++c){light+=(*values)[i+c];peak=std::max(peak,unsigned((*values)[i+c]));}sum+=light;nonzero+=light>=3;}
                report["debug"][prefix][names[phase]][view?"game":"scene"]={{"mean_rgb",sum/(Width*Height*3)},{"peak",peak},{"nonzero_pixels",nonzero}};
            }
        }
    }
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    // Ordinary point shadows can be inspected independently from GI. Overrides
    // affect only the extracted copy, never the frozen scene or user project.
    for(auto& point:extracted.points)point.castShadows=false;
    for(unsigned i=0;i<16;++i)tick(false);
    for(unsigned view=0;view<2;++view){tick(false);read(view,view?"point-shadows-off-game.png":"point-shadows-off-scene.png");}
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::DirectLighting);renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
    for(unsigned i=0;i<8;++i)tick(false);
    for(unsigned view=0;view<2;++view){tick(false);read(view,view?"point-shadows-off-direct-game.png":"point-shadows-off-direct-scene.png");}
    resources.stop();renderer.drainPending(frame);std::ofstream(output/"screen-lighting-project.json")<<report.dump(2)<<'\n';std::cout<<report.dump(2)<<std::endl;return 0;
}
}
