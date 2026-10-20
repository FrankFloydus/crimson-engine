#include <crimson/renderer/forward/MeshNormals.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(Vec3 a,Vec3 b){require(std::abs(a[0]-b[0])<1e-5f&&std::abs(a[1]-b[1])<1e-5f&&std::abs(a[2]-b[2])<1e-5f,"Unexpected generated normal.");}
ModelPrimitive corner() {
    ModelPrimitive p;p.vertices.resize(6);p.indices={0,1,2,3,4,5};
    p.vertices[0].position={0,0,0};p.vertices[1].position={1,0,0};p.vertices[2].position={0,1,0};
    p.vertices[3].position={0,0,0};p.vertices[4].position={0,0,1};p.vertices[5].position={1,0,0};
    for(auto& vertex:p.vertices)vertex.normal={0,0,1};
    p.vertices[3].uv0={.2f,.3f};p.vertices[5].uv0={.4f,.6f};return p;
}
template<class F> void rejects(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid geometry was accepted.");}
}
int main(){try {
    auto p=corner();const auto before=p.vertices;const auto indices=p.indices;
    auto normals=generate_smooth_mesh_normals(p);const auto diagonal=std::sqrt(.5f);
    near(normals[0],{0,diagonal,diagonal});near(normals[3],normals[0]);near(normals[1],normals[5]);
    near(normals[2],{0,0,1});near(normals[4],{0,1,0});
    require(p.indices==indices&&std::memcmp(p.vertices.data(),before.data(),before.size()*sizeof(ModelVertex))==0,"Shading generation modified source geometry or normals.");
    for(std::size_t i=3;i<6;++i)p.vertices[i].normal={0,1,0};
    normals=generate_smooth_mesh_normals(p);near(normals[0],{0,0,1});near(normals[3],{0,1,0});
    p=corner();p.skinWeights.resize(6);for(auto& influence:p.skinWeights){influence.joints[0]=0;influence.weights[0]=1;}
    auto topology=make_mesh_normal_topology(p);require(topology.vertexGroups[0]==topology.vertexGroups[3],"Identical deformed UV duplicates did not share a chart.");
    p.skinWeights[3].joints[0]=1;topology=make_mesh_normal_topology(p);
    require(topology.vertexGroups[0]!=topology.vertexGroups[3],"Different skinning behaviors shared a smoothing chart.");
    p=corner();p.morphTargets.resize(1);p.morphTargets[0].positions.resize(6);p.morphTargets[0].positions[3]={0,0,1};
    topology=make_mesh_normal_topology(p);require(topology.vertexGroups[0]!=topology.vertexGroups[3],"Different morph behaviors shared a smoothing chart.");
    p=corner();p.vertices.resize(3);p.indices={0,0,0};p.vertices[0].normal={2,0,0};
    normals=generate_smooth_mesh_normals(p);near(normals[0],{1,0,0});
    p=corner();p.indices[1]=99;rejects([&]{generate_smooth_mesh_normals(p);});
    p=corner();p.indices.pop_back();rejects([&]{generate_smooth_mesh_normals(p);});
    p=corner();p.vertices[0].position[0]=std::numeric_limits<float>::infinity();rejects([&]{generate_smooth_mesh_normals(p);});
    // A large family of coincident UV islands shares incidence by chart. Its
    // storage remains linear in geometry, instead of copying each island's list.
    p={};for(std::uint32_t island=0;island<1000;++island){const auto first=static_cast<std::uint32_t>(p.vertices.size());
        for(const auto position:{Vec3{0,0,0},Vec3{1,0,0},Vec3{0,1,0}}){ModelVertex v;v.position=position;v.normal={0,0,1};v.uv0={float(island),0};p.vertices.push_back(v);}
        p.indices.insert(p.indices.end(),{first,first+1,first+2});}
    topology=make_mesh_normal_topology(p);require(topology.groupOffsets.size()==4&&topology.groupFaces.size()==p.indices.size()&&topology.vertexGroups.size()==p.vertices.size(),"Coincident chart incidence expanded beyond linear storage.");
    std::cout<<"Mesh normal chart, hard-edge, deformation identity and immutability checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
