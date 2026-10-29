#pragma once
#include <geometrycentral/surface/vertex_position_geometry.h>
#include <cmath>

namespace crimson::decals::detail {
// Retain the embedded metric of positive, possibly very thin contact cells.
// Length-only Heron/law-of-cosines formulas can lose their altitude; uniform
// edge inflation can instead introduce cone curvature into a flat vertex fan.
// These quantities use edge differences in the original embedding throughout.
class DecalGeodesicGeometry final:public geometrycentral::surface::VertexPositionGeometry {
    using Vector=geometrycentral::Vector3;
    static Vector crossAccurate(Vector a,Vector b) {
        const auto difference=[](double a,double b,double c,double d){
            const double product=c*d;
            return std::fma(a,b,-product)-std::fma(c,d,-product);
        };
        return {difference(a.y,b.z,a.z,b.y),difference(a.z,b.x,a.x,b.z),difference(a.x,b.y,a.y,b.x)};
    }
public:
    using VertexPositionGeometry::VertexPositionGeometry;
protected:
    void computeFaceAreas() override {
        namespace gs=geometrycentral::surface;
        faceAreas=gs::FaceData<double>(mesh);
        for(gs::Face face:mesh.faces()) {
            const auto edge=face.halfedge();const auto p=vertexPositions[edge.vertex()];
            faceAreas[face]=.5*norm(crossAccurate(vertexPositions[edge.next().vertex()]-p,vertexPositions[edge.next().next().vertex()]-p));
        }
    }
    void computeCornerAngles() override {
        namespace gs=geometrycentral::surface;
        cornerAngles=gs::CornerData<double>(mesh);
        for(gs::Corner corner:mesh.corners()) {
            const auto edge=corner.halfedge();const auto p=vertexPositions[edge.vertex()];
            const auto a=vertexPositions[edge.next().vertex()]-p,b=vertexPositions[edge.next().next().vertex()]-p;
            cornerAngles[corner]=std::atan2(norm(crossAccurate(a,b)),dot(a,b));
        }
    }
    void computeHalfedgeVectorsInFace() override {
        namespace gs=geometrycentral::surface;
        halfedgeVectorsInFace=gs::HalfedgeData<geometrycentral::Vector2>(mesh);
        for(gs::Face face:mesh.faces()) {
            const auto edge=face.halfedge();const auto p=vertexPositions[edge.vertex()];
            const auto a=vertexPositions[edge.next().vertex()]-p,b=vertexPositions[edge.next().next().vertex()]-p;
            const double length=norm(a),x=dot(a,b)/length,y=norm(crossAccurate(a,b))/length;
            halfedgeVectorsInFace[edge]={length,0};
            halfedgeVectorsInFace[edge.next()]={x-length,y};
            halfedgeVectorsInFace[edge.next().next()]={-x,-y};
        }
    }
};
}
