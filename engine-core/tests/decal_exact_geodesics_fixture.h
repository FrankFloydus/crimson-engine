#pragma once
#include "decals/DecalExactGeodesics.h"
#include "decals/DecalGeodesicGeometry.h"
#include <geometrycentral/surface/manifold_surface_mesh.h>
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <cmath>
#include <string>

namespace crimson::decals::test {
inline void runExactGeodesicProofs(){
    namespace gc=geometrycentral;namespace gs=geometrycentral::surface;namespace exact=detail::exact;
    const auto require=[](bool value,const char*message){if(!value)throw std::runtime_error(message);};
    const auto rejected=[&](auto&&operation,DecalPatchStatus expected){bool failed=false;try{operation();}catch(const detail::Failure&failure){failed=true;require(failure.status==expected,"Exact geodesics reported the wrong failure status.");}require(failed,"Exact geodesics ignored a cancellation or resource limit.");};
    // A source just above a long shared edge has positive embedded altitude,
    // although its rounded lengths satisfy equality. Initialize the opposite
    // interval from that actual face frame in both canonical edge directions.
    for(bool reversed:{false,true}){
        const std::vector<std::vector<size_t>> skinnyFaces=reversed?
            std::vector<std::vector<size_t>>{{1,0,3},{0,1,2}}:
            std::vector<std::vector<size_t>>{{0,1,2},{1,0,3}};
        gs::ManifoldSurfaceMesh skinnyMesh(skinnyFaces);gs::VertexData<gc::Vector3> skinnyPositions(skinnyMesh);
        constexpr double altitude=1e-9;
        skinnyPositions[skinnyMesh.vertex(0)]={0,0,0};skinnyPositions[skinnyMesh.vertex(1)]={2,0,0};
        skinnyPositions[skinnyMesh.vertex(2)]={.25,altitude,0};skinnyPositions[skinnyMesh.vertex(3)]={.25,-1,0};
        detail::DecalGeodesicGeometry skinnyGeometry(skinnyMesh,skinnyPositions);
        skinnyGeometry.requireEdgeLengths();skinnyGeometry.requireHalfedgeVectorsInFace();
        gs::Edge shared;for(gs::Edge edge:skinnyMesh.edges())if(!edge.isBoundary())shared=edge;
        require(shared!=gs::Edge(),"The skinny source fixture has no shared edge.");
        const auto skinnySeed=skinnyMesh.vertex(2);gs::SurfacePoint sourcePoint(skinnySeed);gs::IntervalWithStop rounded,stable;
        rounded.initialize(skinnyGeometry,shared,skinnyGeometry.edgeLengths[shared],&sourcePoint,0);
        require(rounded.pseudo_y()==0,"The skinny source fixture no longer exercises collapsed length-only initialization.");
        exact::initializeVertexSourceInterval(skinnyGeometry,shared,skinnySeed,stable);
        const double expectedX=shared.halfedge().vertex()==skinnyMesh.vertex(0)?.25:1.75;
        require(std::abs(stable.pseudo_x()-expectedX)<1e-15&&std::abs(stable.pseudo_y()+altitude)<1e-22&&std::abs(stable.min()-altitude)<1e-22,
            "Exact geodesic source initialization lost positive embedded altitude or canonical edge orientation.");
        gs::Edge side;for(gs::Edge edge:skinnyMesh.edges()){const auto h=edge.halfedge();if((h.vertex()==skinnyMesh.vertex(0)&&h.tipVertex()==skinnySeed)||(h.tipVertex()==skinnyMesh.vertex(0)&&h.vertex()==skinnySeed))side=edge;}
        require(side!=gs::Edge(),"The skinny traceback fixture has no source side.");
        const double parameter=side.halfedge().vertex()==skinnyMesh.vertex(0)?1e-6:1-1e-6;
        const double fraction=side.halfedge().vertex()==skinnyMesh.vertex(0)?parameter:1-parameter;
        const gs::SurfacePoint nearCorner(side,parameter);
        const auto coordinates=exact::embeddedEdgeCoordinates(skinnyGeometry,shared,nearCorner);
        const double expectedAlong=shared.halfedge().vertex()==skinnyMesh.vertex(0)?.25*fraction:2-.25*fraction;
        require(std::abs(coordinates.first-expectedAlong)<1e-15&&std::abs(coordinates.second-altitude*fraction)<1e-28,
            "Traceback edge coordinates lost a positive near-corner altitude through squared-length cancellation.");
        const auto lengthOnly=gs::exactgeodesic::compute_local_coordinates(skinnyGeometry,shared,nearCorner);
        require(std::abs(lengthOnly.second-altitude*fraction)>1e-17,
            "The skinny edge traceback fixture no longer exercises the unstable upstream coordinate reconstruction.");
        exact::BoundedGeo skinnySolver(skinnyMesh,skinnyGeometry,{1000,100000,1000});skinnySolver.propagateBounded(skinnySeed,4);
        const auto across=skinnySolver.distanceBounded(skinnyMesh.vertex(3));
        require(across&&std::abs(*across-(1+altitude))<1e-12,"A skinny source changed the metre distance across its shared edge.");
        const auto map=skinnySolver.logBounded(skinnyMesh.vertex(3));
        require(map&&map->isFinite()&&std::abs(map->norm()-(1+altitude))<1e-12,"A skinny source lost its finite logarithmic map across the shared edge.");
        skinnyGeometry.unrequireHalfedgeVectorsInFace();skinnyGeometry.unrequireEdgeLengths();
    }
    constexpr size_t side=11;std::vector<std::vector<size_t>> faces;
    for(size_t y=0;y+1<side;++y)for(size_t x=0;x+1<side;++x){const size_t v=y*side+x;faces.push_back({v,v+1,v+side+1});faces.push_back({v,v+side+1,v+side});}
    gs::ManifoldSurfaceMesh mesh(faces);gs::VertexData<gc::Vector3> positions(mesh);
    for(gs::Vertex vertex:mesh.vertices()){const size_t i=vertex.getIndex();positions[vertex]={double(i%side)*.1,double(i/side)*.1,0};}
    gs::VertexPositionGeometry geometry(mesh,positions);const auto seed=mesh.vertex(5*side+5);
    constexpr exact::Budget sufficient{10000,10000000,10000};
    exact::BoundedGeo bounded(mesh,geometry,sufficient);bounded.propagateBounded(seed,10);
    gs::GeodesicAlgorithmExact upstream(mesh,geometry);upstream.propagate(seed);
    for(gs::Vertex vertex:mesh.vertices()){
        const auto map=bounded.logBounded(vertex);const auto distance=bounded.distanceBounded(vertex);
        require(map.has_value()&&distance.has_value(),"Exact geodesics lost a reached plane vertex.");
        require((*map-upstream.getLog(vertex)).norm()<1e-10,"Bounded exact geodesics changed the upstream logarithmic map.");
        require(std::abs(*distance-(positions[vertex]-positions[seed]).norm())<1e-10,"Exact geodesics changed the plane metre metric.");
    }
    require(bounded.allocatedIntervals()<=sufficient.maximumIntervals&&bounded.work()<=sufficient.maximumWork,"Exact geodesics exceeded its arena/work contract.");
    exact::BoundedGeo radial(mesh,geometry,sufficient);radial.propagateBounded(seed,.15);
    require(radial.logBounded(mesh.vertex(5*side+6)).has_value(),"Finite-radius propagation lost a nearer vertex.");
    require(!radial.distanceBounded(mesh.vertex(0))&&!radial.logBounded(mesh.vertex(0)),"Finite-radius propagation fabricated a map for an unreached vertex.");
    std::stop_source stopped;stopped.request_stop();
    rejected([&]{exact::BoundedGeo cancelled(mesh,geometry,sufficient,stopped.get_token());},DecalPatchStatus::Cancelled);
    std::stop_source live;exact::BoundedGeo cancellable(mesh,geometry,sufficient,live.get_token());cancellable.propagateBounded(seed,10);live.request_stop();
    rejected([&]{cancellable.logBounded(seed);},DecalPatchStatus::Cancelled);
    rejected([&]{exact::BoundedGeo limited(mesh,geometry,{1,10000000,10000});limited.propagateBounded(seed,10);},DecalPatchStatus::LimitExceeded);
    rejected([&]{exact::BoundedGeo limited(mesh,geometry,{10000,1,10000});limited.propagateBounded(seed,10);},DecalPatchStatus::LimitExceeded);
    rejected([&]{exact::BoundedGeo limited(mesh,geometry,{10000,10000000,1});limited.propagateBounded(seed,10);limited.logBounded(mesh.vertex(0));},DecalPatchStatus::LimitExceeded);

}
} // namespace crimson::decals::test
