#pragma once

#include "decals/DecalSurface.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Portable, exact CPU inputs for investigating a project-specific decal without
// retaining a dependency on the project's model packages or editable scene.
namespace decal_probe {
using namespace crimson;
using namespace crimson::decals;
using Json=nlohmann::json;
inline Json idJson(const ReceiverId& id) { return {id.entity,id.draw,id.primitive}; }
inline ReceiverId receiverId(const Json& value) {
    return {value.at(0).get<uint64_t>(),value.at(1).get<uint32_t>(),value.at(2).get<uint32_t>()};
}
inline Vec3 vector(const Json& value) {
    if(value.size()!=3)throw std::runtime_error("Invalid snapshot vector.");
    return {value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
}
inline Mat4 matrix(const Json& value) {
    if(value.size()!=16)throw std::runtime_error("Invalid snapshot matrix.");
    Mat4 result;for(size_t i=0;i<16;++i)result[i]=value.at(i).get<float>();return result;
}
inline Json snapshotJson(const DecalSurfaceSnapshot& snapshot,const DecalBuildRequest& request) {
    Json output={{"version",1},{"frame",request.frame.values},
        {"seed",{{"receiver",idJson(request.seed.receiver)},{"triangle",request.seed.triangle},{"barycentrics",request.seed.barycentrics.values}}},
        {"limits",{{"maximumTriangles",request.limits.maximumTriangles},{"maximumInputTriangles",request.limits.maximumInputTriangles},
            {"maximumStretch",request.limits.maximumStretch},{"sampleSpacing",request.limits.sampleSpacing},{"seamTolerance",request.limits.seamTolerance}}},
        {"receivers",Json::array()}};
    for(const auto& receiver:snapshot.receivers) {
        Json item={{"id",idJson(receiver.id)},{"generation",receiver.generation},{"localToWorld",receiver.localToWorld.values},{"triangles",Json::array()}};
        for(const auto& triangle:receiver.triangles) {
            Json positions=Json::array(),normals=Json::array();
            for(size_t i=0;i<3;++i){positions.push_back(triangle.positions[i].values);normals.push_back(triangle.normals[i].values);}
            item["triangles"].push_back({{"triangle",triangle.triangle},{"positions",positions},{"normals",normals}});
        }
        if(receiver.boundary){item["boundary"]=Json::array();for(const auto& record:receiver.boundary->triangles){Json points=Json::array();for(const auto& point:record.points)points.push_back({{"first",point.first.values},{"last",point.last.values},{"parameter",point.parameter}});item["boundary"].push_back({{"triangle",record.triangle},{"corners",record.corners},{"points",points}});}}
        output["receivers"].push_back(std::move(item));
    }
    return output;
}
inline std::pair<DecalSurfaceSnapshot,DecalBuildRequest> snapshotInput(const Json& value) {
    if(value.at("version")!=1)throw std::runtime_error("Unsupported decal snapshot version.");
    DecalBuildRequest request;request.frame=matrix(value.at("frame"));
    const auto& seed=value.at("seed");request.seed={receiverId(seed.at("receiver")),seed.at("triangle").get<uint32_t>(),vector(seed.at("barycentrics"))};
    const auto& limits=value.at("limits");request.limits={limits.at("maximumTriangles").get<uint32_t>(),limits.at("maximumInputTriangles").get<uint32_t>(),
        limits.at("maximumStretch").get<double>(),limits.at("sampleSpacing").get<double>(),limits.at("seamTolerance").get<double>()};
    DecalSurfaceSnapshot snapshot;
    for(const auto& item:value.at("receivers")) {
        SurfaceReceiver receiver;receiver.id=receiverId(item.at("id"));receiver.generation=item.at("generation").get<uint64_t>();receiver.localToWorld=matrix(item.at("localToWorld"));
        for(const auto& input:item.at("triangles")) {
            SurfaceTriangle triangle;triangle.triangle=input.at("triangle").get<uint32_t>();
            for(size_t i=0;i<3;++i){triangle.positions[i]=vector(input.at("positions").at(i));triangle.normals[i]=vector(input.at("normals").at(i));}
            receiver.triangles.push_back(triangle);
        }
        if(item.contains("boundary")){auto boundary=std::make_shared<SurfaceBoundaryLineage>();for(const auto& input:item.at("boundary")){SurfaceBoundaryTriangle record;record.triangle=input.at("triangle").get<uint32_t>();record.corners=input.at("corners").get<uint8_t>();for(size_t i=0;i<3;++i){const auto& point=input.at("points").at(i);record.points[i]={vector(point.at("first")),vector(point.at("last")),point.at("parameter").get<double>()};}boundary->triangles.push_back(record);}receiver.boundary=std::move(boundary);}
        snapshot.receivers.push_back(std::move(receiver));
    }
    return {std::move(snapshot),request};
}
inline void writeSnapshot(const std::filesystem::path& file,const DecalSurfaceSnapshot& snapshot,const DecalBuildRequest& request,const Json& provenance={}) {
    auto value=snapshotJson(snapshot,request);value["provenance"]=provenance;
    std::ofstream output(file);if(!output)throw std::runtime_error("Cannot write decal snapshot.");output<<value.dump()<<'\n';output.close();
    // Validate the serialized float values and all original identities before
    // presenting this file as an exact reproduction rather than a proxy.
    std::ifstream stream(file);Json restored;stream>>restored;const auto [surface,build]=snapshotInput(restored);
    if(snapshotJson(surface,build)!=snapshotJson(snapshot,request))throw std::runtime_error("Decal snapshot round trip changed CPU input.");
    std::cout<<"export="<<file.string()<<" bytes="<<std::filesystem::file_size(file)<<" exactRoundTrip=1\n"<<std::flush;
}
inline void reportCoverage(const DecalSurfaceSnapshot& snapshot,const DecalBuildRequest& request,const DecalPatch& patch,bool expectFrontSide,bool expectFullImage=false) {
    double uvArea{},frontArea{},sideArea{},maximumError{};
    const auto front=math::normalized(math::transformDirection(request.frame,{0,0,1}));
    for(const auto& chunk:patch.chunks) {
        const auto receiver=std::find_if(snapshot.receivers.begin(),snapshot.receivers.end(),[&](const auto& value){return value.id==chunk.receiver;});
        if(receiver==snapshot.receivers.end()||receiver->generation!=chunk.generation)throw std::runtime_error("Patch changed receiver identity or generation.");
        for(const auto& vertex:chunk.vertices) {
            const auto source=std::find_if(receiver->triangles.begin(),receiver->triangles.end(),[&](const auto& value){return value.triangle==vertex.sourceTriangle;});
            if(source==receiver->triangles.end())throw std::runtime_error("Patch lost source triangle provenance.");
            Vec3 local;for(size_t i=0;i<3;++i)local=local+source->positions[i]*vertex.barycentrics[i];
            maximumError=std::max(maximumError,math::length(math::transformPoint(receiver->localToWorld,local)-vertex.position));
            if(vertex.uv[0]<-1e-5||vertex.uv[0]>1.00001||vertex.uv[1]<-1e-5||vertex.uv[1]>1.00001)throw std::runtime_error("Patch UV exceeds image coverage.");
        }
        for(size_t i=0;i<chunk.indices.size();i+=3) {
            const auto& a=chunk.vertices.at(chunk.indices[i]);const auto& b=chunk.vertices.at(chunk.indices[i+1]);const auto& c=chunk.vertices.at(chunk.indices[i+2]);
            uvArea+=.5*std::abs((double(b.uv[0])-a.uv[0])*(double(c.uv[1])-a.uv[1])-(double(b.uv[1])-a.uv[1])*(double(c.uv[0])-a.uv[0]));
            if(chunk.receiver==request.seed.receiver) {
                const auto cross=math::cross(b.position-a.position,c.position-a.position);const double area=.5*math::length(cross),dot=math::dot(math::normalized(cross,{0,0,0},0),front);
                if(dot>.9)frontArea+=area;if(std::abs(dot)<.1)sideArea+=area;
            }
        }
    }
    std::cout<<"coverage uvArea="<<uvArea<<" seedFrontWorldArea="<<frontArea<<" seedSideWorldArea="<<sideArea<<" maximumProvenanceError="<<maximumError<<'\n'<<std::flush;
    if(maximumError>2e-5)throw std::runtime_error("Patch moved away from its exact source surface.");
    if(expectFrontSide&&(!patch.usable()||frontArea<.05||sideArea<.05))throw std::runtime_error("Pillar decal did not cover both available front and side surfaces.");
    if(expectFullImage&&(!patch.usable()||std::abs(uvArea-1)>.005))throw std::runtime_error("Decal did not cover its complete image.");
}
}
