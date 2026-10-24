#include "quader/QuaderPaintMigration.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const std::string& part) {
    try {operation();}catch(const std::exception& error){require(std::string{error.what()}.find(part)!=std::string::npos,"Unexpected rejection: "+std::string{error.what()});return;}
    throw std::runtime_error("Expected migration rejection: "+part);
}
ModelPrimitive square(bool quad) {
    ModelPrimitive p;p.material=0;p.vertices.resize(4);
    p.vertices[0].position={0,0,0};p.vertices[1].position={2,0,0};p.vertices[2].position={2,2,0};p.vertices[3].position={0,2,0};
    for(auto& v:p.vertices){v.normal={0,0,1};v.tangent={1,0,0,1};}
    p.indices={0,1,2,0,2,3};if(quad)p.sourceFaces.push_back({{0,1,2,3},{0,1},4});return p;
}
std::shared_ptr<ModelAsset> model(bool quad,const std::string& id) {
    auto result=std::make_shared<ModelAsset>();result->id=id;result->materialIds={"material-stone"};result->primitives.push_back(square(quad));result->draws.push_back({0});return result;
}
template<class Color> QuaderPaintInput painted(std::shared_ptr<ModelAsset> source,std::uint32_t level,Color color) {
    QuaderPaintInput input;input.sourceKey="mesh:room";input.modelId=source->id;input.model=source;input.paint.emplace();input.paint->subdivisionLevel=level;
    for(std::uint32_t draw=0;draw<source->draws.size();++draw){const auto primitive=source->draws[draw].primitive;const auto geometry=buildMeshPaintGeometry(source->primitives[primitive],level);std::vector<std::uint8_t> bytes;
        for(const auto& vertex:geometry->vertices){const auto value=color(vertex.position,draw);bytes.insert(bytes.end(),value.begin(),value.end());}
        input.paint->draws.push_back({draw,primitive,level,geometry->sourceSignature,makeMeshPaintPayload(static_cast<std::uint32_t>(geometry->vertices.size()),bytes)});
    }
    return input;
}
void trianglePatchesToQuadGrid() {
    const auto before=model(false,"old-room"),after=model(true,"quad-room");
    auto input=painted(before,3,[](Vec3 p,unsigned){return MeshPaintRgb{static_cast<std::uint8_t>(std::lround(p[0]*60)),static_cast<std::uint8_t>(std::lround(p[1]*30)),0};});
    const auto oldBytes=meshPaintBytes(*input.paint->draws[0].weights);const auto result=migrateQuaderPaint(input,*after);
    require(result.sourceKey==input.sourceKey&&result.beforeModelId==before->id&&result.before==input.paint,"Migration lost the exact immutable baseline.");
    require(result.after&&result.after->subdivisionLevel==3&&result.after->draws.size()==1,"Migration changed the authored level or draw set.");
    const auto geometry=buildMeshPaintGeometry(after->primitives[0],3);const auto& stored=result.after->draws[0];
    require(stored.sourceSignature==geometry->sourceSignature&&stored.weights->vertexCount==geometry->vertices.size(),"Migration did not publish corrected quad provenance.");
    for(std::uint32_t vertex=0;vertex<geometry->vertices.size();++vertex){const auto p=geometry->vertices[vertex].position;const auto value=stored.weights->rgb(vertex);require(std::abs(int(value[0])-std::lround(p[0]*60))<=1&&std::abs(int(value[1])-std::lround(p[1]*30))<=1,"Affine paint changed during physical-surface transfer.");}
    require(result.maxError<.01f&&result.rmsError<.005f,"Affine transfer reported excessive interpolation error.");
    require(meshPaintBytes(*input.paint->draws[0].weights)==oldBytes,"Migration mutated saved paint.");
}
void bilinearSamplingAcrossReindexing() {
    const auto before=model(true,"before"),after=model(true,"after");auto& target=after->primitives[0];
    std::rotate(target.vertices.begin(),target.vertices.begin()+1,target.vertices.end());target.indices={3,0,1,3,1,2};target.sourceFaces={{{3,0,1,2},{0,1},4}};
    auto input=painted(before,0,[](Vec3 p,unsigned){return MeshPaintRgb{static_cast<std::uint8_t>(p[0]==2&&p[1]==2?255:0),0,0};});
    const auto result=migrateQuaderPaint(input,*after);
    require(result.maxError<1e-5f&&result.rmsError<1e-5f,"Migration sampled a bilinear quad as its render triangles.");
    const auto geometry=buildMeshPaintGeometry(target,0);for(unsigned i=0;i<geometry->vertices.size();++i){const auto p=geometry->vertices[i].position;const auto rgb=result.after->draws[0].weights->rgb(i);require(rgb[0]==(p[0]==2&&p[1]==2?255:0),"Reindexed corner paint moved.");}
}
void unchangedPaintAndBaseRemainExact() {
    const auto before=model(true,"before"),after=model(true,"after");
    auto input=painted(before,2,[](Vec3 p,unsigned){return MeshPaintRgb{static_cast<std::uint8_t>(p[0]*70),0,0};});
    const auto result=migrateQuaderPaint(input,*after);require(result.after==input.paint&&result.after->draws[0].weights==input.paint->draws[0].weights,"Unchanged topology did not preserve exact COW payload ownership.");
    auto base=painted(model(false,"triangle-source"),2,[](Vec3,unsigned){return MeshPaintRgb{};});const auto restored=migrateQuaderPaint(base,*after);
    for(unsigned vertex=0;vertex<restored.after->draws[0].weights->vertexCount;++vertex)require(restored.after->draws[0].weights->rgb(vertex)==MeshPaintRgb{},"Base-only surface acquired overlay paint.");
    QuaderPaintInput empty;empty.sourceKey="empty";empty.modelId="unused";const auto untouched=migrateQuaderPaint(empty,*after);require(!untouched.before&&!untouched.after,"Unpainted occurrence acquired a paint payload.");
}
void reorderedDrawsKeepTheirMaterialAndPaint() {
    const auto before=model(false,"before"),after=model(true,"after");
    before->materialIds.push_back("material-metal");before->primitives.push_back(square(false));before->primitives[1].material=1;before->draws.push_back({1});
    after->materialIds.push_back("material-metal");after->primitives.push_back(square(true));after->primitives[1].material=1;after->draws={{1},{0}};
    const auto input=painted(before,2,[](Vec3,unsigned draw){return draw?MeshPaintRgb{0,170,0}:MeshPaintRgb{90,0,0};});
    const auto result=migrateQuaderPaint(input,*after);require(result.after->draws[0].draw==1&&result.after->draws[1].draw==0,"Reordered source draws retained stale ordinals.");
    require(result.after->draws[0].weights->rgb(0)==MeshPaintRgb{90,0,0}&&result.after->draws[1].weights->rgb(0)==MeshPaintRgb{0,170,0},"Reordered material slots exchanged paint.");
}
void rejectsInvalidOrChangedSources() {
    const auto before=model(false,"before"),after=model(true,"after");const auto valid=painted(before,2,[](Vec3,unsigned){return MeshPaintRgb{100,0,0};});
    auto broken=valid;broken.paint->draws[0].sourceSignature^=1;rejects([&]{migrateQuaderPaint(broken,*after);},"signature");
    broken=valid;broken.paint->draws[0].weights=makeMeshPaintPayload(3);rejects([&]{migrateQuaderPaint(broken,*after);},"vertex count");
    broken=valid;broken.paint->draws[0].level=1;rejects([&]{migrateQuaderPaint(broken,*after);},"level");
    broken=valid;broken.paint->draws.push_back(broken.paint->draws[0]);rejects([&]{migrateQuaderPaint(broken,*after);},"draw identity");
    auto changed=*after;changed.primitives[0].vertices[2].position[0]+=1;rejects([&]{migrateQuaderPaint(valid,changed);},"no unique unchanged surface");
    changed=*after;changed.draws[0].transform=math::translation({1,0,0});rejects([&]{migrateQuaderPaint(valid,changed);},"no unique unchanged surface");
    changed=*after;changed.materialIds[0]="another-material";rejects([&]{migrateQuaderPaint(valid,changed);},"no unique unchanged surface");
    changed=*after;changed.primitives[0].hasSkinning=true;rejects([&]{migrateQuaderPaint(valid,changed);},"rigid static");
    std::stop_source stop;stop.request_stop();rejects([&]{migrateQuaderPaint(valid,*after,stop.get_token());},"canceled");
}
void rejectsOverlappingDifferentPaint() {
    const auto before=model(false,"before"),after=model(true,"after");
    for(auto* primitive:{&before->primitives[0],&after->primitives[0]}){const auto initial=primitive->vertices;primitive->vertices.insert(primitive->vertices.end(),initial.begin(),initial.end());primitive->indices.insert(primitive->indices.end(),{4,5,6,4,6,7});}
    after->primitives[0].sourceFaces.push_back({{4,5,6,7},{2,3},4});
    auto input=painted(before,1,[](Vec3,unsigned){return MeshPaintRgb{100,0,0};});const auto geometry=buildMeshPaintGeometry(before->primitives[0],1);std::vector<MeshPaintChange> changes;
    for(const auto& cell:geometry->cells)if(cell.sourceFace>=2)for(unsigned corner=0;corner<cell.cornerCount;++corner)changes.push_back({cell.vertices[corner],{0,160,0}});
    input.paint->draws[0].weights=updateMeshPaintPayload(input.paint->draws[0].weights,changes);
    rejects([&]{migrateQuaderPaint(input,*after);},"overlapping source surfaces");
}
void countsUnpaintedDrawsAgainstSubdivisionBudget() {
    const auto before=model(false,"before"),after=model(true,"after");
    const auto input=painted(before,5,[](Vec3,unsigned){return MeshPaintRgb{100,0,0};});
    after->materialIds.push_back("unpainted-material");after->primitives.push_back(square(true));after->primitives[1].material=1;
    while(after->draws.size()<256)after->draws.push_back({1});
    rejects([&]{migrateQuaderPaint(input,*after);},"entity vertex or triangle budget");
    auto subdivisionOnly=input;subdivisionOnly.paint->draws.clear();
    rejects([&]{migrateQuaderPaint(subdivisionOnly,*after);},"entity vertex or triangle budget");
}
void sharpTriangleBoundaryRefinesDirectlyFromOriginal() {
    const auto before=model(false,"triangle-field"),after=model(true,"quad-field");
    // Two source triangles have a dark common diagonal and bright opposite
    // corners. A single bilinear quad incorrectly fills that diagonal by 50%.
    const auto input=painted(before,0,[](Vec3 p,unsigned){return MeshPaintRgb{static_cast<std::uint8_t>(p[0]!=p[1]?255:0),0,0};});
    const auto originalBytes=meshPaintBytes(*input.paint->draws.front().weights);const auto result=migrateQuaderPaint(input,*after);
    require(result.after&&result.after->subdivisionLevel>0&&result.after->subdivisionLevel<=MaximumMeshPaintLevel,
        "High-contrast cross-topology paint retained an inaccurate coarse destination grid.");
    require(result.maxError<.03f&&result.rmsError<.01f,"Finer direct transfer did not reduce the coarse grid's 50% boundary error.");
    const auto level=result.after->subdivisionLevel;const auto geometry=buildMeshPaintGeometry(after->primitives.front(),level);const auto& stored=result.after->draws.front();
    require(stored.level==level&&stored.sourceSignature==geometry->sourceSignature&&stored.weights->vertexCount==geometry->vertices.size(),"Adaptive paint level and corrected provenance disagree.");
    for(std::uint32_t i=0;i<geometry->vertices.size();++i)if(std::abs(geometry->vertices[i].position[0]-geometry->vertices[i].position[1])<1e-6)
        require(stored.weights->rgb(i)==MeshPaintRgb{},"Adaptive retry refined an already blurred mask instead of sampling the original dark diagonal.");
    require(result.before==input.paint&&meshPaintBytes(*input.paint->draws.front().weights)==originalBytes,"Adaptive retries mutated the retained original paint.");
}
void adaptiveRetryRetainsCompleteCandidateAtGeometryBudget() {
    const auto before=model(false,"triangle-field"),after=model(true,"quad-field");
    const auto input=painted(before,4,[](Vec3 p,unsigned){return MeshPaintRgb{static_cast<std::uint8_t>(p[0]!=p[1]?255:0),0,0};});
    after->materialIds.push_back("unpainted-material");after->primitives.push_back(square(true));after->primitives[1].material=1;
    while(after->draws.size()<257)after->draws.push_back({1});
    const auto result=migrateQuaderPaint(input,*after);
    require(result.after&&result.after->subdivisionLevel==4&&result.after->draws.size()==1&&result.after->draws.front().weights,
        "A finer-level budget limit discarded the already validated migration candidate.");
    require(result.maxError>.015f&&result.before==input.paint,"Budget fallback hid residual transfer error or lost the original payload.");
    require(result.after->draws.front().weights->vertexCount==buildMeshPaintGeometry(after->primitives[0],4)->vertices.size(),
        "Budget fallback published a partial finer-grid payload.");
}
}
int main() {
    try {trianglePatchesToQuadGrid();bilinearSamplingAcrossReindexing();unchangedPaintAndBaseRemainExact();reorderedDrawsKeepTheirMaterialAndPaint();rejectsInvalidOrChangedSources();rejectsOverlappingDifferentPaint();countsUnpaintedDrawsAgainstSubdivisionBudget();sharpTriangleBoundaryRefinesDirectlyFromOriginal();adaptiveRetryRetainsCompleteCandidateAtGeometryBudget();std::cout<<"Quader paint migration checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
