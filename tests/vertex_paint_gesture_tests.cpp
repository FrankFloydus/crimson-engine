#include "editor/VertexPaintGesture.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const MeshPaintGeometry> geometry() {
    auto mesh=std::make_shared<MeshPaintGeometry>();mesh->sourceSignature=123;mesh->signature=456;
    for(const auto point:std::array<Vec3,4>{{{-.5f,0,0},{0,0,0},{.5f,0,0},{0,0,.5f}}}){ModelVertex vertex;vertex.position=point;mesh->vertices.push_back(vertex);}
    mesh->indices={0,1,3,1,2,3};return mesh;
}
VertexPaintTarget target(EntityId entity=1) {
    VertexPaintTarget target;target.entity=entity;target.draw=2;target.primitive=7;target.sourcePrimitive=3;target.geometry=geometry();target.weights=makeMeshPaintPayload(4);return target;
}
VertexPaintTarget triangleTarget(std::uint32_t draw,bool opposite) {
    ModelPrimitive primitive;
    for(const auto point:std::array<Vec3,3>{{{-.5f,0,0},{.5f,0,0},{0,opposite?-.5f:.5f,0}}}){ModelVertex vertex;vertex.position=point;primitive.vertices.push_back(vertex);}
    primitive.indices=opposite?std::vector<std::uint32_t>{1,0,2}:std::vector<std::uint32_t>{0,1,2};
    auto mesh=target();mesh.draw=draw;mesh.primitive=draw;mesh.sourceNode=4;mesh.geometry=buildMeshPaintGeometry(primitive,0);return mesh;
}
void sourceMeshSeams() {
    const auto empty=[](const VertexPaintSeams& seams){return std::all_of(seams.begin(),seams.end(),[](const auto& target){return target.empty();});};
    auto first=triangleTarget(0,false),second=triangleTarget(1,true);
    auto seams=buildVertexPaintSeams(std::array{first,second});
    require(seams[0].size()==2&&seams[1].size()==2&&seams[0].at(0).front().target==1&&seams[0].at(0).front().vertex==0,"A source mesh material seam did not cross its separate draw calls.");
    second.sourceNode=5;require(empty(buildVertexPaintSeams(std::array{first,second})),"Coincident edges joined separate source mesh nodes.");second.sourceNode=4;
    second.entity=2;require(empty(buildVertexPaintSeams(std::array{first,second})),"Coincident edges joined separate entities.");second.entity=1;
    first.sourceNode=second.sourceNode=-1;require(empty(buildVertexPaintSeams(std::array{first,second})),"Unknown source occurrences joined separate draw calls.");first.sourceNode=second.sourceNode=4;
    auto sameDirection=triangleTarget(2,false);require(empty(buildVertexPaintSeams(std::array{first,sameDirection})),"Same-winding overlapping triangles proved a seam.");
    auto third=triangleTarget(2,true);require(empty(buildVertexPaintSeams(std::array{first,second,third})),"An ambiguous nonmanifold edge proved a seam.");
    auto isolated=triangleTarget(1,true);auto geometry=std::make_shared<MeshPaintGeometry>(*isolated.geometry);geometry->vertices[1].position={-1,0,0};isolated.geometry=geometry;
    require(empty(buildVertexPaintSeams(std::array{first,isolated})),"An isolated coincident vertex proved a seam.");
    auto shifted=second;auto local=std::make_shared<MeshPaintGeometry>(*second.geometry);for(auto& vertex:local->vertices)vertex.position=vertex.position-Vec3{1,0,0};shifted.geometry=local;shifted.world=math::translation({1,0,0});
    require(empty(buildVertexPaintSeams(std::array{first,shifted})),"Equal world edges joined incompatible source transforms.");
}
void brushVisibilityAndOrder() {
    auto mesh=target();const std::array targets{mesh};const std::array weights{mesh.weights};
    VertexPaintBrushCache cache;require(cache.prepare(targets,Mat4{},200,200),"Could not prepare immutable brush geometry.");
    const std::array dabs{VertexPaintDab{{0,0,0},{100,100},150},VertexPaintDab{{0,0,0},{100,100},150}};
    VertexPaintBrush brush;brush.radius=1;brush.strength=.5f;brush.hardness=1;brush.layer=1;
    std::uint32_t queries=0;
    auto result=cache.apply(dabs,brush,weights,[&](const auto& vertex)->std::optional<bool>{++queries;if(vertex.vertex==3)return std::nullopt;return vertex.vertex!=0;});
    require(result.missing.has_value()&&result.changes.empty(),"Missing exact visibility partially painted a batch.");
    result=cache.apply(dabs,brush,weights,[&](const auto& vertex)->std::optional<bool>{++queries;return vertex.vertex!=0;});
    require(!result.missing&&result.changes.size()==1&&result.changes.front().vertices.size()==3,"Exact visible vertex set changed.");
    require(queries==5,"Overlapping dabs did not retain per-vertex visibility memoization.");
    const auto twice=normalizedMeshPaintRgb({63.75f,191.25f,0,0});
    for(const auto& change:result.changes.front().vertices)require(change.vertex!=0&&change.rgb==twice,"Batched dabs changed order or painted an occluded vertex.");
    require(mesh.weights->rgb(1)==MeshPaintRgb{},"Brush publication mutated its immutable source.");
    const auto next=updateMeshPaintPayload(mesh.weights,result.changes.front().vertices);
    require(next->rgb(0)==MeshPaintRgb{}&&next->rgb(1)==twice,"Brush payload update changed hidden vertices.");
    VertexPaintBrushCache hidden;require(hidden.prepare(targets,Mat4{},200,200),"Could not prepare hidden-vertex fixture.");
    const auto occluded=hidden.apply(dabs,brush,weights,[](const auto&)->std::optional<bool>{return false;});
    require(!occluded.missing&&occluded.changes.empty(),"A fully occluded dab created paint data.");
    VertexPaintBrushCache zero;require(zero.prepare(targets,Mat4{},200,200),"Could not prepare zero-strength fixture.");brush.strength=0;
    const auto blank=zero.apply(dabs,brush,weights,[](const auto&)->std::optional<bool>{throw std::runtime_error("Zero-strength dab queried visibility.");});
    require(!blank.missing&&blank.changes.empty(),"A zero-strength dab created paint data.");
    VertexPaintBrushCache hover;require(hover.prepare(targets,Mat4{},200,200),"Could not prepare hover visibility.");brush.strength=.5f;std::size_t hoverQueries=0;
    const auto missing=hover.prepareVisibility(dabs.front(),brush,[&](const auto& vertex)->std::optional<bool>{++hoverQueries;if(vertex.vertex==3)return {};return true;});
    require(missing.has_value()&&weights.front()->rgb(1)==MeshPaintRgb{},"Hover visibility preparation changed paint or ignored an unknown brush edge.");
    require(!hover.prepareVisibility(dabs.front(),brush,[&](const auto&)->std::optional<bool>{++hoverQueries;return true;}),"Hover visibility did not become ready after its missing surface arrived.");
    require(hoverQueries==5,"Hover visibility repeated already validated vertices.");
    require(!hover.prepareVisibility(dabs.front(),brush,[](const auto&)->std::optional<bool>{throw std::runtime_error("Prepared hover visibility was queried again.");}),"A prepared hover footprint became unknown.");
}
void fractionalStrokeWeights() {
    auto mesh=target();const std::array targets{mesh};
    const auto visible=[](const auto&)->std::optional<bool>{return true;};
    const VertexPaintDab dab{{0,0,0},{100,100},150};
    std::array<VertexPaintDab,8> dabs;dabs.fill(dab);
    VertexPaintBrush brush;brush.radius=1;brush.strength=.0001f;brush.hardness=1;brush.layer=1;
    const auto publish=[&](VertexPaintBrushCache& cache,auto& weights,std::span<const VertexPaintDab> batch) {
        const auto result=cache.apply(batch,brush,weights,visible);require(!result.missing,"Exact visibility unexpectedly deferred a fractional stroke.");
        for(const auto& changes:result.changes)weights[changes.target]=updateMeshPaintPayload(weights[changes.target],changes.vertices);
    };
    VertexPaintBrushCache batched,singles;
    require(batched.prepare(targets,Mat4{},200,200)&&singles.prepare(targets,Mat4{},200,200),"Could not prepare fractional brush fixtures.");
    std::array batchedWeights{mesh.weights},singleWeights{mesh.weights};
    publish(batched,batchedWeights,dabs);
    require(batchedWeights.front()->rgb(1)==MeshPaintRgb{},"A sub-byte batch should not publish a premature weight.");
    for(unsigned i=1;i<8;++i)publish(batched,batchedWeights,dabs);
    for(unsigned i=0;i<64;++i)publish(singles,singleWeights,std::span{&dab,1});
    require(batchedWeights.front()->rgb(1)==MeshPaintRgb{2,0,0},"Unpublished fractional contributions were lost between batches.");
    require(sameMeshPaintPayload(batchedWeights.front(),singleWeights.front()),"Changing dab batch boundaries changed fractional stroke results.");

    // Base must remove the final RGB8 residue rather than stall at the point
    // where each individual dab rounds back to the same published byte.
    brush.strength=.01f;brush.layer=0;
    const auto painted=updateMeshPaintPayload(mesh.weights,std::array{MeshPaintChange{1,{100,70,50}}});
    std::array eraseWeights{painted};batched.resetStrokeWeights();
    for(unsigned i=0;i<128;++i)publish(batched,eraseWeights,dabs);
    require(eraseWeights.front()->rgb(1)==MeshPaintRgb{},"Repeated soft Base dabs retained a quantization plateau.");
    brush.layer=2;batched.resetStrokeWeights();eraseWeights.front()=painted;
    for(unsigned i=0;i<128;++i)publish(batched,eraseWeights,dabs);
    require(eraseWeights.front()->rgb(1)==MeshPaintRgb{0,255,0},"Soft replacement dabs failed to converge on their chosen layer.");

    // Starting another stroke reads the current immutable payload; fractional
    // residue from the previous stroke must not leak into a new transaction.
    brush.strength=.0001f;brush.layer=1;
    std::array resetWeights{mesh.weights};batched.resetStrokeWeights();
    for(unsigned i=0;i<2;++i)publish(batched,resetWeights,dabs);
    require(resetWeights.front()->rgb(1)==MeshPaintRgb{},"The reset fixture unexpectedly published its fractional baseline.");
    batched.resetStrokeWeights();
    for(unsigned i=0;i<2;++i)publish(batched,resetWeights,dabs);
    require(resetWeights.front()->rgb(1)==MeshPaintRgb{},"Fractional contributions escaped their stroke lifetime.");
    for(unsigned i=0;i<2;++i)publish(batched,resetWeights,dabs);
    require(resetWeights.front()->rgb(1)==MeshPaintRgb{1,0,0},"An explicit stroke reset discarded subsequent fractional contributions.");
    require(mesh.weights->rgb(1)==MeshPaintRgb{},"Fractional stroke accumulation mutated saved source paint.");
}
void baseCellMaskErasure() {
    auto mesh=target();auto geometry=std::make_shared<MeshPaintGeometry>();geometry->sourceSignature=123;geometry->signature=456;
    for(const auto point:std::array<Vec3,4>{{{-.8f,-.8f,0},{.8f,-.8f,0},{.8f,.8f,0},{-.8f,.8f,0}}}){ModelVertex vertex;vertex.position=point;geometry->vertices.push_back(vertex);}
    geometry->indices={0,1,2,0,2,3};MeshPaintCell cell;cell.cornerCount=4;cell.vertices={0,1,2,3};geometry->cells.push_back(cell);mesh.geometry=geometry;
    mesh.weights=updateMeshPaintPayload(mesh.weights,std::array{MeshPaintChange{0,{100,70,50}},MeshPaintChange{1,{50,100,70}},MeshPaintChange{2,{70,50,100}},MeshPaintChange{3,{80,80,80}}});
    const auto visible=[](const auto&)->std::optional<bool>{return true;};
    VertexPaintBrush brush;brush.radius=.025f;brush.strength=1;brush.hardness=1;brush.layer=0;
    const std::array offCenter{VertexPaintDab{{-.48f,-.16f,0},{104,232},8}};
    VertexPaintBrushCache cache;require(cache.prepare(std::array{mesh},Mat4{},400,400),"Could not prepare Base cell erasure.");
    auto result=cache.apply(offCenter,brush,std::array{mesh.weights},visible);
    require(!result.missing&&result.changes.size()==1&&result.changes.front().vertices.size()==4,"Full-strength Base missed controls contributing beneath a small off-center brush.");
    for(const auto& change:result.changes.front().vertices)require(change.rgb==MeshPaintRgb{},"Full-strength off-center Base left part of the sampled cell mask.");
    require(mesh.weights->rgb(2)==MeshPaintRgb{70,50,100},"Cell erasure modified immutable saved paint.");

    // The exact control under the cursor is already Base. A distant painted
    // corner still contributes within the circle, and must respond to Base
    // without moving the brush toward that remote control.
    mesh.weights=updateMeshPaintPayload(makeMeshPaintPayload(4),std::array{MeshPaintChange{2,{255,0,0}}});
    const std::array atBaseCorner{VertexPaintDab{{-.8f,-.8f,0},{40,360},8}};
    require(cache.prepare(std::array{mesh},Mat4{},400,400),"Could not prepare already-Base corner erasure.");
    result=cache.apply(atBaseCorner,brush,std::array{mesh.weights},visible);
    require(!result.missing&&result.changes.size()==1&&result.changes.front().vertices.size()==1&&result.changes.front().vertices.front().vertex==2&&result.changes.front().vertices.front().rgb==MeshPaintRgb{},"A brush over an already-Base corner failed to restore its neighboring visible mask.");
    require(cache.prepare(std::array{mesh},Mat4{},400,400),"Could not prepare soft already-Base corner erasure.");brush.strength=.5f;
    result=cache.apply(atBaseCorner,brush,std::array{mesh.weights},visible);
    require(result.changes.size()==1&&result.changes.front().vertices.size()==1&&result.changes.front().vertices.front().rgb==MeshPaintRgb{127,0,0},"Soft Base failed to reduce the sampled mask by its requested amount or applied duplicate footprint samples twice.");

    // A deferred later sample must leave the entire floating-point baseline
    // untouched, so retrying the batch is indistinguishable from one attempt.
    VertexPaintBrushCache fresh;require(fresh.prepare(std::array{mesh},Mat4{},400,400),"Could not prepare the deferred Base reference.");
    require(cache.prepare(std::array{mesh},Mat4{},400,400),"Could not prepare deferred Base visibility.");
    const std::array paired{atBaseCorner.front(),offCenter.front()};std::size_t queries{};
    const auto deferred=cache.apply(paired,brush,std::array{mesh.weights},[&](const auto& candidate)->std::optional<bool>{if(candidate.cell!=~0u&&++queries==3)return {};return true;});
    require(deferred.missing&&deferred.changes.empty(),"Unknown Base footprint visibility partially published a batch.");
    const auto retried=cache.apply(paired,brush,std::array{mesh.weights},visible),reference=fresh.apply(paired,brush,std::array{mesh.weights},visible);
    require(retried.changes.size()==1&&reference.changes.size()==1&&retried.changes.front().vertices.size()==reference.changes.front().vertices.size(),"Retried Base batch changed its publication set.");
    for(std::size_t i=0;i<retried.changes.front().vertices.size();++i) {
        const auto& a=retried.changes.front().vertices[i];const auto& b=reference.changes.front().vertices[i];
        require(a.vertex==b.vertex&&a.rgb==b.rgb,"Deferred Base visibility changed fractional stroke state before publication.");
    }
}
void worldRadiusAndFalloff() {
    auto mesh=target();mesh.world=math::scaling({4,1,1});VertexPaintBrushCache cache;
    const std::array targets{mesh};const std::array weights{mesh.weights};
    require(cache.prepare(targets,math::scaling({.25f,1,1}),200,200),"Affine paint preparation failed.");
    VertexPaintBrush brush;brush.radius=1;brush.strength=1;brush.hardness=0;brush.layer=2;
    const std::array dabs{VertexPaintDab{{0,0,0},{100,100},180}};
    auto result=cache.apply(dabs,brush,weights,[](const auto&)->std::optional<bool>{return true;});
    require(result.changes.size()==1&&result.changes[0].vertices.size()==2,"World-space radius followed local/nonuniform scale.");
    require(result.changes[0].vertices[0].vertex==1&&result.changes[0].vertices[0].rgb==MeshPaintRgb{0,255,0},"Full-strength center did not paint its selected layer.");
    require(result.changes[0].vertices[1].vertex==3&&result.changes[0].vertices[1].rgb[1]>=127&&result.changes[0].vertices[1].rgb[1]<=128,"Smooth radial falloff changed its midpoint.");
}
void worldBrushOutline() {
    const auto center=Vec3{3.25f,-.7f,22.75f},normal=math::normalized(Vec3{.3f,.8f,.5f});
    const auto circle=vertexPaintBrushCircle(center,normal,.5f);
    require(circle.front()==circle.back(),"The world brush outline did not close.");
    for(const auto point:circle) {
        require(std::abs(math::length(point-center)-.5)<.00001,"The displayed brush boundary changed its authored metre radius.");
        require(std::abs(math::dot(point-center,normal))<.00001,"The displayed brush boundary left the hit tangent plane.");
    }
    const auto wide=math::perspective(1.2,1,.1,1000,ClipDepth::NegativeOneToOne),zoom=math::perspective(.6,1,.1,1000,ClipDepth::NegativeOneToOne);
    const auto ring=vertexPaintBrushCircle({0,0,-4},{0,0,1},.5f);
    const auto radius=[&](const Mat4& viewProjection){float radius=0;for(const auto point:ring){const auto p=math::project(point,viewProjection,{0,0,800,800},ClipDepth::NegativeOneToOne);require(p.has_value(),"A visible outline point did not project.");radius=std::max(radius,float(std::hypot((*p)[0]-400,(*p)[1]-400)));}return radius;};
    require(radius(zoom)>radius(wide)*2,"The brush outline did not follow the viewport's zoom.");
}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-vertex-paint-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-vertex-paint-"))std::filesystem::remove_all(path,error);}
};
void transactionalGesture(const std::filesystem::path& root) {
    SceneDocument document(root);const auto entity=document.createBox("Paint target");document.select({entity},entity);
    const auto original=*document.scene().find(entity);const auto history=document.history().size();
    auto mesh=target(entity);mesh.world=math::translation({4,0,0}); // Authored draw-local transform is independent of entity world.
    VertexPaintGesture gesture;
    require(gesture.begin(document,{mesh})&&!document.transactionActive()&&gesture.finish(document),"A pending paint click unexpectedly began an authored transaction.");
    require(document.history().size()==history&&!document.scene().find(entity)->meshPaint,"An unchanged paint click created data or history.");
    require(gesture.begin(document,{mesh})&&gesture.publish(document,std::array{VertexPaintDabChanges{0,{{0,{0,0,0}}}}})&&!document.transactionActive()&&gesture.finish(document),"Painting unchanged Base opened a transaction.");
    require(document.history().size()==history&&!document.scene().find(entity)->meshPaint,"A Base dab on an empty mesh created data or history.");
    require(gesture.begin(document,{mesh}),"Could not arm paint stroke.");
    const std::array first{VertexPaintDabChanges{0,{{1,{255,0,0}}}}};
    const std::array second{VertexPaintDabChanges{0,{{2,{0,255,0}}}}};
    require(gesture.publish(document,first)&&gesture.publish(document,second)&&document.transactionActive(),"Paint dabs lost the document transaction.");
    const auto painted=document.scene().find(entity);require(painted&&painted->meshPaint&&painted->meshPaint->draws.size()==1,"Paint did not create one per-occurrence stream.");
    const auto& stream=painted->meshPaint->draws.front();require(stream.draw==2&&stream.primitive==3&&stream.weights->rgb(1)==MeshPaintRgb{255,0,0},"GPU primitive identity leaked into persisted source identity.");
    require(gesture.finish(document)&&document.history().size()==history+1&&document.undo(),"A stroke did not form exactly one undo command.");
    require(sameEntity(*document.scene().find(entity),original),"Undo failed to restore the exact empty component.");
    require(document.redo()&&document.scene().find(entity)->meshPaint->draws.front().weights->rgb(2)==MeshPaintRgb{0,255,0},"Redo did not restore final paint bytes.");
    const auto beforeCancel=*document.scene().find(entity);mesh.weights=beforeCancel.meshPaint->draws.front().weights;
    require(gesture.begin(document,{mesh})&&gesture.publish(document,std::array{VertexPaintDabChanges{0,{{1,{0,0,255}}}}}),"Could not start cancellation fixture.");
    gesture.cancel(document);require(!document.transactionActive()&&sameEntity(*document.scene().find(entity),beforeCancel),"Cancel did not restore exact paint pages.");
    require(gesture.begin(document,{mesh})&&gesture.publish(document,std::array{VertexPaintDabChanges{0,{{1,{0,0,255}}}}}),"Could not arm selection-change fixture.");
    document.select({});require(document.selection().ids==std::vector<EntityId>{entity}&&gesture.valid(document),"Document selection bypassed an active stroke transaction.");gesture.cancel(document);
    require(gesture.begin(document,{mesh}),"Could not arm pending context-change fixture.");
    document.select({});require(!gesture.valid(document),"Pending selection change retained painting ownership.");gesture.cancel(document);
    require(!document.transactionActive()&&sameEntity(*document.scene().find(entity),beforeCancel),"Context loss retained authored stroke changes.");
}
}
int main(){try{sourceMeshSeams();brushVisibilityAndOrder();fractionalStrokeWeights();baseCellMaskErasure();worldRadiusAndFalloff();worldBrushOutline();Project project;transactionalGesture(project.root);std::cout<<"Vertex paint source seams, visibility, fractional batched dabs, Base cell erasure, world brush and transactional history passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
