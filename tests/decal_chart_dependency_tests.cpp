#include <geometrycentral/surface/signpost_intrinsic_triangulation.h>
#include <geometrycentral/surface/vector_heat_method.h>
#include <iostream>
int main(){
    using namespace geometrycentral;using namespace geometrycentral::surface;
    for(double curvature:{0.,.1}){
        constexpr size_t n=21;std::vector<std::vector<size_t>> faces;
        for(size_t y=0;y+1<n;++y)for(size_t x=0;x+1<n;++x){size_t v=y*n+x;faces.push_back({v,v+1,v+n+1});faces.push_back({v,v+n+1,v+n});}
        ManifoldSurfaceMesh mesh(faces);VertexData<Vector3> p(mesh);for(size_t y=0;y<n;++y)for(size_t x=0;x<n;++x){double xx=(static_cast<double>(x)-10)*.1,yy=(static_cast<double>(y)-10)*.1;p[mesh.vertex(y*n+x)]={xx,yy,curvature*(xx*xx+yy*yy)};}
        VertexPositionGeometry geometry(mesh,p);SignpostIntrinsicTriangulation tri(mesh,geometry);tri.flipToDelaunay();VectorHeatMethodSolver solver(tri);auto map=solver.computeLogMap(tri.intrinsicMesh->vertex(10*n+10),LogMapStrategy::AffineLocal);size_t negative=0,centralNegative=0;
        for(Face f:tri.intrinsicMesh->faces()){auto h=f.halfedge();auto a=map[h.vertex()],b=map[h.next().vertex()],c=map[h.next().next().vertex()];double det=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);bool central=true;for(Vertex v:f.adjacentVertices()){auto q=p[mesh.vertex(v.getIndex())];central&=std::abs(q.x)<.6&&std::abs(q.y)<.6;}if(det<=0){++negative;if(central)++centralNegative;}}
        std::cout<<"Direct regular curvature "<<curvature<<": "<<centralNegative<<" nonpositive triangles in bounded central footprint ("<<negative<<" boundary-halo triangles excluded)\n";
        if(centralNegative)return 1;
    }return 0;
}
