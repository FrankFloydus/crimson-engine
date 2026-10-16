#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/ModelAnimation.h"
#include "resources/StreamingModel.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void verify(const std::filesystem::path& root) {
    using namespace crimson;
    const auto package=root/"assets/models/fixture";std::filesystem::create_directories(package);
    writeFileAtomically(package/"manifest.json",R"({"document":"model.gltf","dependencies":[{"kind":"buffer","path":"buffer.bin"}]})");
    writeFileAtomically(package/"model.gltf","{}");writeFileAtomically(package/"buffer.bin","animated fixture");
    ModelAsset asset;asset.id="assets/models/fixture/manifest.json";asset.name="Animated metadata";asset.format="glTF";
    ModelPrimitive primitive;primitive.vertices.resize(3);primitive.vertices[0].position={-1,0,0};primitive.vertices[1].position={1,0,0};primitive.vertices[2].position={0,1,0};
    primitive.indices={0,1,2};primitive.sourceVertexCount=3;primitive.sourceIndexCount=3;primitive.hasSkinning=true;
    for(const auto& vertex:primitive.vertices)math::include(primitive.bounds,vertex.position);asset.bounds=primitive.bounds;
    primitive.skinWeights.resize(3);for(auto& influences:primitive.skinWeights)influences.weights[0]=1;
    primitive.skinJointBounds={primitive.bounds};
    ModelMorphTarget target;target.positions.assign(3,{0,0,.2f});target.positionBounds={{0,0,.2f},{0,0,.2f},true};primitive.morphTargets.push_back(target);
    asset.primitives.push_back(primitive);asset.materials.push_back({});asset.materials[0].alphaBlend=true;asset.materialIds={"fixture-material"};
    asset.meshes.push_back({"Mesh",{0},{.1f}});asset.nodes.push_back({"Mesh",{}, {},0});asset.nodes[0].skin=0;asset.nodes[0].weights={.25f};
    asset.nodes.push_back(ModelNode{});asset.nodes[1].name="Joint";asset.roots={0,1};asset.draws={{0,{},0}};
    asset.skins.push_back({"Skeleton",{1},{Mat4{}},1});
    ModelAnimationChannel channel;channel.node=1;channel.components=3;channel.times={1,3};channel.values={0,0,0,0,2,0};
    asset.animations.push_back({"Lift",1,3,{channel}});validateModelAnimationData(asset);
    const auto cold=evaluateModelPose(asset,0,2,false);
    const auto stream=streaming::buildModelCache(asset,root);
    check(stream.primitives[0].levels.size()==1,"Deformation attributes were simplified with a static-only LOD policy.");
    const auto warm=streaming::loadModelMetadataCache(package/"manifest.json",root);
    check(warm&&warm->skins.size()==1&&warm->animations.size()==1&&warm->nodes[0].skin==0&&warm->draws[0].node==0,
        "Warm model metadata lost skeleton, animation or draw-node identity.");
    check(warm->primitives[0].vertices.empty()&&warm->primitives[0].skinWeights.empty()&&warm->primitives[0].morphTargets[0].positions.empty(),
        "Warm animation metadata retained heavy vertex payloads.");
    check(warm->primitives[0].hasSkinning&&warm->nodes[0].weights==asset.nodes[0].weights&&warm->meshes[0].weights==asset.meshes[0].weights,
        "Warm model metadata lost skin/morph declarations.");
    check(warm->primitives[0].skinJointBounds.size()==1,"Warm metadata lost used-joint bounds.");
    check(warm->materials[0].alphaBlend,"Warm metadata lost imported alpha blending.");
    const auto posed=evaluateModelPose(*warm,0,2,false);
    check(posed.signature==cold.signature&&posed.bounds.minimum==cold.bounds.minimum&&posed.bounds.maximum==cold.bounds.maximum,
        "Cold and warm model animation metadata produced different posed content or bounds.");
    const auto original=streaming::loadModelLod(stream.primitives[0].levels[0]);
    check(original->geometry.skinWeights.size()==primitive.skinWeights.size()&&original->geometry.skinWeights[0].weights==primitive.skinWeights[0].weights&&
        original->geometry.morphTargets[0].positions==target.positions,"Original streamed geometry lost skin/morph data.");
    const auto index=std::filesystem::directory_iterator(resolveAssetPath(root,"assets/cache/mesh-lods/index"))->path();
    std::ifstream input(index);auto invalid=nlohmann::json::parse(input);input.close();
    invalid["skins"][0]["joints"][0]=1000;writeFileAtomically(index,invalid.dump());
    check(!streaming::loadModelMetadataCache(package/"manifest.json",root),"Malformed cached skeleton was admitted as valid metadata.");
    std::ifstream source(package/"buffer.bin");const std::string bytes{std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>()};
    check(bytes=="animated fixture","Cache rejection modified a managed source buffer.");
}
}
int main() {
    const auto temporary=std::filesystem::temp_directory_path();
    const auto root=temporary/("crimson-animation-metadata-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const auto cleanup=[&]{if(std::filesystem::weakly_canonical(root).parent_path()!=std::filesystem::weakly_canonical(temporary))throw std::runtime_error("Test cleanup escaped its temporary parent.");std::filesystem::remove_all(root);};
    try{verify(root);cleanup();std::cout<<"Animated warm metadata and original streamed deformation payload passed.\n";return 0;}
    catch(const std::exception& error){try{cleanup();}catch(...){}std::cerr<<error.what()<<'\n';return 1;}
}
