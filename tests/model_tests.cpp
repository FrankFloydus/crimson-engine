#include "assets/ModelAsset.h"
#include "assets/ModelAnimation.h"
#include "assets/ModelMaterialOverrides.h"
#include "assets/ModelMaterialSlots.h"
#include "assets/MaterialAsset.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
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
    size_t count=0;
    for(const auto& entry:crimson::storedAssetPaths(root))if(entry.file.filename()=="manifest.json"&&fs::is_regular_file(entry.file))++count;
    const auto staging=crimson::resolveAssetPath(root,"assets/.staging-models");
    if(fs::exists(staging))for(const auto& item:fs::directory_iterator(staging))
        require(!item.path().filename().string().starts_with(".staging-"),"Failed import left a staging directory");
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

void uvInterpolationAttributes(const fs::path& base) {
    using crimson::Vec4;
    const std::vector<Vec4> coordinates{{1,0,1,0},{0,2,2,1.57079637f},{1,1,1.41421356f,.78539816f},{},{},{}};
    const Vec4 coefficient{2,.3f,-.5f,1.25f};const std::vector<Vec4> coefficients{coefficient,coefficient,coefficient,{},{},{}};
    const std::vector<float> modes{1,1,1,0,0,0};
    Triangle interleaved(true,6);interleaved.addAttribute("TEXCOORD_0",std::vector<std::array<float,2>>(6,{.25f,.75f}),"VEC2");
    Bytes packed;for(std::size_t vertex=0;vertex<coordinates.size();++vertex){append(packed,coordinates[vertex]);append(packed,coefficients[vertex]);}
    const auto view=interleaved.view(packed,8*sizeof(float));
    interleaved.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COORD"]=interleaved.accessor(view,6,"VEC4");
    interleaved.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COEFF"]=interleaved.accessor(view,6,"VEC4",5126,4*sizeof(float));
    interleaved.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",modes,"SCALAR");
    const auto source=interleaved.save(base/"interleaved");const auto asset=crimson::importModelPackage(source,base/"project");
    const auto verify=[&](const ModelAsset& value){const auto& vertices=value.primitives.at(0).vertices;require(vertices.size()==6,"Circular attributes changed source correspondence");
        for(std::size_t vertex=0;vertex<vertices.size();++vertex){require(vertices[vertex].uvInterpolationCoordinates==coordinates[vertex]&&vertices[vertex].uvInterpolationCorrection==coefficients[vertex]&&vertices[vertex].uvInterpolationMode==modes[vertex],
            "Circular attributes were dropped, flipped or reordered");near(vertices[vertex].uv0[1],.75f);}};
    verify(*asset);verify(*crimson::loadModelPackage(asset->id,base/"project"));
    auto deformed=*asset;deformed.primitives[0]=crimson::deformModelPrimitive(*asset,0,0,crimson::evaluateModelPose(*asset));verify(deformed);

    Triangle sparse(false,6);const std::array<std::array<float,3>,3> secondTriangle{{{2,0,0},{3,0,0},{2,1,0}}};
    std::memcpy(sparse.bytes.data()+3*3*sizeof(float),secondTriangle.data(),sizeof(secondTriangle));
    sparse.addAttribute("TEXCOORD_0",std::vector<std::array<float,2>>(6,{.25f,.75f}),"VEC2");
    const auto indices=sparse.view(Bytes{0,1,2});
    const auto sparseAttribute=[&]<class T>(const char* semantic,const std::vector<T>& values,const char* type){
        Bytes encoded;for(std::size_t vertex=0;vertex<3;++vertex)append(encoded,values[vertex]);
        const auto valueView=sparse.view(encoded),accessor=sparse.json["accessors"].size();
        sparse.json["accessors"].push_back({{"count",6},{"type",type},{"componentType",5126},
            {"sparse",{{"count",3},{"indices",{{"bufferView",indices},{"componentType",5121}}},{"values",{{"bufferView",valueView}}}}}});
        sparse.primitive()["attributes"][semantic]=accessor;
    };
    sparseAttribute("_CRIMSON_UV_INTERPOLATION_COORD",coordinates,"VEC4");sparseAttribute("_CRIMSON_UV_INTERPOLATION_COEFF",coefficients,"VEC4");sparseAttribute("_CRIMSON_UV_INTERPOLATION_MODE",modes,"SCALAR");
    verify(*crimson::importModelPackage(sparse.save(base/"sparse"),base/"sparse-project"));

    const auto reject=[&](Triangle fixture,const char* name,const char* error){rejects([&]{crimson::importModelPackage(fixture.save(base/name),base/"invalid-project");},error);};
    auto unpaired=interleaved;unpaired.primitive()["attributes"].erase("_CRIMSON_UV_INTERPOLATION_COEFF");reject(unpaired,"unpaired","must be paired");
    auto absentMode=interleaved;absentMode.primitive()["attributes"].erase("_CRIMSON_UV_INTERPOLATION_MODE");reject(absentMode,"missing-mode","paired with their mode");
    auto absentUv=interleaved;absentUv.primitive()["attributes"].erase("TEXCOORD_0");reject(absentUv,"missing-uv","require UV0");
    auto malformed=interleaved;malformed.json["accessors"][malformed.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COORD"].get<std::size_t>()]["type"]="VEC3";
    reject(malformed,"shape","matching FLOAT VEC4");
    malformed=interleaved;malformed.json["accessors"][malformed.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COORD"].get<std::size_t>()]["componentType"]=5123;
    malformed.json["accessors"][malformed.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COORD"].get<std::size_t>()]["normalized"]=true;
    reject(malformed,"normalized","matching FLOAT VEC4");
    auto mixed=coordinates;mixed[0]={};Triangle mixedTriangle; mixedTriangle.addAttribute("TEXCOORD_0",std::vector<std::array<float,2>>(3),"VEC2");
    mixedTriangle.addAttribute("_CRIMSON_UV_INTERPOLATION_COORD",std::vector<Vec4>{mixed[0],mixed[1],mixed[2]},"VEC4");
    mixedTriangle.addAttribute("_CRIMSON_UV_INTERPOLATION_COEFF",std::vector<Vec4>{{},coefficient,coefficient},"VEC4");
    mixedTriangle.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{0,1,1},"SCALAR");
    reject(mixedTriangle,"mixed","one interpolation field");
    auto changed=coefficients;changed[0][0]+=1;auto different=interleaved;different.addAttribute("_CRIMSON_UV_INTERPOLATION_COEFF",changed,"VEC4");
    reject(different,"different-coefficients","one interpolation field");
    changed=coefficients;changed[0][0]=std::numeric_limits<float>::infinity();auto nonfinite=interleaved;nonfinite.addAttribute("_CRIMSON_UV_INTERPOLATION_COEFF",changed,"VEC4");
    reject(nonfinite,"nonfinite","nonfinite vertex");

    const std::vector<Vec4> quadCoordinates{{0,0,0,1},{1,0,-.2f,1},{0,1,0,1},{},{},{}};
    auto quad=interleaved;quad.addAttribute("_CRIMSON_UV_INTERPOLATION_COORD",quadCoordinates,"VEC4");
    quad.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{2,2,2,0,0,0},"SCALAR");
    const auto quadAsset=crimson::importModelPackage(quad.save(base/"quad"),base/"quad-project");
    for(std::size_t vertex=0;vertex<quadCoordinates.size();++vertex)require(quadAsset->primitives[0].vertices[vertex].uvInterpolationCoordinates==quadCoordinates[vertex]&&
        quadAsset->primitives[0].vertices[vertex].uvInterpolationCorrection==coefficients[vertex]&&quadAsset->primitives[0].vertices[vertex].uvInterpolationMode==(vertex<3?2.f:0.f),
        "Quad interpolation lost its mode or nonpositive coordinate Z.");
    auto bothCoordinates=coordinates,bothCoefficients=coefficients;
    for(std::size_t vertex=0;vertex<3;++vertex){bothCoordinates[vertex+3]=quadCoordinates[vertex];bothCoefficients[vertex+3]=coefficient;}
    auto both=interleaved;both.addAttribute("_CRIMSON_UV_INTERPOLATION_COORD",bothCoordinates,"VEC4");both.addAttribute("_CRIMSON_UV_INTERPOLATION_COEFF",bothCoefficients,"VEC4");
    both.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{1,1,1,2,2,2},"SCALAR");
    const auto bothAsset=crimson::importModelPackage(both.save(base/"both-modes"),base/"both-project");
    for(std::size_t vertex=0;vertex<6;++vertex)require(bothAsset->primitives[0].vertices[vertex].uvInterpolationMode==(vertex<3?1.f:2.f),"Separate circular/quad triangles could not share a primitive.");
    auto mixedMode=interleaved;mixedMode.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{1,2,1,0,0,0},"SCALAR");
    reject(mixedMode,"mixed-mode","one interpolation field");
    auto invalidMode=interleaved;invalidMode.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{1.5f,1,1,0,0,0},"SCALAR");
    reject(invalidMode,"fractional-mode","invalid UV interpolation corner");
    invalidMode=interleaved;invalidMode.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{3,1,1,0,0,0},"SCALAR");
    reject(invalidMode,"unknown-mode","invalid UV interpolation corner");
    invalidMode=interleaved;invalidMode.addAttribute("_CRIMSON_UV_INTERPOLATION_MODE",std::vector<float>{0,0,0,0,0,0},"SCALAR");
    reject(invalidMode,"inactive-attributes","invalid UV interpolation corner");
    auto modeShape=interleaved;
    modeShape.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_MODE"]=modeShape.primitive()["attributes"]["_CRIMSON_UV_INTERPOLATION_COORD"];
    reject(modeShape,"mode-shape","matching FLOAT SCALAR");

    auto legacy=interleaved;auto& attributes=legacy.primitive()["attributes"];
    attributes["_CRIMSON_CIRCULAR_COORD"]=attributes["_CRIMSON_UV_INTERPOLATION_COORD"];
    attributes["_CRIMSON_CIRCULAR_COEFF"]=attributes["_CRIMSON_UV_INTERPOLATION_COEFF"];
    auto ambiguous=legacy;reject(ambiguous,"ambiguous","ambiguous legacy and current");
    attributes.erase("_CRIMSON_UV_INTERPOLATION_COORD");attributes.erase("_CRIMSON_UV_INTERPOLATION_COEFF");attributes.erase("_CRIMSON_UV_INTERPOLATION_MODE");
    verify(*crimson::importModelPackage(legacy.save(base/"legacy-circle"),base/"legacy-project"));
}

void hierarchyAndManagedReopening(const fs::path& base) {
    const auto sources=base/"source"; const auto project=base/"project"; auto fixtures=writeModelFixtures(sources);
    const auto asset=crimson::importModelPackage(fixtures.gltf,project);
    require(crimson::isAssetUuid(asset->id)&&std::all_of(asset->images.begin(),asset->images.end(),[](const auto& id){return crimson::isAssetUuid(id);}),"Model import published filesystem paths instead of UUID asset references.");
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
    for (const auto& image:reopened->images) require(fs::is_regular_file(crimson::resolveAssetPath(project,image)),"Managed image file missing");
    require(packages(project)==2,"Unexpected model package count");
    const auto image=crimson::resolveAssetPath(project,reopened->images[0]); fs::rename(image,image.string()+".removed");
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
void usedMaterialSlotPresentation(const fs::path& base) {
    using namespace crimson;
    const auto project=base/"project";
    Triangle explicitMaterial;
    explicitMaterial.json["materials"]=Json::array({{{"name","Source material"}}});
    explicitMaterial.primitive()["material"]=0;
    const auto one=importModelPackage(explicitMaterial.save(base/"explicit"),project);
    require(one->materials.size()==1&&one->materialIds.size()==1&&one->primitives[0].material==0,
        "A fully assigned mesh created an unused fallback material slot.");
    require(one->legacyUnusedMaterialSlot==1,"Implicit imported slots lost their legacy override compatibility index.");
    const auto manifest=resolveAssetPath(project,one->id);
    const auto manifestBefore=readBytes(manifest);
    const auto materialIds=one->materialIds;
    require(usedModelMaterialSlots(*one)==std::vector<std::size_t>{0},
        "An unused imported fallback appeared as an editable material slot.");
    require(one->materialIds==materialIds&&one->materials.size()==1&&readBytes(manifest)==manifestBefore,
        "Presenting used slots changed immutable package data or material identities.");
    require(usedModelMaterialSlots(*loadModelPackage(one->id,project))==std::vector<std::size_t>{0},
        "Managed reopen exposed an unused fallback slot.");

    Triangle noMaterial;
    const auto fallback=importModelPackage(noMaterial.save(base/"unassigned"),project);
    require(fallback->materials.size()==1&&fallback->primitives[0].material==0&&
        usedModelMaterialSlots(*fallback)==std::vector<std::size_t>{0},
        "A fallback actually used by materialless geometry was hidden.");

    Triangle sparse;
    sparse.json["materials"]=Json::array({{{"name","Unused first"}},{{"name","Same name"}},
        {{"name","Unused middle"}},{{"name","Same name"}}});
    const std::array<std::array<float,4>,4> sourceColors{{{.1f,.2f,.3f,1},{.4f,.5f,.6f,1},{.7f,.8f,.9f,1},{.3f,.6f,.2f,1}}};
    const std::array<float,4> sourceMetallic{.1f,.25f,.5f,.75f},sourceRoughness{.9f,.7f,.4f,.2f};
    for(std::size_t slot=0;slot<4;++slot)sparse.json["materials"][slot]["pbrMetallicRoughness"]={
        {"baseColorFactor",sourceColors[slot]},{"metallicFactor",sourceMetallic[slot]},{"roughnessFactor",sourceRoughness[slot]}};
    sparse.primitive()["material"]=3;
    const auto first=sparse.primitive();auto second=first;second["material"]=1;
    sparse.json["meshes"][0]["primitives"].push_back(second);
    sparse.json["meshes"][0]["primitives"].push_back(first);
    const auto multiple=importModelPackage(sparse.save(base/"sparse"),project);
    ModelMaterialOverrides overrides;
    setModelMaterialOverride(overrides,3,AssetRef<MaterialAsset>{multiple->materialIds[1]});
    const auto overridesBefore=overrides;
    const auto idsBefore=multiple->materialIds;
    const auto slots=usedModelMaterialSlots(*multiple);
    require(slots==std::vector<std::size_t>{1,3},
        "Material slots were renumbered, repeated per primitive or merged by equal names.");
    require(multiple->materials[slots[0]].name==multiple->materials[slots[1]].name&&
        multiple->materialIds[slots[0]]!=multiple->materialIds[slots[1]],
        "Sparse fixture must retain distinct source slots with equal display names.");
    require(overrides[slots[1]].id==multiple->materialIds[1]&&overrides==overridesBefore&&
        multiple->materialIds==idsBefore&&multiple->primitives[0].material==3&&multiple->primitives[1].material==1,
        "Used-slot presentation changed original material/override indices.");
    auto metadata=*multiple;
    for(auto& primitive:metadata.primitives){primitive.vertices.clear();primitive.indices.clear();}
    require(usedModelMaterialSlots(metadata)==slots,
        "Metadata-only streamed primitives lost their editable material slots.");
    require(multiple->materials.size()==4&&multiple->materialIds.size()==4,
        "Removing the unused loader fallback compacted authored material slots.");
    const auto checkSourceFactors=[&](const ModelAsset& model) {
        require(model.materials.size()==4,"Unused source material factors were discarded.");
        for(std::size_t slot=0;slot<4;++slot){
            for(std::size_t channel=0;channel<4;++channel)near(model.materials[slot].baseColor[channel],sourceColors[slot][channel]);
            near(model.materials[slot].metallic,sourceMetallic[slot]);near(model.materials[slot].roughness,sourceRoughness[slot]);
        }
    };
    checkSourceFactors(*multiple);checkSourceFactors(*loadModelPackage(multiple->id,project));
}
void materialSlotCompatibility(const fs::path& base) {
    using namespace crimson;
    const auto project=base/"project";
    Triangle assigned;assigned.json["materials"]=Json::array({{{"name","Authored material"}}});assigned.primitive()["material"]=0;
    const auto imported=importModelPackage(assigned.save(base/"assigned"),project);
    const auto manifest=resolveAssetPath(project,imported->id);
    const auto original=readJson(manifest);auto description=original;
    const auto authoredId=imported->materialIds.at(0);
    const std::array<std::string,2> legacyFallbacks{DefaultMaterialId,importedMaterialId(imported->id,1)};
    for(const auto& fallbackId:legacyFallbacks) {
        description["materialAssets"]=Json::array({authoredId,fallbackId});writeJson(manifest,description);
        const auto immutableBytes=readBytes(manifest);
        std::vector<std::string> resolved;
        const ModelResolvers resolver{{},[&](const std::string& id)->std::optional<ModelMaterial>{
            resolved.push_back(id);ModelMaterial material;material.name="Retained native assignment";material.roughness=.37f;return material;
        }};
        const auto old=loadModelPackage(imported->id,project,resolver);
        require(old->materials.size()==1&&old->materialIds==std::vector<std::string>{authoredId}&&old->primitives[0].material==0&&
            old->legacyUnusedMaterialSlot==1,"Legacy fallback conversion changed actual slots or their persistent identities.");
        require(resolved==std::vector<std::string>{authoredId}&&old->materials[0].name=="Retained native assignment",
            "Legacy loading resolved the unused fallback or lost an assigned native material.");
        near(old->materials[0].roughness,.37f);
        const auto metadata=loadModelMaterialMetadata(imported->id,project);
        require(metadata->materials.size()==1&&metadata->materialIds==old->materialIds&&metadata->legacyUnusedMaterialSlot==1,
            "Material-only loading disagreed with legacy full-model slot conversion.");
        require(readBytes(manifest)==immutableBytes,"Legacy material conversion rewrote an immutable package.");
    }
    description["materialAssets"]=Json::array({authoredId});writeJson(manifest,description);
    const auto current=loadModelPackage(imported->id,project);
    require(current->materials.size()==1&&current->materialIds==std::vector<std::string>{authoredId}&&!current->legacyUnusedMaterialSlot,
        "Current explicit slot mappings created a phantom fallback or compatibility slot.");
    description["materialAssets"].push_back(derivedAssetId(imported->id,"unexpected-material"));writeJson(manifest,description);
    rejects([&]{loadModelPackage(imported->id,project);},"unexpected extra material slot");
    description["materialAssets"]=Json::array({authoredId,DefaultMaterialId,DefaultMaterialId});writeJson(manifest,description);
    rejects([&]{loadModelPackage(imported->id,project);},"must match the material slots");
    description["materialAssets"]=Json::array({authoredId,7});writeJson(manifest,description);
    rejects([&]{loadModelPackage(imported->id,project);},"array of material identities");
    description["materialAssets"]=Json::array();writeJson(manifest,description);
    rejects([&]{loadModelPackage(imported->id,project);},"must match the material slots");
    writeJson(manifest,original);

    Triangle mixed;mixed.json["materials"]=Json::array({{{"name","Assigned"}}});mixed.primitive()["material"]=0;
    auto unassigned=mixed.primitive();unassigned.erase("material");mixed.json["meshes"][0]["primitives"].push_back(unassigned);
    const auto actualFallback=importModelPackage(mixed.save(base/"mixed"),project);
    require(actualFallback->materials.size()==2&&actualFallback->materialIds.size()==2&&actualFallback->primitives[0].material==0&&
        actualFallback->primitives[1].material==1&&!actualFallback->legacyUnusedMaterialSlot,
        "Materialless geometry lost its real default slot or changed authored material indices.");
    const auto mixedManifest=resolveAssetPath(project,actualFallback->id);auto mixedDescription=readJson(mixedManifest);
    mixedDescription["materialAssets"]=Json::array({actualFallback->materialIds[0],DefaultMaterialId});writeJson(mixedManifest,mixedDescription);
    const auto explicitFallback=loadModelPackage(actualFallback->id,project);
    require(explicitFallback->materials.size()==2&&explicitFallback->materialIds[1]==DefaultMaterialId&&
        explicitFallback->primitives[1].material==1&&!explicitFallback->legacyUnusedMaterialSlot,
        "Explicit mapping discarded a fallback that geometry really references.");
    mixedDescription["materialAssets"].push_back(DefaultMaterialId);writeJson(mixedManifest,mixedDescription);
    rejects([&]{loadModelPackage(actualFallback->id,project);},"must match the material slots");
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
    Triangle shortTail;Bytes packed;
    for(const auto& vertexPosition:std::array<std::array<float,3>,3>{{{0,0,0},{1,0,0},{0,1,0}}}) {
        if(!packed.empty())append(packed,std::array<float,2>{999,888});
        append(packed,vertexPosition);
    }
    const auto shortPosition=shortTail.primitive()["attributes"]["POSITION"].get<std::size_t>();
    shortTail.json["accessors"][shortPosition]["bufferView"]=shortTail.view(packed,20);
    const auto shortTailModel=crimson::importModelPackage(shortTail.save(base/"short-tail"),base/"short-tail-project");
    near(shortTailModel->primitives[0].vertices[1].position[0],1);near(shortTailModel->primitives[0].vertices[2].position[1],1);
    for(const int component:{5121,5123,5126}) {
        Triangle rgb;Bytes encoded;
        for(std::size_t vertex=0;vertex<3;++vertex) {
            if(component==5121)append(encoded,std::array<std::uint8_t,3>{255,0,128});
            else if(component==5123)append(encoded,std::array<std::uint16_t,3>{65535,0,32768});
            else append(encoded,std::array<float,3>{1,0,.5f});
        }
        const auto colorAccessor=rgb.accessor(rgb.view(encoded),3,"VEC3",component);
        rgb.primitive()["attributes"]["COLOR_0"]=colorAccessor;
        if(component!=5126)rgb.json["accessors"][colorAccessor]["normalized"]=true;
        const auto named="rgb-"+std::to_string(component);
        const auto colored=crimson::importModelPackage(rgb.save(base/named),base/(named+"-project"));
        for(const auto& vertex:colored->primitives[0].vertices){near(vertex.color[0],1);near(vertex.color[1],0);near(vertex.color[3],1);}
        near(colored->primitives[0].vertices[0].color[2],component==5121?128.f/255:component==5123?32768.f/65535:.5f);
    }
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
void uv1WithoutUv0(const fs::path& base) {
    using namespace crimson;
    Triangle triangle;
    const std::vector<std::array<float,2>> coordinates{{.125f,.25f},{.875f,.25f},{.125f,.75f}};
    triangle.addAttribute("TEXCOORD_1",coordinates,"VEC2");
    triangle.json["images"]=Json::array({{{"uri","normal.png"}}});triangle.json["textures"]=Json::array({{{"source",0}}});
    triangle.json["materials"]=Json::array({{{"name","UV1 only"},
        {"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0},{"texCoord",1}}}}},
        {"normalTexture",{{"index",0},{"texCoord",1}}}}});
    triangle.primitive()["material"]=0;
    const auto source=triangle.save(base/"source"),project=base/"project";
    writeBytes(source.parent_path()/"normal.png",png(1,1,Bytes{128,128,255,255}));
    const auto imported=importModelPackage(source,project);
    const auto checkUv1=[&](const ModelAsset& asset) {
        require(asset.primitives.size()==1&&asset.primitives[0].vertices.size()==coordinates.size(),"UV1-only import changed source geometry.");
        const auto& primitive=asset.primitives[0];
        require(primitive.uvMask==2&&primitive.tangentUv==1,"Transient UV0 became authored metadata or changed the tangent basis.");
        require(asset.materials[0].baseColorTexture.texCoord==1&&asset.materials[0].normalTexture.texCoord==1,
            "UV1-only material texture selection changed.");
        for(std::size_t vertex=0;vertex<coordinates.size();++vertex){near(primitive.vertices[vertex].uv1[0],coordinates[vertex][0]);near(primitive.vertices[vertex].uv1[1],coordinates[vertex][1]);}
    };
    checkUv1(*imported);require(warning(*imported,"MikkTSpace"),"UV1-only normal-map tangents were not generated.");
    fs::rename(base/"source",base/"source-removed");checkUv1(*loadModelPackage(imported->id,project));
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
    Bytes indices;for(const auto index:std::array<std::uint16_t,6>{0,1,2,3,1,2})append(indices,index);remapped.primitive()["indices"]=remapped.accessor(remapped.view(indices),6,"SCALAR",5123);
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
void sourceSemanticPreservation(const fs::path& base) {
    using namespace crimson;
    Triangle triangle;
    triangle.addAttribute("TANGENT",std::vector<std::array<float,4>>(3,{1,0,0,1}),"VEC4");
    triangle.addAttribute("JOINTS_0",std::vector<std::array<std::uint16_t,4>>(3,{0,1,2,3}),"VEC4",5123);
    triangle.addAttribute("WEIGHTS_0",std::vector<std::array<float,4>>(3,{.05f,.1f,.15f,.2f}),"VEC4");
    triangle.addAttribute("JOINTS_1",std::vector<std::array<std::uint16_t,4>>(3,{4,5,6,7}),"VEC4",5123);
    triangle.addAttribute("WEIGHTS_1",std::vector<std::array<float,4>>(3,{.05f,.1f,.15f,.2f}),"VEC4");
    const auto vec3=[&](std::array<float,3> value) {
        Bytes encoded;for(std::size_t vertex=0;vertex<3;++vertex)append(encoded,value);
        return triangle.accessor(triangle.view(encoded),3,"VEC3");
    };
    triangle.primitive()["targets"]=Json::array({
        {{"POSITION",vec3({0,0,1})},{"NORMAL",vec3({.1f,0,0})},{"TANGENT",vec3({0,.1f,0})}},
        {{"POSITION",vec3({1,0,0})}}});
    triangle.json["meshes"][0]["weights"]={.25,-.5};
    const std::array<float,16> affine{1,.2f,0,0, .3f,1,0,0, 0,0,-1,0, 2,3,4,1};
    triangle.json["nodes"]=Json::array({{{"name","Affine root"},{"matrix",affine},{"children",{1,2,3,4,5,6,7,8,9,10}}},
        {{"name","Twin"},{"mesh",0},{"skin",0},{"weights",{.5,-.25}}},
        {{"name","Twin"},{"mesh",0},{"skin",0},{"weights",{-.5,.75}}}});
    for(std::size_t joint=0;joint<8;++joint)
        triangle.json["nodes"].push_back({{"name","Joint "+std::to_string(joint)},{"translation",{float(joint),0,0}}});
    triangle.json["skins"]=Json::array({{{"joints",{3,4,5,6,7,8,9,10}},{"skeleton",0}}});
    const auto values=[&](const std::vector<float>& data,const char* type,std::size_t components) {
        Bytes encoded;for(float value:data)append(encoded,value);
        return triangle.accessor(triangle.view(encoded),data.size()/components,type);
    };
    const auto times=values({0,2},"SCALAR",1);
    triangle.json["animations"]=Json::array({
        {{"name","Distinct twins"},{"samplers",Json::array({
            {{"input",times},{"output",values({0,0,0,2,0,0},"VEC3",3)}},
            {{"input",times},{"output",values({0,0,0,0,4,0},"VEC3",3)}}})},
            {"channels",Json::array({{{"sampler",0},{"target",{{"node",1},{"path","translation"}}}},
                {{"sampler",1},{"target",{{"node",2},{"path","translation"}}}}})}},
        {{"name","Signed cubic morph"},{"samplers",Json::array({{{"input",times},{"interpolation","CUBICSPLINE"},
            {"output",values({0,0, -.5f,1, 1,0, 0,-1, .5f,-1, 0,0},"SCALAR",1)}}})},
            {"channels",Json::array({{{"sampler",0},{"target",{{"node",1},{"path","weights"}}}}})}}});
    const auto project=base/"project";
    const auto asset=importModelPackage(triangle.save(base/"source"),project);
    require(asset->nodes.size()==11&&asset->nodes[1].name=="Twin"&&asset->nodes[2].name=="Twin",
        "Imported node identities were merged or renamed to disambiguate duplicate names.");
    require(asset->nodes[0].matrixAuthored,"Authored affine hierarchy was converted to decomposed TRS.");
    for(std::size_t component=0;component<affine.size();++component)near(asset->nodes[0].local[component],affine[component]);
    require(asset->draws.size()==2&&asset->meshes.size()==1,"Shared mesh placement was flattened or duplicated.");
    const auto twins=evaluateModelPose(*asset,0,1,false);
    near(twins.localTransforms[1][12],1);near(twins.localTransforms[1][13],0);
    near(twins.localTransforms[2][12],0);near(twins.localTransforms[2][13],2);
    near(asset->meshes[0].weights[0],.25f);near(asset->meshes[0].weights[1],-.5f);
    const auto bind=evaluateModelPose(*asset);
    near(bind.nodeWeights[1][0],.5f);near(bind.nodeWeights[1][1],-.25f);
    near(bind.nodeWeights[2][0],-.5f);near(bind.nodeWeights[2][1],.75f);
    require(asset->animations[1].channels.size()==1&&asset->animations[1].channels[0].components==2&&
        asset->animations[1].channels[0].interpolation==ModelAnimationInterpolation::CubicSpline,
        "Multi-target cubic morph metadata lost its source interpolation or component count.");
    const auto start=evaluateModelPose(*asset,1,0,false),middle=evaluateModelPose(*asset,1,1,false),end=evaluateModelPose(*asset,1,2,false);
    near(start.nodeWeights[1][0],-.5f);near(start.nodeWeights[1][1],1);
    // At t=1 over a two-second segment, the authored tangent terms each add .25.
    near(middle.nodeWeights[1][0],.25f);near(middle.nodeWeights[1][1],.25f);
    near(end.nodeWeights[1][0],.5f);near(end.nodeWeights[1][1],-1);
    near(middle.nodeWeights[2][0],-.5f);near(middle.nodeWeights[2][1],.75f);
    const auto& primitive=asset->primitives[0];
    require(primitive.skinWeights.size()==3&&primitive.morphTargets.size()==2,
        "Imported skin or morph vertex declarations were lost.");
    for(const auto& vertex:primitive.skinWeights) {
        std::array<float,8> jointWeights{};
        for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence) {
            require(vertex.joints[influence]<jointWeights.size(),"Imported influence references the wrong joint palette.");
            jointWeights[vertex.joints[influence]]+=vertex.weights[influence];
        }
        for(std::size_t joint=0;joint<8;++joint)near(jointWeights[joint],.05f*float(joint%4+1));
    }
    require(primitive.morphTargets[0].normals.size()==3&&primitive.morphTargets[0].tangents.size()==3,
        "Authored morph normal or tangent deltas were dropped.");
    near(primitive.morphTargets[0].normals[0][0],.1f);near(primitive.morphTargets[0].tangents[0][1],.1f);
    fs::rename(base/"source",base/"source-removed");
    const auto reopened=loadModelPackage(asset->id,project);
    require(evaluateModelPose(*reopened,1,1,false).signature==middle.signature,
        "Managed reopening changed duplicate-node or signed cubic-morph semantics.");
}
void missingTripoBindTransforms(const fs::path& base) {
    using namespace crimson;
    // Tripo's static rig export can omit every node transform while retaining
    // nonidentity inverse binds. Applying those binds to identity joints twists
    // the mesh even though POSITION already contains its intended bind shape.
    const std::array<Mat4,2> jointWorlds{{
        {0,1,0,0, -2,0,0,0, 0,0,.5f,0, 3,-1,2,1},
        {0,0,-.5f,0, 0,1.5f,0,0, 2,0,0,0, -2,4,1,1}}};
    const std::array<Mat4,2> inverseBinds{*math::inverse(jointWorlds[0]),*math::inverse(jointWorlds[1])};
    Triangle triangle;
    triangle.json["asset"]["generator"]="Tripo";
    triangle.addAttribute("JOINTS_0",std::vector<std::array<std::uint8_t,4>>(3,{0,1,0,0}),"VEC4",5121);
    triangle.addAttribute("WEIGHTS_0",std::vector<std::array<float,4>>{{.8f,.2f,0,0},{.25f,.75f,0,0},{.6f,.4f,0,0}},"VEC4");
    triangle.json["nodes"]=Json::array({
        {{"name","Root"},{"children",{1,2}}},{{"name","Mesh"},{"mesh",0},{"skin",0}},
        {{"name","Joint A"},{"children",{3}}},{{"name","Joint helper"},{"children",{4}}},{{"name","Joint B"}}});
    triangle.json["skins"]=Json::array({{{"name","Tripo rig"},{"joints",{2,4}},{"skeleton",2}}});
    const auto setInverseBinds=[](Triangle& fixture,std::size_t skin,const std::array<Mat4,2>& matrices) {
        Bytes encoded;for(const auto& matrix:matrices)append(encoded,matrix);
        fixture.json["skins"][skin]["inverseBindMatrices"]=fixture.accessor(fixture.view(encoded),matrices.size(),"MAT4");
    };
    setInverseBinds(triangle,0,inverseBinds);
    const auto checkRecovered=[&](const ModelAsset& asset) {
        require(asset.nodes.size()==5&&asset.nodes[3].parent==2&&asset.nodes[4].parent==3&&
            asset.skins.size()==1&&asset.skins[0].joints==std::vector<std::uint32_t>{2,4},
            "Tripo bind recovery flattened the hierarchy or removed the rig.");
        require(asset.nodes[2].matrixAuthored&&asset.nodes[4].matrixAuthored&&asset.animations.empty(),
            "Tripo bind recovery did not retain inferred joint matrices as a static rig.");
        require(warning(asset,"Recovered missing Tripo skeleton bind transforms"),"Recovered Tripo bind pose has no diagnostic.");
        const auto pose=evaluateModelPose(asset);
        const auto geometry=deformModelPrimitive(asset,0,1,pose);
        const std::array<Vec3,3> expectedPositions{{{0,0,0},{1,0,0},{0,1,0}}};
        const std::array<std::array<float,2>,3> expectedWeights{{{.8f,.2f},{.25f,.75f},{.6f,.4f}}};
        require(geometry.vertices.size()==expectedPositions.size()&&asset.primitives[0].skinWeights.size()==expectedPositions.size(),
            "Tripo bind recovery lost mesh vertices or skin influences.");
        for(std::size_t joint=0;joint<2;++joint)for(std::size_t component=0;component<16;++component) {
            near(pose.worldTransforms[joint?4:2][component],jointWorlds[joint][component],2e-5f);
            near(asset.skins[0].inverseBindMatrices[joint][component],inverseBinds[joint][component],2e-5f);
            near(pose.skinPalettes[1][joint][component],ModelIdentity[component],2e-5f);
        }
        for(std::size_t vertex=0;vertex<expectedPositions.size();++vertex) {
            const auto world=math::transformPoint(pose.drawTransforms[0],geometry.vertices[vertex].position);
            for(std::size_t axis=0;axis<3;++axis)near(world[axis],expectedPositions[vertex][axis],2e-5f);
            near(geometry.vertices[vertex].normal[2],1,2e-5f);
            std::array<float,2> weightsByJoint{};const auto& weights=asset.primitives[0].skinWeights[vertex];
            for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence) {
                require(weights.joints[influence]<weightsByJoint.size(),"Tripo bind recovery changed a source joint slot.");
                weightsByJoint[weights.joints[influence]]+=weights.weights[influence];
            }
            for(std::size_t joint=0;joint<2;++joint)near(weightsByJoint[joint],expectedWeights[vertex][joint]);
        }
        return pose.signature;
    };
    const auto source=base/"source"/"tripo.glb",project=base/"project";
    writeGlb(source,triangle.json,triangle.bytes);const auto original=readBytes(source);
    const auto imported=importModelPackage(source,project);const auto signature=checkRecovered(*imported);
    const auto manifestFile=resolveAssetPath(project,imported->id);const auto manifest=readJson(manifestFile);
    const auto storedOriginal=manifestFile.parent_path()/manifest.at("original").get<std::string>();
    const auto storedDocument=manifestFile.parent_path()/manifest.at("document").get<std::string>();
    const auto documentBefore=readBytes(storedDocument);
    require(readBytes(storedOriginal)==original&&readJson(storedDocument)["nodes"]==triangle.json["nodes"],
        "Tripo bind recovery rewrote the original source or authored node JSON.");
    fs::rename(source.parent_path(),base/"source-removed");
    const auto reopened=loadModelPackage(imported->id,project);
    require(checkRecovered(*reopened)==signature&&readBytes(storedDocument)==documentBefore&&readBytes(storedOriginal)==original,
        "Managed reopening changed the recovered Tripo pose or rewrote source data.");

    const auto checkUnchanged=[&](Triangle fixture,const char* name) {
        const auto asset=importModelPackage(fixture.save(base/name/"source"),base/name/"project");
        require(!warning(*asset,"Recovered missing Tripo skeleton bind transforms"),
            "Tripo compatibility recovery changed a rig outside its narrowly recognized export case.");
        for(const auto& node:asset->nodes)for(std::size_t component=0;component<16;++component)
            near(node.local[component],ModelIdentity[component]);
        const auto pose=evaluateModelPose(*asset);
        for(std::size_t joint=0;joint<2;++joint)for(std::size_t component=0;component<16;++component)
            near(pose.skinPalettes[1][joint][component],asset->skins[0].inverseBindMatrices[joint][component],2e-5f);
    };
    auto other=triangle;other.json["asset"]["generator"]="Another exporter";checkUnchanged(other,"other-generator");
    auto explicitMesh=triangle;explicitMesh.json["nodes"][1]["matrix"]=ModelIdentity.values;checkUnchanged(explicitMesh,"explicit-mesh-identity");
    auto explicitJoint=triangle;explicitJoint.json["nodes"][2]["translation"]={0,0,0};checkUnchanged(explicitJoint,"explicit-joint-identity");
    auto animated=triangle;Bytes times,translations;
    for(float time:{0.f,1.f})append(times,time);
    for(const auto& value:std::array<Vec3,2>{{{0,0,0},{1,0,0}}})append(translations,value);
    const auto input=animated.accessor(animated.view(times),2,"SCALAR"),output=animated.accessor(animated.view(translations),2,"VEC3");
    animated.json["animations"]=Json::array({{{"samplers",Json::array({{{"input",input},{"output",output}}})},
        {"channels",Json::array({{{"sampler",0},{"target",{{"node",2},{"path","translation"}}}}})}}});
    checkUnchanged(animated,"animated");
    auto missing=triangle;missing.json["skins"][0].erase("inverseBindMatrices");checkUnchanged(missing,"missing-inverse-bind");
    auto singular=triangle;auto singularBinds=inverseBinds;singularBinds[0]=ModelIdentity;singularBinds[0][0]=0;
    setInverseBinds(singular,0,singularBinds);checkUnchanged(singular,"singular-inverse-bind");
    auto conflict=triangle;conflict.json["skins"].push_back(conflict.json["skins"][0]);auto conflictBinds=inverseBinds;conflictBinds[1][12]+=2;
    setInverseBinds(conflict,1,conflictBinds);conflict.json["nodes"].push_back({{"name","Second mesh"},{"mesh",0},{"skin",1}});
    conflict.json["nodes"][0]["children"].push_back(5);checkUnchanged(conflict,"conflicting-shared-joint");
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
    const auto sidecar=readBytes(resolveAssetPath(project,material.id));const auto repeated=importModelPackage(fixture.gltf,project);promoteModelMaterials(project,*repeated);
    require(repeated->id==imported->id&&readBytes(resolveAssetPath(project,material.id))==sidecar,"Repeated BLEND import overwrote its native sidecar.");
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
    Triangle tangent; tangent.addAttribute("TANGENT",std::vector<std::array<float,4>>(3,{1,0,0,.5f}),"VEC4"); invalid(tangent,"tangent-handedness","tangent handedness");
    Triangle normalCount; normalCount.json["accessors"][normalCount.primitive()["attributes"]["NORMAL"].get<std::size_t>()]["count"]=2;
    invalid(normalCount,"normal-count","vertex attribute count or shape mismatch");
    Triangle tangentShape; tangentShape.addAttribute("TANGENT",std::vector<std::array<float,3>>(3,{1,0,0}),"VEC3");
    invalid(tangentShape,"tangent-shape","vertex attribute count or shape mismatch");
    Triangle uvShape; uvShape.addAttribute("TEXCOORD_0",std::vector<std::array<float,3>>(3,{0,0,0}),"VEC3");
    invalid(uvShape,"uv-shape","vertex attribute count or shape mismatch");
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
    const auto manifest=crimson::resolveAssetPath(project,imported->id); auto description=readJson(manifest); description["document"]="../../../../outside.gltf"; writeJson(manifest,description);
    rejects([&]{crimson::loadModelPackage(imported->id,project);},"escapes its package");
    rejects([&]{crimson::loadModelPackage(base/"manifest.json",project);},"outside the managed");
    Triangle singular; singular.json["nodes"][0]["scale"]={1,0,1}; const auto skipped=crimson::importModelPackage(singular.save(base/"singular"),project);
    require(skipped->draws.empty() && warning(*skipped,"singular"),"Singular transformed draw not skipped with diagnostic");
}
} // namespace

int main() {
    const auto root=fs::temp_directory_path()/("crimson-model-tests-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try {
        hierarchyAndManagedReopening(root/"managed"); trianglesAndIndices(root/"triangles"); usedMaterialSlotPresentation(root/"material-slots");
        materialSlotCompatibility(root/"material-compatibility");
        sparseAndInterleaved(root/"sparse"); embeddedDataAndSceneChoice(root/"data");
        generatedTangentsAndOptionalExtensions(root/"tangents"); uv1WithoutUv0(root/"uv1-only");
        uvInterpolationAttributes(root/"circular-uv");
        animationSkinAndMorph(root/"animation"); sourceSemanticPreservation(root/"semantics"); missingTripoBindTransforms(root/"tripo-bind");
        blendedMaterials(root/"transparent");invalidInputsAndRollback(root/"invalid"); uvAndPackageSafety(root/"safety");
        fs::remove_all(root); std::cout<<"Model importer checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Model importer checks failed: "<<error.what()<<"\nFixtures retained at: "<<root<<'\n'; return 1;
    }
}
