#include "assets/ModelAsset.h"
#include "assets/ModelAnimation.h"
#include "assets/MaterialAsset.h"
#include "model_fixtures.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
namespace fs=std::filesystem;
using crimson::ModelAsset;
using namespace crimson::model_test;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual,float expected,float tolerance=1e-5f) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance)
        throw std::runtime_error("Expected "+std::to_string(expected)+", got "+std::to_string(actual));
}
Json readJson(const fs::path& file) { std::ifstream in(file); Json result; in>>result; return result; }
Bytes readBytes(const fs::path& file) { std::ifstream in(file,std::ios::binary); return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()}; }
bool warning(const ModelAsset& asset,const std::string& word) {
    return std::any_of(asset.warnings.begin(),asset.warnings.end(),[&](const std::string& text){return text.find(word)!=std::string::npos;});
}
template<class Action> void rejects(Action action,const std::string& fragment) {
    try { action(); } catch (const std::exception& error) {
        if (std::string(error.what()).find(fragment)==std::string::npos)
            throw std::runtime_error("Expected rejection containing '"+fragment+"', got: "+error.what());
        return;
    }
    throw std::runtime_error("Expected rejection containing: "+fragment);
}
size_t packages(const fs::path& root) {
    size_t count=0; const auto folder=root/"assets"/"models";
    if (fs::exists(folder)) for (const auto& item:fs::directory_iterator(folder)) {
        require(!item.path().filename().string().starts_with(".staging-"),"Failed import left a staging directory");
        if (fs::exists(item.path()/"manifest.json")) ++count;
    }
    return count;
}
struct Triangle {
    Json json={{"asset",{{"version","2.0"}}},{"bufferViews",Json::array()},{"accessors",Json::array()},
        {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",Json::object()}}})}}})},
        {"nodes",Json::array({{{"mesh",0}}})},{"scenes",Json::array({{{"nodes",{0}}}})},{"scene",0}};
    Bytes bytes;
    size_t view(const Bytes& data,size_t stride=0) {
        while (bytes.size()%4) bytes.push_back(0); const auto offset=bytes.size(); bytes.insert(bytes.end(),data.begin(),data.end());
        Json value={{"buffer",0},{"byteOffset",offset},{"byteLength",data.size()}}; if (stride) value["byteStride"]=stride;
        const auto index=json["bufferViews"].size(); json["bufferViews"].push_back(value); return index;
    }
    size_t accessor(size_t bufferView,size_t count,const char* type,int component=5126,size_t offset=0) {
        const auto index=json["accessors"].size(); json["accessors"].push_back({{"bufferView",bufferView},{"byteOffset",offset},{"count",count},{"type",type},{"componentType",component}}); return index;
    }
    Json& primitive() { return json["meshes"][0]["primitives"][0]; }
    template<class T> size_t addAttribute(const char* semantic,const std::vector<T>& values,const char* type,int component=5126) {
        Bytes raw; for (const auto& value:values) append(raw,value);
        const auto index=accessor(view(raw),values.size(),type,component); primitive()["attributes"][semantic]=index; return index;
    }
    Triangle(bool normals=true,size_t count=3) {
        std::vector<std::array<float,3>> positions(count,{0,0,0}); positions[0]={0,0,0}; positions[1]={1,0,0}; positions[2]={0,1,0};
        addAttribute("POSITION",positions,"VEC3");
        if (normals) addAttribute("NORMAL",std::vector<std::array<float,3>>(count,{0,0,1}),"VEC3");
    }
    fs::path save(const fs::path& folder,const std::string& name="triangle.gltf") {
        fs::create_directories(folder); json["buffers"]=Json::array({{{"uri","triangle.bin"},{"byteLength",bytes.size()}}});
        writeBytes(folder/"triangle.bin",bytes); writeJson(folder/name,json); return folder/name;
    }
};

void hierarchyAndManagedReopening(const fs::path& base) {
    const auto sources=base/"source"; const auto project=base/"project"; auto fixtures=writeModelFixtures(sources);
    const auto asset=crimson::importModelPackage(fixtures.gltf,project);
    require(asset->name=="hierarchy" && asset->format=="glTF","Model identity metadata changed");
    require(asset->primitives.size()==2 && asset->meshes.size()==1 && asset->draws.size()==4 && asset->nodes.size()==2,"Mesh sharing or hierarchy failed");
    require(asset->roots==std::vector<uint32_t>{0},"Default scene roots changed");
    near(asset->nodes[0].local[12],-1); near(asset->draws[2].transform[12],1.5f); near(asset->draws[2].transform[0],-.65f);
    near(asset->bounds.minimum[0],-1.65f); near(asset->bounds.maximum[0],1.9225f); near(asset->bounds.maximum[1],1.885f);
    require(asset->materials[0].alphaMask && asset->materials[0].doubleSided,"Cutout/double-sided material lost");
    near(asset->materials[0].baseColor[1],.28f); near(asset->materials[0].metallic,.55f); near(asset->materials[0].roughness,.65f);
    near(asset->materials[0].normalScale,.5f); near(asset->materials[0].occlusionStrength,.7f); near(asset->materials[1].emissive[2],.025f);
    require(asset->materials[1].baseColorTexture.texCoord==1,"Per-slot UV selection lost");
    require(asset->materials[0].baseColorTexture.wrapV==crimson::ModelWrap::Mirror && asset->materials[0].baseColorTexture.magFilter==crimson::ModelFilter::Nearest,"Sampler state lost");
    require(asset->materials[0].baseColorTexture.minFilter==crimson::ModelFilter::LinearMipmapLinear && warning(*asset,"MikkTSpace"),"Mipmap sampler mode or generated-tangent diagnostic missing");
    require(asset->images.size()==3,"Managed image catalog incomplete");
    require(asset->materials[0].metallicRoughnessTexture.image==asset->materials[0].occlusionTexture.image,"Packed texture identity not shared");
    near(asset->primitives[0].vertices[0].normal[2],1); near(asset->primitives[0].vertices[0].color[0],1);
    for (const auto& p:asset->primitives) for (const auto& v:p.vertices) {
        near(std::abs(v.tangent[3]),1); require(std::isfinite(v.tangent[0]),"Generated tangents invalid");
    }
    const auto second=crimson::importModelPackage(fixtures.gltf,project); require(second->id==asset->id,"Identical complete imports must reuse their stable asset ID");
    const auto glb=crimson::importModelPackage(fixtures.glb,project); require(glb->draws.size()==2 && glb->format=="GLB" && glb->images.size()==3,"Embedded GLB import failed");
    fs::rename(sources,base/"source-removed");
    const auto reopened=crimson::loadModelPackage(asset->id,project); require(reopened->id==asset->id && reopened->draws.size()==4,"Managed reopen used original source");
    const auto reopenedGlb=crimson::loadModelPackage(glb->id,project); require(reopenedGlb->images.size()==3,"Embedded GLB reopen failed");
    for (const auto& image:reopened->images) require(fs::is_regular_file(project/image),"Managed image file missing");
    require(packages(project)==2,"Unexpected model package count");
    const auto image=project/reopened->images[0]; fs::rename(image,image.string()+".removed");
    rejects([&]{crimson::loadModelPackage(asset->id,project);},"cannot read file");
}
void trianglesAndIndices(const fs::path& base) {
    for (const int mode:{4,5,6}) for (const int component:{5121,5123,5125}) {
        Triangle t(false); Bytes values;
        for (const uint32_t index:{0,1,2}) {
            if (component==5121) append(values,static_cast<uint8_t>(index));
            else if (component==5123) append(values,static_cast<uint16_t>(index)); else append(values,index);
        }
        t.primitive()["indices"]=t.accessor(t.view(values),3,"SCALAR",component); t.primitive()["mode"]=mode;
        const auto folder=base/(std::to_string(mode)+"-"+std::to_string(component)); const auto asset=crimson::importModelPackage(t.save(folder),folder/"project");
        require(asset->primitives[0].indices.size()==3 && warning(*asset,"flat face normals"),"Triangle topology conversion failed");
        for (const auto& v:asset->primitives[0].vertices) near(v.normal[2],1);
    }
    Triangle quad(false,4);
    const auto viewOffset=quad.json["bufferViews"][0]["byteOffset"].get<size_t>();
    const std::array<float,12> quadPositions{0,0,0,1,0,0,0,1,0,1,1,0}; std::memcpy(quad.bytes.data()+viewOffset,quadPositions.data(),sizeof(quadPositions));
    quad.primitive()["mode"]=5; const auto q=crimson::importModelPackage(quad.save(base/"strip"),base/"strip-project");
    require(q->primitives[0].indices.size()==6,"Strip conversion wrong triangle count"); for (const auto& v:q->primitives[0].vertices) near(v.normal[2],1);
    Triangle large(true,65538); Bytes high;
    for (const uint32_t value:{0,1,65536}) append(high,value);
    large.primitive()["indices"]=large.accessor(large.view(high),3,"SCALAR",5125);
    const auto imported=crimson::importModelPackage(large.save(base/"large"),base/"large-project");
    require(imported->primitives[0].vertices.size()==65538 && imported->primitives[0].indices[2]==65536,"32-bit indices were truncated");
}
void sparseAndInterleaved(const fs::path& base) {
    Triangle t; const auto position=t.primitive()["attributes"]["POSITION"].get<size_t>();
    struct Padded {float position[3]; float padding[2];}; Bytes interleaved;
    for (Padded p: {Padded{{0,0,0},{999,888}},Padded{{-9,0,0},{777,666}},Padded{{0,-9,0},{555,444}}}) append(interleaved,p);
    t.json["accessors"][position]["bufferView"]=t.view(interleaved,sizeof(Padded));
    const auto indexView=t.view(Bytes{1,2}); Bytes values; append(values,std::array<float,3>{1,0,0}); append(values,std::array<float,3>{0,1,0});
    const auto valueView=t.view(values);
    t.json["accessors"][position]["sparse"]={{"count",2},{"indices",{{"bufferView",indexView},{"componentType",5121}}},{"values",{{"bufferView",valueView}}}};
    const auto uv=t.addAttribute("TEXCOORD_0",std::vector<std::array<uint16_t,2>>{{0,0},{65535,0},{0,65535}},"VEC2",5123); t.json["accessors"][uv]["normalized"]=true;
    const auto color=t.addAttribute("COLOR_0",std::vector<std::array<uint8_t,4>>{{255,128,0,255},{0,255,0,255},{0,0,255,128}},"VEC4",5121); t.json["accessors"][color]["normalized"]=true;
    Bytes indexValues{0,1,0}; const auto indexAccessor=t.accessor(t.view(indexValues),3,"SCALAR",5121);
    const auto sparseIndex=t.view(Bytes{2}), sparseValue=t.view(Bytes{2});
    t.json["accessors"][indexAccessor]["sparse"]={{"count",1},{"indices",{{"bufferView",sparseIndex},{"componentType",5121}}},{"values",{{"bufferView",sparseValue}}}};
    t.primitive()["indices"]=indexAccessor;
    const auto imported=crimson::importModelPackage(t.save(base/"valid"),base/"project");
    const auto& p=imported->primitives[0]; near(p.vertices[1].position[0],1); near(p.vertices[2].position[1],1);
    near(p.vertices[1].uv0[0],1); near(p.vertices[0].color[1],128.f/255); require(p.indices[2]==2,"Sparse index accessor not applied");
    const auto count=packages(base/"project");
    t.bytes[t.json["bufferViews"][indexView]["byteOffset"].get<size_t>()]=3;
    rejects([&]{crimson::importModelPackage(t.save(base/"invalid"),base/"project");},"sparse indices");
    require(packages(base/"project")==count,"Invalid sparse import published a package");
}
void embeddedDataAndSceneChoice(const fs::path& base) {
    Triangle t; t.json["nodes"]=Json::array({{{"mesh",0},{"translation",{4,0,0}}},{{"mesh",0},{"translation",{-4,0,0}}}});
    t.json["scenes"]=Json::array({{{"nodes",{0}}},{{"nodes",{1}}}}); t.json["scene"]=1;
    t.save(base/"source","DATA.GLTF"); t.json["buffers"][0]["uri"]="data:application/octet-stream;base64,"+base64(t.bytes); writeJson(base/"source"/"DATA.GLTF",t.json);
    fs::remove(base/"source"/"triangle.bin"); const auto imported=crimson::importModelPackage(base/"source"/"DATA.GLTF",base/"project");
    near(imported->bounds.minimum[0],-4); require(imported->roots==std::vector<uint32_t>{1},"Declared default scene ignored");
    t.json.erase("scene"); writeJson(base/"source"/"first.gltf",t.json);
    near(crimson::importModelPackage(base/"source"/"first.gltf",base/"project")->bounds.minimum[0],4);
    t.json.erase("scenes"); writeJson(base/"source"/"parentless.gltf",t.json);
    require(crimson::importModelPackage(base/"source"/"parentless.gltf",base/"project")->draws.size()==2,"Parentless scene fallback failed");
}
void generatedTangentsAndOptionalExtensions(const fs::path& base) {
    const auto fixture=writeModelFixtures(base/"source"); auto json=readJson(fixture.gltf);
    auto bytes=readBytes(base/"source"/"geometry.bin");
    while (bytes.size()%4) bytes.push_back(0); const auto offset=bytes.size();
    for (size_t i=0;i<24;++i) append(bytes,std::array<float,4>{0,1,0,-1});
    const auto view=json["bufferViews"].size(); json["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",24*16}});
    const auto accessor=json["accessors"].size(); json["accessors"].push_back({{"bufferView",view},{"componentType",5126},{"count",24},{"type","VEC4"}});
    for (auto& p:json["meshes"][0]["primitives"]) { p["attributes"].erase("NORMAL"); p["attributes"]["TANGENT"]=accessor; }
    json["buffers"][0]["byteLength"]=bytes.size(); writeBytes(base/"source"/"geometry.bin",bytes);
    json["materials"][0]["normalTexture"]["scale"]=-.75;
    json["extensionsUsed"]={"KHR_texture_basisu","CRIMSON_optional_test"};
    json["images"].push_back({{"uri","optional.ktx2"},{"mimeType","image/ktx2"}});
    json["textures"][0]["extensions"]["KHR_texture_basisu"]={{"source",3}};
    writeBytes(base/"source"/"optional.ktx2",Bytes{1,2,3,4});
    writeJson(fixture.gltf,json); const auto asset=crimson::importModelPackage(fixture.gltf,base/"project");
    near(asset->materials[0].normalScale,-.75f); require(warning(*asset,"MikkTSpace") && warning(*asset,"Optional extension"),"Required generation/fallback warning missing");
    near(asset->primitives[0].vertices[0].tangent[0],1); near(asset->primitives[0].vertices[0].tangent[1],0);
    require(asset->images.size()==3,"Optional unsupported image leaked into usable asset catalog");
    require(crimson::loadModelPackage(asset->id,base/"project")->images.size()==3,"Optional image package not self-contained");
    json["extensionsRequired"]={"KHR_texture_basisu"}; writeJson(fixture.gltf,json);
    rejects([&]{crimson::importModelPackage(fixture.gltf,base/"project");},"required extension");
}
void animationSkinAndMorph(const fs::path& base) {
    using namespace crimson;
    Triangle triangle;
    triangle.addAttribute("JOINTS_0",std::vector<std::array<std::uint8_t,4>>(3,{0,0,0,0}),"VEC4",5121);
    const auto weights=triangle.addAttribute("WEIGHTS_0",std::vector<std::array<std::uint8_t,4>>(3,{128,0,0,0}),"VEC4",5121);
    triangle.json["accessors"][weights]["normalized"]=true;
    triangle.json["nodes"]=Json::array({{{"name","Root"},{"translation",{3,0,0}},{"children",{1,2}}},
        {{"name","Mesh"},{"mesh",0},{"skin",0},{"translation",{2,0,0}},{"weights",{.5}}},
        {{"name","Joint"},{"translation",{0,1,0}}}});
    triangle.json["skins"]=Json::array({{{"name","Single Joint"},{"joints",{2}},{"skeleton",0}}});
    Bytes inverse;append(inverse,std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,1});
    triangle.json["skins"][0]["inverseBindMatrices"]=triangle.accessor(triangle.view(inverse),1,"MAT4");
    Bytes deltas;for(const auto& value:std::array<std::array<float,3>,3>{{{0,0,2},{1,0,0},{0,0,0}}})append(deltas,value);
    const auto morph=triangle.accessor(triangle.view(deltas),3,"VEC3");triangle.primitive()["targets"]=Json::array({{{"POSITION",morph}}});
    triangle.json["meshes"][0]["weights"]={.25};
    const auto values=[&](const std::vector<float>& data,const char* type,std::size_t components) {
        Bytes encoded;for(float value:data)append(encoded,value);return triangle.accessor(triangle.view(encoded),data.size()/components,type);
    };
    const auto times=values({0,2},"SCALAR",1);
    const auto channel=[&](const char* name,const char* interpolation,const char* property,std::size_t node,std::size_t output) {
        return Json{{"name",name},{"samplers",Json::array({{{"input",times},{"output",output},{"interpolation",interpolation}}})},
            {"channels",Json::array({{{"sampler",0},{"target",{{"node",node},{"path",property}}}}})}};
    };
    triangle.json["animations"]=Json::array({
        channel("Lift","LINEAR","translation",2,values({0,1,0,0,3,0},"VEC3",3)),
        channel("Step","STEP","translation",2,values({0,1,0,0,5,0},"VEC3",3)),
        channel("Cubic","CUBICSPLINE","translation",2,values({0,0,0,0,1,0,0,2,0, 0,0,0,0,3,0,0,0,0},"VEC3",3)),
        channel("Turn","LINEAR","rotation",2,values({0,0,0,1,0,0,1,0},"VEC4",4)),
        channel("Morph","LINEAR","weights",1,values({0,1},"SCALAR",1)),
        channel("Scale","LINEAR","scale",2,values({1,1,1,2,3,1},"VEC3",3)),
        channel("Cubic Turn","CUBICSPLINE","rotation",2,values({0,0,0,0,0,0,0,1,0,0,0,0, 0,0,0,0,0,0,1,0,0,0,0,0},"VEC4",4)),
        channel("Cubic Morph","CUBICSPLINE","weights",1,values({0,0,1,0,1,0},"SCALAR",1))});
    const auto source=triangle.save(base/"source"),project=base/"project";
    const auto asset=importModelPackage(source,project);
    require(asset->skins.size()==1&&asset->skins[0].joints==std::vector<std::uint32_t>{2}&&asset->animations.size()==8,"Imported skin/clip metadata was lost.");
    require(asset->nodes[1].skin==0&&asset->nodes[2].parent==0&&asset->draws[0].node==1,"Skin hierarchy/draw association was lost.");
    require(asset->primitives[0].hasSkinning&&asset->primitives[0].skinWeights.size()==3&&asset->primitives[0].morphTargets.size()==1,"Imported vertex skin or morph channels were lost.");
    near(asset->primitives[0].skinWeights[0].weights[0],1);near(asset->nodes[1].weights[0],.5f);near(asset->meshes[0].weights[0],.25f);
    const auto bind=evaluateModelPose(*asset);near(bind.drawTransforms[0][12],5);near(bind.skinPalettes[1][0][12],-2);
    auto deformed=deformModelPrimitive(*asset,0,1,bind);const auto world=math::transformPoint(bind.drawTransforms[0],deformed.vertices[0].position);
    near(world[0],3);near(world[1],0);near(world[2],1);
    const auto linear=evaluateModelPose(*asset,0,1,false);deformed=deformModelPrimitive(*asset,0,1,linear);
    const auto lifted=math::transformPoint(linear.drawTransforms[0],deformed.vertices[0].position);near(lifted[0],3);near(lifted[1],1);near(lifted[2],1);
    near(evaluateModelPose(*asset,0,3,true).skinPalettes[1][0][13],linear.skinPalettes[1][0][13]);
    near(evaluateModelPose(*asset,0,-1,false).skinPalettes[1][0][13],0);
    near(evaluateModelPose(*asset,0,3,false).skinPalettes[1][0][13],2);
    near(evaluateModelPose(*asset,1,1,false).skinPalettes[1][0][13],0);near(evaluateModelPose(*asset,1,2,false).skinPalettes[1][0][13],4);
    near(evaluateModelPose(*asset,2,1,false).skinPalettes[1][0][13],1.5f);
    const auto turn=evaluateModelPose(*asset,3,1,false);const auto direction=math::transformDirection(turn.localTransforms[2],{1,0,0});near(direction[0],0,1e-4f);near(direction[1],1,1e-4f);
    const auto morphed=evaluateModelPose(*asset,4,.5,false);near(morphed.nodeWeights[1][0],.25f);
    near(deformModelPrimitive(*asset,0,1,morphed).vertices[0].position[2],.5f);
    const auto scaled=evaluateModelPose(*asset,5,1,false);near(scaled.localTransforms[2][0],1.5f);near(scaled.localTransforms[2][5],2);
    const auto cubicTurn=evaluateModelPose(*asset,6,1,false);const auto cubicDirection=math::transformDirection(cubicTurn.localTransforms[2],{1,0,0});
    near(cubicDirection[0],0,1e-4f);near(cubicDirection[1],1,1e-4f);near(evaluateModelPose(*asset,7,1,false).nodeWeights[1][0],.75f);
    require(linear.signature==evaluateModelPose(*asset,0,1,false).signature&&linear.signature!=bind.signature,"Pose signature did not track immutable transform/morph content.");
    for(const auto& vertex:deformed.vertices) {
        const auto position=math::transformPoint(linear.drawTransforms[0],vertex.position);
        for(std::size_t axis=0;axis<3;++axis)require(position[axis]>=linear.bounds.minimum[axis]-1e-5f&&position[axis]<=linear.bounds.maximum[axis]+1e-5f,"Posed conservative bounds excluded a deformed vertex.");
    }
    fs::rename(base/"source",base/"source-removed");const auto reopened=loadModelPackage(asset->id,project);
    require(reopened->animations.size()==8&&evaluateModelPose(*reopened,0,1,false).signature==linear.signature,"Managed animated reopen depended on the external source or changed the pose.");
    auto invalid=*asset;invalid.animations[0].channels[0].times={0,0};rejects([&]{validateModelAnimationData(invalid);},"strictly increasing");
    invalid=*asset;invalid.skins[0].joints[0]=999;rejects([&]{validateModelAnimationData(invalid);},"joint");
    invalid=*asset;invalid.nodes[1].parent=-1;rejects([&]{validateModelAnimationData(invalid);},"parent");
    invalid=*asset;invalid.animations[0].channels[0].values[0]=std::numeric_limits<float>::infinity();rejects([&]{validateModelAnimationData(invalid);},"finite");
    // Cached metadata contains no vertices but retains deformation range data.
    auto metadata=*asset;for(auto& primitive:metadata.primitives){primitive.vertices.clear();primitive.indices.clear();primitive.skinWeights.clear();for(auto& target:primitive.morphTargets){target.positions.clear();target.normals.clear();target.tangents.clear();}}
    validateModelAnimationData(metadata);const auto cached=evaluateModelPose(metadata,0,1,false);
    near(cached.bounds.minimum[0],linear.bounds.minimum[0]);near(cached.bounds.maximum[2],linear.bounds.maximum[2]);

    // Core allows normalized integer rotation and morph-weight outputs.
    auto compact=triangle;Bytes rotations;
    for(const auto& value:std::array<std::array<std::int16_t,4>,2>{{{0,0,0,32767},{0,0,32767,0}}})append(rotations,value);
    const auto compactRotation=compact.accessor(compact.view(rotations),2,"VEC4",5122);compact.json["accessors"][compactRotation]["normalized"]=true;
    const auto compactWeights=compact.accessor(compact.view(Bytes{0,255}),2,"SCALAR",5121);compact.json["accessors"][compactWeights]["normalized"]=true;
    compact.json["animations"][3]["samplers"][0]["output"]=compactRotation;compact.json["animations"][4]["samplers"][0]["output"]=compactWeights;
    const auto normalized=importModelPackage(compact.save(base/"normalized-source"),base/"normalized-project");
    const auto normalizedTurn=math::transformDirection(evaluateModelPose(*normalized,3,1,false).localTransforms[2],{1,0,0});
    near(normalizedTurn[0],0,1e-4f);near(normalizedTurn[1],1,1e-4f);near(evaluateModelPose(*normalized,4,1,false).nodeWeights[1][0],.5f);
    // Mesh-node transforms cancel during skinning, including a singular one.
    auto singularSkin=triangle;singularSkin.json["nodes"][1]["scale"]={0,0,0};
    const auto singular=importModelPackage(singularSkin.save(base/"singular-skin-source"),base/"singular-skin-project");
    require(singular->draws.size()==1,"Singular skin mesh placement permanently removed valid joint-driven geometry.");
    const auto singularPose=evaluateModelPose(*singular);const auto singularGeometry=deformModelPrimitive(*singular,0,1,singularPose);
    const auto singularWorld=math::transformPoint(singularPose.drawTransforms[0],singularGeometry.vertices[0].position);
    near(singularWorld[0],world[0]);near(singularWorld[1],world[1]);near(singularWorld[2],world[2]);
    // A rigid animated node may start hidden by a zero scale and later appear.
    Triangle hidden;hidden.json["nodes"][0]["scale"]={0,0,0};Bytes hiddenTimes,hiddenScales;
    for(float value:{0.f,1.f})append(hiddenTimes,value);for(const auto& value:std::array<std::array<float,3>,2>{{{0,0,0},{1,1,1}}})append(hiddenScales,value);
    const auto hiddenInput=hidden.accessor(hidden.view(hiddenTimes),2,"SCALAR"),hiddenOutput=hidden.accessor(hidden.view(hiddenScales),2,"VEC3");
    hidden.json["animations"]=Json::array({{{"samplers",Json::array({{{"input",hiddenInput},{"output",hiddenOutput}}})},
        {"channels",Json::array({{{"sampler",0},{"target",{{"node",0},{"path","scale"}}}}})}}});
    const auto hiddenAsset=importModelPackage(hidden.save(base/"hidden-source"),base/"hidden-project");
    require(hiddenAsset->draws.size()==1,"Zero-scale animated bind node permanently lost its draw association.");
    near(evaluateModelPose(*hiddenAsset,0,0,false).drawTransforms[0][0],0);near(evaluateModelPose(*hiddenAsset,0,1,false).drawTransforms[0][0],1);

    // Eight influences and generated-normal reindexing retain skin/morph data.
    Triangle remapped(false,4);const auto position=remapped.primitive()["attributes"]["POSITION"].get<std::size_t>();
    const auto offset=remapped.json["bufferViews"][remapped.json["accessors"][position]["bufferView"].get<std::size_t>()]["byteOffset"].get<std::size_t>();
    const std::array<float,12> duplicated{0,0,0,1,0,0,0,1,0,0,0,0};std::memcpy(remapped.bytes.data()+offset,duplicated.data(),sizeof(duplicated));
    remapped.addAttribute("JOINTS_0",std::vector<std::array<std::uint16_t,4>>(4,{0,0,0,0}),"VEC4",5123);
    remapped.addAttribute("WEIGHTS_0",std::vector<std::array<float,4>>(4,{.5f,0,0,0}),"VEC4");
    remapped.addAttribute("JOINTS_1",std::vector<std::array<std::uint16_t,4>>(4,{1,0,0,0}),"VEC4",5123);
    remapped.addAttribute("WEIGHTS_1",std::vector<std::array<float,4>>(4,{.5f,0,0,0}),"VEC4");
    remapped.json["nodes"]=Json::array({{{"mesh",0},{"skin",0}},{{"translation",{0,0,0}}},{{"translation",{2,0,0}}}});
    remapped.json["scenes"][0]["nodes"]={0,1,2};remapped.json["skins"]=Json::array({{{"joints",{1,2}}}});
    Bytes indices;for(std::uint16_t index:{0,1,2,3,1,2})append(indices,index);remapped.primitive()["indices"]=remapped.accessor(remapped.view(indices),6,"SCALAR",5123);
    Bytes morphs;for(const auto& delta:std::array<std::array<float,3>,4>{{{0,0,0},{0,0,0},{0,0,0},{0,0,1}}})append(morphs,delta);
    remapped.primitive()["targets"]=Json::array({{{"POSITION",remapped.accessor(remapped.view(morphs),4,"VEC3")}}});
    remapped.json["meshes"][0]["weights"]={1};
    const auto eight=importModelPackage(remapped.save(base/"eight-source"),base/"eight-project");
    require(eight->primitives[0].vertices.size()==6&&eight->primitives[0].skinWeights.size()==6&&eight->primitives[0].morphTargets[0].positions.size()==6,
        "Normal generation merged distinct morph/skin attributes or lost vertex remapping.");
    const auto eightPose=evaluateModelPose(*eight);const auto eightDeformed=deformModelPrimitive(*eight,0,0,eightPose);
    near(eightDeformed.vertices[0].position[0],1);near(eightDeformed.vertices[3].position[2],1);
    near(eightDeformed.vertices[3].normal[0],1/std::sqrt(3.f));near(eightDeformed.vertices[3].normal[2],1/std::sqrt(3.f));
    // Invalid skin input must fail before a managed package is published.
    Triangle zero;zero.addAttribute("JOINTS_0",std::vector<std::array<std::uint8_t,4>>(3,{0,0,0,0}),"VEC4",5121);
    zero.addAttribute("WEIGHTS_0",std::vector<std::array<float,4>>(3,{0,0,0,0}),"VEC4");
    rejects([&]{importModelPackage(zero.save(base/"zero-source"),base/"zero-project");},"zero total weight");
}
void blendedMaterials(const fs::path& base) {
    using namespace crimson;
    const auto fixture=writeModelFixtures(base/"source");auto json=readJson(fixture.gltf);
    json["materials"][0]["alphaMode"]="BLEND";json["materials"][0]["alphaCutoff"]=.95;
    json["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3]=.35;
    writeJson(fixture.gltf,json);const auto project=base/"project";
    const auto imported=importModelPackage(fixture.gltf,project);
    require(imported->materials[0].alphaBlend&&!imported->materials[0].alphaMask,"glTF BLEND was rejected or treated as a cutout.");
    near(imported->materials[0].baseColor[3],.35f);promoteModelMaterials(project,*imported);
    const auto material=loadMaterialAsset(project,imported->materialIds[0]);
    require(material.blend==MaterialBlend::Alpha&&material.surface.alphaBlend&&!material.surface.alphaMask&&
        materialOutputNode(material.graph).output.blend==MaterialBlend::Alpha,"BLEND promotion lost native transparency.");
    near(material.surface.baseColor[3],.35f);
    const auto* opacity=materialInputLink(material.graph,materialOutputNode(material.graph).id,MaterialPin::Opacity);
    require(opacity!=nullptr&&material.surface.baseColorTexture.image==imported->materials[0].baseColorTexture.image&&
        material.surface.baseColorTexture.minFilter==imported->materials[0].baseColorTexture.minFilter,
        "BLEND promotion lost texture alpha, material factor or sampler.");
    const auto* expression=findMaterialNode(material.graph,opacity->fromNode);
    require(expression&&expression->kind==MaterialNodeKind::Multiply,"Textured BLEND opacity did not retain its explicit factor.");
    const auto* alpha=materialInputLink(material.graph,expression->id,MaterialPin::Left);
    require(alpha&&alpha->fromPin==MaterialPin::A&&findMaterialNode(material.graph,alpha->fromNode)->kind==MaterialNodeKind::Texture2D,
        "BLEND opacity is not connected to the base-color texture alpha.");
    const auto sidecar=readBytes(project/material.id);const auto repeated=importModelPackage(fixture.gltf,project);promoteModelMaterials(project,*repeated);
    require(repeated->id==imported->id&&readBytes(project/material.id)==sidecar,"Repeated BLEND import overwrote its native sidecar.");
    const ModelResolvers resolver{{},[&](const std::string& id)->std::optional<ModelMaterial>{return loadMaterialAsset(project,id).surface;}};
    const auto reopened=loadModelPackage(imported->id,project,resolver);
    require(reopened->materials[0].alphaBlend&&reopened->materials[0].graph&&
        materialOutputNode(*reopened->materials[0].graph).output.blend==MaterialBlend::Alpha,"Managed BLEND reload became opaque.");
    near(reopened->materials[0].baseColor[3],.35f);
    Triangle embedded;embedded.json["materials"]=Json::array({{{"alphaMode","BLEND"},
        {"pbrMetallicRoughness",{{"baseColorFactor",{.3,.4,.5,.4}}}}}});embedded.primitive()["material"]=0;
    const auto glb=base/"alpha.glb";writeGlb(glb,embedded.json,embedded.bytes);
    const auto embeddedAsset=importModelPackage(glb,project);promoteModelMaterials(project,*embeddedAsset);
    const auto embeddedMaterial=loadMaterialAsset(project,embeddedAsset->materialIds[0]);
    require(embeddedMaterial.blend==MaterialBlend::Alpha&&embeddedMaterial.surface.alphaBlend,"Embedded GLB BLEND material became opaque.");
    near(embeddedMaterial.surface.baseColor[3],.4f);
}
void invalidInputsAndRollback(const fs::path& base) {
    const auto project=base/"project";
    auto invalid=[&](Triangle t,const char* name,const char* expected) {
        const auto count=packages(project); rejects([&]{crimson::importModelPackage(t.save(base/name),project);},expected);
        require(packages(project)==count,"Failed model import published a package");
    };
    Triangle lines; lines.primitive()["mode"]=1; invalid(lines,"lines","point and line");
    Triangle morph; morph.primitive()["targets"]=Json::array({{{"POSITION",1},{"COLOR_0",1}}}); invalid(morph,"morph","morph target attribute");
    Triangle skin; skin.json["skins"]=Json::array({{{"joints",{0}}}});skin.json["nodes"][0]["skin"]=0;invalid(skin,"skin","requires JOINTS_0");
    Triangle cycle; cycle.json["nodes"]=Json::array({{{"mesh",0}},{{"children",{2}}},{{"children",{1}}}}); invalid(cycle,"cycle","cycle");
    Triangle count; count.json["accessors"][0]["count"]=99999; invalid(count,"count","accessor exceeds");
    Triangle overflow; overflow.json["bufferViews"][0]["byteOffset"]=std::numeric_limits<uint64_t>::max(); invalid(overflow,"overflow","buffer view");
    Triangle nan; const auto quiet=std::numeric_limits<float>::quiet_NaN(); std::memcpy(nan.bytes.data(),&quiet,sizeof(float)); invalid(nan,"nan","nonfinite vertex");
    Triangle shortIndices; Bytes shortData{0,1}; shortIndices.primitive()["indices"]=shortIndices.accessor(shortIndices.view(shortData),2,"SCALAR",5121); invalid(shortIndices,"indices","incomplete");
    Triangle badIndices; Bytes badData{0,1,255}; badIndices.primitive()["indices"]=badIndices.accessor(badIndices.view(badData),3,"SCALAR",5121); invalid(badIndices,"out-of-bounds","out of bounds");
    Triangle missing; auto file=missing.save(base/"missing"); fs::remove(file.parent_path()/"triangle.bin");
    rejects([&]{crimson::importModelPackage(file,project);},"cannot read file"); require(packages(project)==0,"Missing dependency left published package");
    const auto fixture=writeModelFixtures(base/"uv"); auto json=readJson(fixture.gltf);
    json["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0"); writeJson(fixture.gltf,json);
    rejects([&]{crimson::importModelPackage(fixture.gltf,project);},"missing UV");
}
void uvAndPackageSafety(const fs::path& base) {
    const auto fixture=writeModelFixtures(base/"uv"); auto json=readJson(fixture.gltf); const auto project=base/"project";
    json["materials"][0]["normalTexture"]["texCoord"]=2; writeJson(fixture.gltf,json);
    rejects([&]{crimson::importModelPackage(fixture.gltf,project);},"beyond UV1");
    json["materials"][0]["normalTexture"]["texCoord"]=0; writeJson(fixture.gltf,json);
    const auto imported=crimson::importModelPackage(fixture.gltf,project);
    const auto manifest=project/imported->id; auto description=readJson(manifest); description["document"]="../../../../outside.gltf"; writeJson(manifest,description);
    rejects([&]{crimson::loadModelPackage(imported->id,project);},"escapes its package");
    rejects([&]{crimson::loadModelPackage(base/"manifest.json",project);},"outside the managed");
    Triangle singular; singular.json["nodes"][0]["scale"]={1,0,1}; const auto skipped=crimson::importModelPackage(singular.save(base/"singular"),project);
    require(skipped->draws.empty() && warning(*skipped,"singular"),"Singular transformed draw not skipped with diagnostic");
}
} // namespace

int main() {
    const auto root=fs::temp_directory_path()/("crimson-model-tests-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try {
        hierarchyAndManagedReopening(root/"managed"); trianglesAndIndices(root/"triangles");
        sparseAndInterleaved(root/"sparse"); embeddedDataAndSceneChoice(root/"data");
        generatedTangentsAndOptionalExtensions(root/"tangents"); animationSkinAndMorph(root/"animation"); blendedMaterials(root/"transparent");invalidInputsAndRollback(root/"invalid"); uvAndPackageSafety(root/"safety");
        fs::remove_all(root); std::cout<<"Model importer checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Model importer checks failed: "<<error.what()<<"\nFixtures retained at: "<<root<<'\n'; return 1;
    }
}
