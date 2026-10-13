#include "assets/ModelAsset.h"
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
    require(warning(*asset,"Mipmap") && warning(*asset,"MikkTSpace"),"Import warnings missing");
    require(asset->images.size()==3,"Managed image catalog incomplete");
    require(asset->materials[0].metallicRoughnessTexture.image==asset->materials[0].occlusionTexture.image,"Packed texture identity not shared");
    near(asset->primitives[0].vertices[0].normal[2],1); near(asset->primitives[0].vertices[0].color[0],1);
    for (const auto& p:asset->primitives) for (const auto& v:p.vertices) {
        near(std::abs(v.tangent[3]),1); require(std::isfinite(v.tangent[0]),"Generated tangents invalid");
    }
    const auto second=crimson::importModelPackage(fixtures.gltf,project); require(second->id!=asset->id,"Repeated imports overwrite existing asset IDs");
    const auto glb=crimson::importModelPackage(fixtures.glb,project); require(glb->draws.size()==2 && glb->format=="GLB" && glb->images.size()==3,"Embedded GLB import failed");
    fs::rename(sources,base/"source-removed");
    const auto reopened=crimson::loadModelPackage(asset->id,project); require(reopened->id==asset->id && reopened->draws.size()==4,"Managed reopen used original source");
    const auto reopenedGlb=crimson::loadModelPackage(glb->id,project); require(reopenedGlb->images.size()==3,"Embedded GLB reopen failed");
    for (const auto& image:reopened->images) require(fs::is_regular_file(project/image),"Managed image file missing");
    require(packages(project)==3,"Unexpected model package count");
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
void invalidInputsAndRollback(const fs::path& base) {
    const auto project=base/"project";
    auto invalid=[&](Triangle t,const char* name,const char* expected) {
        const auto count=packages(project); rejects([&]{crimson::importModelPackage(t.save(base/name),project);},expected);
        require(packages(project)==count,"Failed model import published a package");
    };
    Triangle alpha; alpha.json["materials"]=Json::array({{{"alphaMode","BLEND"}}}); alpha.primitive()["material"]=0; invalid(alpha,"alpha","alpha-blended");
    Triangle lines; lines.primitive()["mode"]=1; invalid(lines,"lines","point and line");
    Triangle morph; morph.primitive()["targets"]=Json::array({{{"POSITION",0}}}); invalid(morph,"morph","morph targets");
    Triangle skin; skin.json["skins"]=Json::array({{{"joints",{0}}}}); invalid(skin,"skin","skins are unsupported");
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
        generatedTangentsAndOptionalExtensions(root/"tangents"); invalidInputsAndRollback(root/"invalid"); uvAndPackageSafety(root/"safety");
        fs::remove_all(root); std::cout<<"Model importer checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Model importer checks failed: "<<error.what()<<"\nFixtures retained at: "<<root<<'\n'; return 1;
    }
}
