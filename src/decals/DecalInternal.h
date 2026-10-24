#pragma once
#include "DecalSurface.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace crimson::decals::detail {
struct D3 { double x{},y{},z{};double operator[](size_t i)const{return i==0?x:i==1?y:z;} };
inline D3 operator+(D3 a,D3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline D3 operator-(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline D3 operator*(D3 a,double b){return {a.x*b,a.y*b,a.z*b};}
inline D3 operator/(D3 a,double b){return a*(1/b);}
inline double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double length(D3 a){return std::sqrt(dot(a,a));}
inline D3 unit(D3 a){const double l=length(a);return l>1e-20?a/l:D3{};}
inline D3 d3(Vec3 a){return {a[0],a[1],a[2]};}
inline Vec3 v3(D3 a){return {a.x,a.y,a.z};}
inline bool finite(D3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline D3 column(const Mat4&m,size_t c){return {m.at(0,c),m.at(1,c),m.at(2,c)};}
inline D3 point(const Mat4&m,D3 p){return column(m,0)*p.x+column(m,1)*p.y+column(m,2)*p.z+column(m,3);}
inline D3 normal(const Mat4&m,D3 n){const D3 a=column(m,0),b=column(m,1),c=column(m,2);return unit((cross(b,c)*n.x+cross(c,a)*n.y+cross(a,b)*n.z)/dot(a,cross(b,c)));}
struct Failure:std::runtime_error {DecalPatchStatus status;Failure(DecalPatchStatus s,const char*m):runtime_error(m),status(s){}};
inline void cancelled(std::stop_token stop){if(stop.stop_requested())throw Failure(DecalPatchStatus::Cancelled,"Surface preparation cancelled.");}
struct Source {size_t receiver{},triangle{};std::array<D3,3> p{},n{};D3 geometricNormal{};};
inline D3 barycentric(const Source&s,D3 p){const D3 a=s.p[1]-s.p[0],b=s.p[2]-s.p[0],q=p-s.p[0];const double aa=dot(a,a),ab=dot(a,b),bb=dot(b,b),qa=dot(q,a),qb=dot(q,b),det=aa*bb-ab*ab;const double y=(bb*qa-ab*qb)/det,z=(aa*qb-ab*qa)/det;return {1-y-z,y,z};}
struct AnalysisFace {std::array<size_t,3> v{};size_t source{};};
struct AnalysisPatch {
    std::vector<Source> sources;std::vector<D3> positions;std::vector<AnalysisFace> faces;
    size_t seedVertex{},seedFace{};D3 seedPosition{},basisX{},basisY{},seedNormal{};
    double ax{},ay{},bx{},by{},frameDet{},scale{},tolerance{};
};
AnalysisPatch buildTopology(const DecalSurfaceSnapshot&,const DecalBuildRequest&,DecalBuildStatistics&,std::stop_token);
void buildChart(const DecalSurfaceSnapshot&,const DecalBuildRequest&,const AnalysisPatch&,DecalPatch&,std::stop_token);
}
