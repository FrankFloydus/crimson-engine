#include <crimson/renderer/forward/MeshPaintUpload.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace crimson;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
ModelPrimitive quads(std::uint32_t count=1) {
    ModelPrimitive source;
    for(std::uint32_t face=0;face<count;++face) {
        const auto base=std::uint32_t(source.vertices.size()),triangle=std::uint32_t(source.indices.size()/3);
        const auto x=float(face)*2;
        for(const Vec3 position:{Vec3{x,0,0},Vec3{x+1,0,0},Vec3{x+1,1,0},Vec3{x,1,0}}){ModelVertex vertex;vertex.position=position;source.vertices.push_back(vertex);}
        source.indices.insert(source.indices.end(),{base,base+1,base+2,base,base+2,base+3});
        source.sourceFaces.push_back({{base,base+1,base+2,base+3},{triangle,triangle+1},4});
    }
    return source;
}
ModelPrimitive shadingQuad(bool alternate) {
    auto source=quads();if(alternate)source.indices={0,1,3,1,2,3};
    for(auto& vertex:source.vertices){vertex.normal={0,0,1};vertex.tangent={1,0,0,1};}
    for(unsigned corner=3;corner<6;++corner) {
        const auto original=source.indices[corner];auto alias=source.vertices[original];alias.normal={0,1,0};alias.tangent={0,0,1,-1};
        if(std::find(source.indices.begin(),source.indices.begin()+3,original)!=source.indices.begin()+3) {
            source.indices[corner]=std::uint32_t(source.vertices.size());source.vertices.push_back(alias);
        } else source.vertices[original]=alias;
    }
    return source;
}
std::uint32_t incidentCells(const MeshPaintGeometry& geometry,std::span<const MeshPaintChange> changes) {
    std::uint32_t count{};
    for(const auto& cell:geometry.cells) {
        const auto end=cell.vertices.begin()+cell.cornerCount;
        if(std::any_of(changes.begin(),changes.end(),[&](const auto& change){return std::find(cell.vertices.begin(),end,change.vertex)!=end;}))++count;
    }
    return count;
}
struct PageBaseline {std::uint32_t cells{},ranges{};};
PageBaseline pageBaseline(const MeshPaintGeometry& geometry,std::span<const MeshPaintChange> changes) {
    PageBaseline result;bool previous=false;
    for(const auto& cell:geometry.cells) {
        bool changed=false;
        for(unsigned corner=0;corner<cell.cornerCount&&!changed;++corner)
            for(const auto& value:changes)if(cell.vertices[corner]/MeshPaintPageVertices==value.vertex/MeshPaintPageVertices){changed=true;break;}
        if(changed){++result.cells;if(!previous)++result.ranges;}previous=changed;
    }
    return result;
}
void applyRanges(std::vector<MeshPaintGpuVertex>& simulated,const MeshPaintUpload& upload) {
    const auto values=upload.values();if(simulated.empty())simulated.resize(values.size());
    require(simulated.size()==values.size(),"A paint update changed the render layout without a topology replacement.");
    require(upload.ranges().size()<=MaximumMeshPaintUploadRanges,"A paint edit exceeded the bounded GPU update count.");
    std::uint64_t previousEnd{};
    for(const auto range:upload.ranges()) {
        require(range.count&&range.first>=previousEnd&&std::uint64_t(range.first)+range.count<=values.size(),"A paint upload range is empty, overlapping, unordered or out of bounds.");
        std::copy_n(values.begin()+range.first,range.count,simulated.begin()+range.first);
        previousEnd=std::uint64_t(range.first)+range.count;
    }
}
void matchesFresh(const MeshPaintGeometry& geometry,const MeshPaintPayload& payload,const std::vector<MeshPaintGpuVertex>& simulated) {
    MeshPaintUpload fresh;require(fresh.prepare(geometry,payload),"A fresh paint upload did not prepare its layout.");
    require(fresh.values().size()==simulated.size(),"Fresh and incremental paint streams have different layouts.");
    require(simulated.empty()||std::memcmp(simulated.data(),fresh.values().data(),simulated.size()*sizeof(MeshPaintGpuVertex))==0,"Applying only the advertised upload ranges left stale or incorrect GPU paint values.");
}
void incrementalUndoRedoAndSemanticEquality() {
    const auto geometry=buildMeshPaintGeometry(quads(),3);
    const auto base=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()));
    MeshPaintUpload upload;require(upload.prepare(*geometry,*base),"Initial paint upload did not rebuild its layout.");
    require(upload.ranges().size()==1&&upload.ranges().front().first==0&&upload.ranges().front().count==upload.values().size(),"The first paint upload did not cover the complete stream.");
    std::vector<MeshPaintGpuVertex> simulated;applyRanges(simulated,upload);matchesFresh(*geometry,*base,simulated);
    const std::array firstChange{MeshPaintChange{0,{100,60,20}}};
    const auto first=updateMeshPaintPayload(base,firstChange);
    require(!upload.prepare(*geometry,*first),"A COW paint edit rebuilt immutable geometry.");
    require(upload.changed_vertices()==1&&upload.changed_cells()==incidentCells(*geometry,firstChange),"A single-control edit dirtied unrelated paint controls or cells.");
    applyRanges(simulated,upload);matchesFresh(*geometry,*first,simulated);
    const std::array secondChange{MeshPaintChange{std::uint32_t(geometry->vertices.size()-1),{5,30,200}}};
    const auto second=updateMeshPaintPayload(first,secondChange);
    require(!upload.prepare(*geometry,*second),"A second COW paint edit rebuilt geometry.");
    applyRanges(simulated,upload);matchesFresh(*geometry,*second,simulated);
    const auto equal=makeMeshPaintPayload(second->vertexCount,meshPaintBytes(*second));
    require(equal->pages.front()!=second->pages.front(),"The semantic-equality fixture reused its page pointer.");
    require(!upload.prepare(*geometry,*equal)&&upload.ranges().empty()&&!upload.changed_vertices()&&!upload.changed_cells(),"Equal RGB data in a replacement payload uploaded unchanged paint.");
    applyRanges(simulated,upload);matchesFresh(*geometry,*equal,simulated);
    for(const auto& payload:{first,second,base,second,base}) {
        require(!upload.prepare(*geometry,*payload),"Undo/redo or Base clearing rebuilt the paint layout.");
        applyRanges(simulated,upload);matchesFresh(*geometry,*payload,simulated);
    }
    require(!base->pages.front(),"Clearing all paint changed the immutable null-page baseline.");
    const auto cleared=updateMeshPaintPayload(first,std::array{MeshPaintChange{0,{}}});
    require(!cleared->pages.front(),"An all-Base COW page was not released to a null page.");
    require(!upload.prepare(*geometry,*first),"Restoring paint rebuilt the layout.");applyRanges(simulated,upload);
    require(!upload.prepare(*geometry,*cleared),"A null-page clear rebuilt the layout.");applyRanges(simulated,upload);matchesFresh(*geometry,*cleared,simulated);
    require(!upload.prepare(*geometry,*cleared)&&upload.ranges().empty(),"An unchanged payload scheduled another GPU upload.");
}
void unusedControlsProduceNoUpload() {
    auto source=quads();ModelVertex unused;unused.position={9,9,9};source.vertices.push_back(unused);
    const auto geometry=buildMeshPaintGeometry(source,2);const std::uint32_t unusedControl=4;
    for(const auto& cell:geometry->cells)require(std::find(cell.vertices.begin(),cell.vertices.begin()+cell.cornerCount,unusedControl)==cell.vertices.begin()+cell.cornerCount,"The unused-control fixture accidentally references its spare source vertex.");
    const auto base=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()));
    MeshPaintUpload upload;upload.prepare(*geometry,*base);std::vector<MeshPaintGpuVertex> simulated;applyRanges(simulated,upload);
    const auto edited=updateMeshPaintPayload(base,std::array{MeshPaintChange{unusedControl,{10,20,30}}});
    require(!upload.prepare(*geometry,*edited)&&upload.ranges().empty()&&!upload.changed_cells(),"An unused source control scheduled a GPU update.");
    matchesFresh(*geometry,*edited,simulated);
}
void shadingLayoutsUseLogicalControls() {
    for(const bool alternate:{false,true}) {
        const auto geometry=buildMeshPaintGeometry(shadingQuad(alternate),2);
        require(std::any_of(geometry->cells.begin(),geometry->cells.end(),[](const auto& cell){return meshPaintCellVertexCount(cell)==4;})&&
            std::any_of(geometry->cells.begin(),geometry->cells.end(),[](const auto& cell){return meshPaintCellVertexCount(cell)==6;}),"The shading-seam fixture did not include both four- and six-vertex cells.");
        std::vector<std::uint8_t> bytes;bytes.reserve(geometry->vertices.size()*3);
        for(std::size_t vertex=0;vertex<geometry->vertices.size();++vertex){bytes.push_back(std::uint8_t(vertex%91));bytes.push_back(std::uint8_t(vertex*7%81));bytes.push_back(std::uint8_t(vertex*3%61));}
        const auto base=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()),bytes);
        MeshPaintUpload upload;require(upload.prepare(*geometry,*base),"A shading-seam layout was not prepared.");
        std::vector<MeshPaintGpuVertex> simulated;applyRanges(simulated,upload);std::size_t offset{};
        for(const auto& cell:geometry->cells)for(std::uint32_t renderCorner=0;renderCorner<meshPaintCellVertexCount(cell);++renderCorner) {
            const auto logical=meshPaintCellLogicalCorner(cell,renderCorner);const auto rgb=base->rgb(cell.vertices[logical]);
            for(unsigned channel=0;channel<3;++channel)require(std::abs(upload.values()[offset].rgb[channel]-rgb[channel]/255.f)<1e-7f,"A split shading corner read another logical paint control.");
            ++offset;
        }
        require(offset==upload.values().size(),"Split paint cells disagree with the expanded GPU stream size.");
        const auto edited=updateMeshPaintPayload(base,std::array{MeshPaintChange{0,{200,20,5}}});
        require(!upload.prepare(*geometry,*edited),"Editing a split quad rebuilt its GPU layout.");applyRanges(simulated,upload);matchesFresh(*geometry,*edited,simulated);
        require(!upload.prepare(*geometry,*base),"Undo on a split quad rebuilt its GPU layout.");applyRanges(simulated,upload);matchesFresh(*geometry,*base,simulated);
    }
}
void denseGeometryAndRangeBudget(bool benchmark) {
    const auto geometry=buildMeshPaintGeometry(quads(28),5);
    require(geometry->vertices.size()>30000&&geometry->cells.size()==28*1024,"The dense fixture does not represent a Level 5 room.");
    const auto base=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()));
    const std::array oneChange{MeshPaintChange{0,{100,60,20}}};const auto edited=updateMeshPaintPayload(base,oneChange);
    const auto baseline=pageBaseline(*geometry,oneChange);const auto actual=incidentCells(*geometry,oneChange);
    require(baseline.ranges>MaximumMeshPaintUploadRanges&&baseline.cells>actual*100,"The dense fixture did not expose fragmented page-wide GPU updates.");
    MeshPaintUpload upload;upload.prepare(*geometry,*base);std::vector<MeshPaintGpuVertex> simulated;applyRanges(simulated,upload);
    require(!upload.prepare(*geometry,*edited),"A dense-mesh paint edit rebuilt geometry.");
    require(upload.changed_vertices()==1&&upload.changed_cells()==actual,"A dense-mesh edit dirtied the changed page instead of actual incident cells.");
    applyRanges(simulated,upload);matchesFresh(*geometry,*edited,simulated);
    std::vector<MeshPaintChange> separated;
    for(const auto face:{0u,3u,9u,15u,21u,27u})separated.push_back({face*4,{20,100,40}});
    const auto scattered=updateMeshPaintPayload(edited,separated);
    require(!upload.prepare(*geometry,*scattered),"Scattered controls rebuilt dense geometry.");
    require(upload.changed_vertices()==separated.size()&&upload.changed_cells()==incidentCells(*geometry,separated),"Scattered controls dirtied unrelated logical cells.");
    applyRanges(simulated,upload);matchesFresh(*geometry,*scattered,simulated);
    require(!upload.prepare(*geometry,*base),"Dense undo rebuilt the layout.");applyRanges(simulated,upload);matchesFresh(*geometry,*base,simulated);
    if(benchmark) {
        constexpr unsigned iterations=400;const auto started=std::chrono::steady_clock::now();std::uint64_t ranges{};
        for(unsigned iteration=0;iteration<iterations;++iteration){upload.prepare(*geometry,*(iteration%2?base:edited));ranges+=upload.ranges().size();}
        const auto microseconds=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count()/iterations;
        std::cout<<"Level 5 paint upload CPU preparation: "<<microseconds<<" us/edit; "<<geometry->vertices.size()<<" logical controls, "<<geometry->cells.size()<<" cells. Page-wide baseline: "<<baseline.cells<<" cells / "<<baseline.ranges<<" upload ranges; actual edit: "<<actual<<" cells / "<<double(ranges)/iterations<<" bounded ranges. This measures CPU preparation and range count, not GPU or editor frame time.\n";
    }
}
}
int main(int argc,char** argv) {
    try {
        const bool benchmark=argc>1&&std::string_view(argv[1])=="--benchmark";
        incrementalUndoRedoAndSemanticEquality();unusedControlsProduceNoUpload();shadingLayoutsUseLogicalControls();denseGeometryAndRangeBudget(benchmark);
        std::cout<<"Mesh paint uploads preserve full GPU values through COW edits, semantic equality, null pages, undo/redo, sparse shading seams and bounded dense-mesh updates.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
