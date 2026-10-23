#include "crimson-renderer/DirectionalShadowMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
using Point=std::array<double,3>;
using Pixel=std::array<double,2>;
using Clip=std::array<double,4>;
using DoubleMatrix=std::array<Clip,4>;
std::string context;

void check(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message+(context.empty()?"":" ["+context+"]")); }
bool close(double a,double b,double absolute=2e-8,double relative=3e-5) {
    return std::abs(a-b)<=absolute+relative*std::max(std::abs(a),std::abs(b));
}
Point point(const Vec3& p) { return {p[0],p[1],p[2]}; }
Point add(Point a,Point b) { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
Point subtract(Point a,Point b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Point scale(Point a,double s) { return {a[0]*s,a[1]*s,a[2]*s}; }
double dot(Point a,Point b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Point cross(Point a,Point b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double length(Point a) { return std::sqrt(dot(a,a)); }

// Model the portable shader expression mul(rowVector, projection). HLSL and
// GLSL agree on this algebra even though matrix indexing conventions differ.
Point rowMultiply(const Clip& row,const Mat4& matrix) {
    Point result{};
    for(std::size_t column=0;column<3;++column)
        for(std::size_t axis=0;axis<4;++axis)result[column]+=row[axis]*matrix.at(axis,column);
    return result;
}
struct Basis {Point u,v,z;};
Basis portableRows(const Mat4& matrix) {
    return {rowMultiply({.5,0,0,0},matrix),rowMultiply({0,-.5,0,0},matrix),rowMultiply({0,0,1,0},matrix)};
}
Basis oldHlslColumns(const Mat4& matrix) {
    return {{matrix.at(0,0)*.5,matrix.at(1,0)*.5,matrix.at(2,0)*.5},
        {matrix.at(0,1)*-.5,matrix.at(1,1)*-.5,matrix.at(2,1)*-.5},
        {matrix.at(0,2),matrix.at(1,2),matrix.at(2,2)}};
}
std::array<double,2> gradient(const Basis& basis,Point normal,unsigned resolution) {
    const auto tangent=cross(basis.v,normal);
    const double determinant=dot(basis.u,tangent);
    if(std::abs(determinant)<=1e-12)return {};
    return {dot(basis.z,tangent)/(determinant*resolution),dot(basis.z,cross(normal,basis.u))/(determinant*resolution)};
}
double texelWorld(const Basis& basis,unsigned resolution) { return 1/(length(basis.u)*resolution); }

// Independent oracle: unproject two clip depths, intersect that world-space
// line with the receiver plane, then project the intersection. It does not use
// the receiver-gradient cross products or invert the U/V plane basis.
Clip transform(const Mat4& matrix,const Clip& input) {
    Clip result{};
    for(std::size_t row=0;row<4;++row)for(std::size_t column=0;column<4;++column)
        result[row]+=matrix.at(row,column)*input[column];
    return result;
}
DoubleMatrix inverseProjection(const Mat4& matrix) {
    // Invert the actual float coefficients in double precision. The engine's
    // Mat4 inverse stores floats; on a grazing receiver, its axis error can be
    // magnified by 1/dot(normal, light), corrupting the independent oracle.
    std::array<std::array<double,8>,4> augmented{};
    for(unsigned row=0;row<4;++row) {
        for(unsigned column=0;column<4;++column)augmented[row][column]=matrix.at(row,column);
        augmented[row][row+4]=1;
    }
    for(unsigned column=0;column<4;++column) {
        unsigned pivot=column;
        for(unsigned row=column+1;row<4;++row)
            if(std::abs(augmented[row][column])>std::abs(augmented[pivot][column]))pivot=row;
        check(std::abs(augmented[pivot][column])>1e-14,"Oracle directional projection is singular.");
        std::swap(augmented[pivot],augmented[column]);
        const auto divisor=augmented[column][column];
        for(auto& value:augmented[column])value/=divisor;
        for(unsigned row=0;row<4;++row)if(row!=column) {
            const auto multiplier=augmented[row][column];
            for(unsigned axis=0;axis<8;++axis)augmented[row][axis]-=multiplier*augmented[column][axis];
        }
    }
    DoubleMatrix inverse;
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)inverse[row][column]=augmented[row][column+4];
    return inverse;
}
Point unproject(const DoubleMatrix& inverse,double u,double v,double depth) {
    const Clip input{u*2-1,1-v*2,depth,1};Clip p{};
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)p[row]+=inverse[row][column]*input[column];
    check(std::isfinite(p[3]) && std::abs(p[3])>1e-12,"Oracle unprojection produced an invalid homogeneous coordinate.");
    return {p[0]/p[3],p[1]/p[3],p[2]/p[3]};
}
Point receiverAt(const DoubleMatrix& inverse,double u,double v,Point planePoint,Point normal) {
    const auto near=unproject(inverse,u,v,0),far=unproject(inverse,u,v,1),ray=subtract(far,near);
    const double denominator=dot(normal,ray);
    check(std::abs(denominator)>1e-10,"Oracle receiver plane is parallel to the shadow projection.");
    return add(near,scale(ray,dot(normal,subtract(planePoint,near))/denominator));
}
Point project(const Mat4& matrix,Point p) {
    const auto clip=transform(matrix,{p[0],p[1],p[2],1});
    check(std::isfinite(clip[3]) && std::abs(clip[3])>1e-12,"Oracle projection produced an invalid homogeneous coordinate.");
    return {clip[0]/clip[3]*.5+.5,-clip[1]/clip[3]*.5+.5,clip[2]/clip[3]};
}
Pixel guardedCenter(Pixel p,Pixel origin,unsigned resolution) {
    for(unsigned axis=0;axis<2;++axis)p[axis]=std::clamp(p[axis],origin[axis]+.5,origin[axis]+resolution+3.5);
    return p;
}
template<class Sample> double bilinear(Pixel p,Pixel origin,unsigned resolution,const Sample& sample) {
    p=guardedCenter(p,origin,resolution);
    const Pixel lower{std::floor(p[0]-.5)+.5,std::floor(p[1]-.5)+.5},f{p[0]-lower[0],p[1]-lower[1]};
    double result{};
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
        result+=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*
            sample(guardedCenter({lower[0]+x,lower[1]+y},origin,resolution));
    return result;
}
// Independent filter oracle: five nominal taps per axis, each reconstructed
// bilinearly. This does not use the candidate's six combined weights.
template<class Sample> double pcf25(Pixel p,Pixel origin,unsigned resolution,const Sample& sample) {
    constexpr std::array<double,5> kernel{1,3,4,3,1};double result{};
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)
        result+=kernel[x+2]*kernel[y+2]*bilinear({p[0]+x,p[1]+y},origin,resolution,sample);
    return result/144;
}
template<class Sample> double pcf36(Pixel p,Pixel origin,unsigned resolution,const Sample& sample) {
    const Pixel base{std::floor(p[0]+.5)-.5,std::floor(p[1]+.5)-.5},f{p[0]-base[0],p[1]-base[1]};
    const auto weights=[](double s){return std::array<double,6>{1-s,3-2*s,4-s,3+s,1+2*s,s};};
    const auto wx=weights(f[0]),wy=weights(f[1]);double result{};
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<6;++x) {
        check(wx[x]>=0 && wy[y]>=0,"Expanded PCF has a negative spatial weight.");
        result+=wx[x]*wy[y]*sample(guardedCenter({base[0]+double(x)-2,base[1]+double(y)-2},origin,resolution));
    }
    return result/144;
}
// Deliberately incorrect historical implementation, used only as a negative
// control: one shallow reference for all four centers of each compressed tap.
template<class Stored> double shallowPcf9(Pixel p,Pixel origin,unsigned resolution,Pixel gradient,double depth,double tolerance,const Stored& stored) {
    const Pixel base{std::floor(p[0]+.5)-.5,std::floor(p[1]+.5)-.5},f{p[0]-base[0],p[1]-base[1]};
    const auto weights=[](double s){return std::array<double,3>{4-3*s,7,1+3*s};};
    const auto positions=[](double s){return std::array<double,3>{(3-2*s)/(4-3*s)-2,(3+s)/7,s/(1+3*s)+2};};
    const auto wx=weights(f[0]),wy=weights(f[1]),x=positions(f[0]),y=positions(f[1]);double result{};
    for(unsigned iy=0;iy<3;++iy)for(unsigned ix=0;ix<3;++ix) {
        const auto query=guardedCenter({base[0]+x[ix],base[1]+y[iy]},origin,resolution);
        const Pixel shallow{std::floor(query[0]-.5)+.5+(gradient[0]<0?1:0),std::floor(query[1]-.5)+.5+(gradient[1]<0?1:0)};
        const auto reference=depth+gradient[0]*(shallow[0]-p[0])+gradient[1]*(shallow[1]-p[1])-tolerance;
        result+=wx[ix]*wy[iy]*bilinear(query,origin,resolution,[&](Pixel center){return reference<=stored(center)?1.:0.;});
    }
    return result/144;
}
struct Counts {std::size_t projections{},planes{},wrongDiscrepancies{},footprints{},centers{};double maximumOldError{};};

void checkProjection(const directional_shadow::Cascade& fit,unsigned resolution,Counts& counts,bool footprints) {
    const auto& matrix=fit.receiverViewProjection;
    const auto inverse=inverseProjection(matrix);
    const auto rows=portableRows(matrix);
    check(close(texelWorld(rows,resolution),fit.texelWorld,1e-7,5e-5),
        "Portable row extraction disagrees with the fit's world-space texel size.");
    const auto origin=point(fit.center);
    const auto uv=project(matrix,origin);
    // A plane perpendicular to the light has a one-to-one orthographic texel
    // footprint. This checks scale independently of matrix-row lengths.
    const auto lightNormal=point(fit.forward);
    const auto start=receiverAt(inverse,uv[0],uv[1],origin,lightNormal);
    for(unsigned axis=0;axis<2;++axis) {
        const auto next=receiverAt(inverse,uv[0]+(axis==0?1.0/resolution:0),uv[1]+(axis==1?1.0/resolution:0),origin,lightNormal);
        check(close(length(subtract(next,start)),fit.texelWorld,1e-7,5e-5),
            "Independent one-pixel world displacement disagrees with shadow texel size.");
    }
    ++counts.projections;
    const auto projectionContext=context;
    for(const auto n:{Vec3{0,1,0},Vec3{.3f,1,-.2f},Vec3{-.4f,1,.5f},Vec3{.9f,.12f,.4f}}) {
        context=projectionContext+", normal="+std::to_string(n[0])+","+std::to_string(n[1])+","+std::to_string(n[2]);
        const auto normal=point(math::normalized(n));
        if(std::abs(dot(normal,lightNormal))<1e-5)continue;
        const auto expected=gradient(rows,normal,resolution),wrong=gradient(oldHlslColumns(matrix),normal,resolution);
        const auto base=receiverAt(inverse,uv[0],uv[1],origin,normal),baseProjected=project(matrix,base);
        double error{};
        for(unsigned axis=0;axis<2;++axis) {
            const auto moved=receiverAt(inverse,uv[0]+(axis==0?1.0/resolution:0),uv[1]+(axis==1?1.0/resolution:0),origin,normal);
            const auto displaced=project(matrix,moved);
            check(close(displaced[0]-baseProjected[0],axis==0?1.0/resolution:0,1e-8,1e-5) &&
                close(displaced[1]-baseProjected[1],axis==1?1.0/resolution:0,1e-8,1e-5),
                "Independent receiver displacement missed its requested shadow pixel.");
            const auto finiteDifference=displaced[2]-baseProjected[2];
            check(close(expected[axis],finiteDifference),"Portable receiver depth gradient disagrees with independent displaced world geometry.");
            error=std::max(error,std::abs(wrong[axis]-finiteDifference));
        }
        ++counts.planes;counts.maximumOldError=std::max(counts.maximumOldError,error);
        if(error>std::max(1e-6,.1*std::max(std::abs(expected[0]),std::abs(expected[1]))))++counts.wrongDiscrepancies;
        if(!footprints)continue;
        // Use a nonzero atlas tile with two rendered guard texels per side.
        // Unprojection must remove both the tile origin and inner guard offset.
        const Pixel tileOrigin{double(resolution+4),double(2*(resolution+4))};
        const auto depthAt=[&](Pixel center){return project(matrix,receiverAt(inverse,
            (center[0]-tileOrigin[0]-2)/resolution,(center[1]-tileOrigin[1]-2)/resolution,origin,normal))[2];};
        for(const auto phaseX:{.05,.3,.55,.95})for(const auto phaseY:{.1,.35,.6,.9}) {
            const Pixel pixel{tileOrigin[0]+2+uv[0]*resolution+phaseX,tileOrigin[1]+2+uv[1]*resolution+phaseY};
            const double centerDepth=depthAt(pixel),tolerance=1e-7*(1+std::abs(centerDepth)+3*std::abs(expected[0])+3*std::abs(expected[1]));
            const auto reference=[&](Pixel center){return centerDepth+expected[0]*(center[0]-pixel[0])+expected[1]*(center[1]-pixel[1]);};
            const auto stored=[&](Pixel center){
                const bool blocker=(static_cast<int>(center[0])+3*static_cast<int>(center[1]))%5<2;
                return depthAt(center)+(blocker?-1:1)*(1e-4+8*tolerance);
            };
            const auto truth=pcf25(pixel,tileOrigin,resolution,[&](Pixel center){return depthAt(center)<=stored(center)+tolerance?1.:0.;});
            const auto actual=pcf36(pixel,tileOrigin,resolution,[&](Pixel center){
                check(close(reference(center)-centerDepth,depthAt(center)-centerDepth),
                    "PCF reference disagrees with independently unprojected receiver depth at the same texel center.");
                ++counts.centers;return reference(center)<=stored(center)+tolerance?1.:0.;
            });
            check(close(actual,truth,1e-12,0),"Expanded PCF disagrees with independent weighted25-tap same-center visibility.");
            check(close(pcf36(pixel,tileOrigin,resolution,[&](Pixel center){return reference(center)<=depthAt(center)+tolerance?1.:0.;}),1,1e-12,0),
                "A self-receiving planar surface acquired PCF acne.");
            ++counts.footprints;
        }
    }
}

void numericRegression() {
    const auto direction=math::normalized(Vec3{-.5f,-.8f,-.6f});
    const auto right=math::normalized(math::cross(direction,{0,1,0})),up=math::normalized(math::cross(right,direction));
    const auto matrix=math::orthographic(-50,50,-50,50,0,200,ClipDepth::ZeroToOne)*math::lookAt({},direction,up);
    const auto correct=gradient(portableRows(matrix),{0,1,0},1024),wrong=gradient(oldHlslColumns(matrix),{0,1,0},1024);
    check(std::abs(correct[0])<1e-8 && close(correct[1],-.0004766998093204745,1e-8,0),
        "Oblique-sun horizontal-floor reference gradient changed unexpectedly.");
    const double bias=.005/200;
    check(std::max(std::abs(wrong[0]-correct[0]),std::abs(wrong[1]-correct[1]))>50*bias,
        "The regression fixture does not expose the old HLSL column error relative to the authored depth bias.");
    check(std::abs(texelWorld(oldHlslColumns(matrix),1024)/texelWorld(portableRows(matrix),1024)-1)>.01,
        "The regression fixture does not expose the old world-texel normal-bias scale error.");
}

void kernelRegression() {
    context="weighted PCF normalization, atlas guards and continuous phases";
    constexpr unsigned resolution=16;const Pixel origin{37,91};
    const auto field=[&](Pixel center){
        check(center[0]>=origin[0]+.5 && center[0]<=origin[0]+resolution+3.5 &&
            center[1]>=origin[1]+.5 && center[1]<=origin[1]+resolution+3.5,"PCF escaped its guarded atlas tile.");
        const int x=static_cast<int>(center[0]-origin[0]-.5),y=static_cast<int>(center[1]-origin[1]-.5);
        return (x+2*y)%5<2?0.:1.;
    };
    for(const auto x:{.5,1.3,2.,2.05,6.4,18.,19.5})for(const auto y:{.5,1.3,2.,2.05,6.4,18.,19.5}) {
        const Pixel pixel{origin[0]+x,origin[1]+y};
        check(close(pcf36(pixel,origin,resolution,field),pcf25(pixel,origin,resolution,field),1e-12,0),
            "Clamped expanded PCF changed weighted bilinear spatial coverage.");
        check(close(pcf36(pixel,origin,resolution,[](Pixel){return 1.;}),1,1e-12,0) &&
            pcf36(pixel,origin,resolution,[](Pixel){return 0.;})==0,"PCF lost all-lit/all-blocked normalization at an atlas edge.");
    }
    check(pcf36({origin[0]+6.4,origin[1]+6.4},origin,resolution,field)>0 &&
        pcf36({origin[0]+6.4,origin[1]+6.4},origin,resolution,field)<1,"PCF collapsed filtered coverage into a binary center result.");
    for(unsigned axis=0;axis<2;++axis)for(unsigned boundary=0;boundary<=resolution+4;++boundary) {
        Pixel before{origin[0]+6.75,origin[1]+6.75},after=before;
        before[axis]=origin[axis]+boundary+.5-1e-7;after[axis]=origin[axis]+boundary+.5+1e-7;
        check(close(pcf36(before,origin,resolution,field),pcf36(after,origin,resolution,field),2e-6,0),
            "PCF coverage jumps when its six-center footprint crosses a texel boundary.");
    }
    context.clear();
}

void perpendicularJoinRegression() {
    context="floor and perpendicular half-wall join";
    constexpr unsigned resolution=512;const Pixel origin{1032,1548};
    const auto direction=math::normalized(Vec3{.5f,-.8f,-.6f});
    const auto right=math::normalized(math::cross(direction,{0,1,0})),up=math::normalized(math::cross(right,direction));
    const auto matrix=math::orthographic(-16,16,-16,16,0,200,ClipDepth::ZeroToOne)*math::lookAt(direction*-100.f,{},up);
    const auto inverse=inverseProjection(matrix);const auto slope=gradient(portableRows(matrix),{0,1,0},resolution);
    const auto planeAt=[&](Pixel center,Point normal){return receiverAt(inverse,
        (center[0]-origin[0]-2)/resolution,(center[1]-origin[1]-2)/resolution,{},normal);};
    const auto floorDepth=[&](Pixel center){return project(matrix,planeAt(center,{0,1,0}))[2];};
    const auto stored=[&](Pixel center){
        // Floor y=0 and wall x=0,y>=0 meet at a real right angle. Intersect
        // both independently; the first light-space surface supplies depth.
        const auto wall=planeAt(center,{1,0,0});const auto floor=floorDepth(center);
        return wall[1]>=0?std::min(floor,project(matrix,wall)[2]):floor;
    };
    constexpr double texelWorld=32./resolution,tolerance=1e-8;double maximumFalseVisibility{};unsigned filteredCases{};
    for(unsigned x=1;x<=96;++x)for(unsigned phase=0;phase<8;++phase) {
        const auto receiver=project(matrix,{texelWorld*x/32,0,texelWorld*phase/4});
        const Pixel pixel{origin[0]+2+receiver[0]*resolution,origin[1]+2+receiver[1]*resolution};
        const auto truth=pcf25(pixel,origin,resolution,[&](Pixel center){return floorDepth(center)<=stored(center)+tolerance?1.:0.;});
        const auto actual=pcf36(pixel,origin,resolution,[&](Pixel center){
            const double reference=receiver[2]+slope[0]*(center[0]-pixel[0])+slope[1]*(center[1]-pixel[1]);
            return reference<=stored(center)+tolerance?1.:0.;
        });
        check(close(actual,truth,1e-12,0),"Same-center PCF disagrees with independent perpendicular-join geometry.");
        const double old=shallowPcf9(pixel,origin,resolution,slope,receiver[2],tolerance,stored);
        check(old+1e-12>=truth,"Shallow-reference negative control unexpectedly reduced join visibility.");
        maximumFalseVisibility=std::max(maximumFalseVisibility,old-truth);
        if(truth>0 && truth<1)++filteredCases;
    }
    check(filteredCases>0 && maximumFalseVisibility>.02,
        "Perpendicular join does not distinguish shallowest-reference light leakage from same-center PCF.");
    context.clear();
}
} // namespace

int main() {
    try {
        numericRegression();kernelRegression();perpendicularJoinRegression();Counts counts;
        ShadowCamera camera;camera.camera.position={12,15,12};camera.camera.target={0,1,0};camera.camera.nearPlane=.1f;
        camera.camera.farPlane=250;camera.camera.fovAxis=CameraFovAxis::Vertical;camera.aspect=16.f/9;
        const std::array<Aabb,1> casters{{{{-12,-4,-12},{14,8,12},true}}};
        for(const auto resolution:{256u,512u,1024u})for(const auto fov:{15.f,65.f})
            for(const auto direction:{Vec3{-.5f,-.8f,-.6f},Vec3{.6f,-.7f,.3f},Vec3{.9f,-.02f,.4f},
                Vec3{-.8f,-.035f,-.55f},Vec3{0,-1,0},Vec3{0,0,-1}}) {
            DirectionalLight light;light.direction=direction;light.shadowDistance=100;camera.camera.fov=fov;
            const auto scene=directional_shadow::build(light,camera,resolution,casters);
            auto firstPerson=camera;firstPerson.camera.position={0,1.65f,4};firstPerson.camera.target={0,1.65f,-4};
            const auto game=directional_shadow::build(light,firstPerson,resolution,casters);
            check(scene.valid && scene.projectionCount==4 && game.valid && game.projectionCount==4,
                "Receiver-plane fixtures failed shared Scene/FPS cascade preparation.");
            const bool footprints=resolution==512 && fov==15;
            const auto label="resolution="+std::to_string(resolution)+", fov="+std::to_string(fov)+", sun="+
                std::to_string(direction[0])+","+std::to_string(direction[1])+","+std::to_string(direction[2]);
            for(unsigned projection=0;projection<scene.projectionCount;++projection) {
                context=label+", Scene projection="+std::to_string(projection);checkProjection(scene.values[projection],resolution,counts,footprints);
            }
            for(unsigned projection=0;projection<game.projectionCount;++projection) {
                context=label+", Game projection="+std::to_string(projection);checkProjection(game.values[projection],resolution,counts,footprints);
            }
        }
        context.clear();
        check(counts.projections>=200 && counts.planes>=700 && counts.footprints>=2000 && counts.centers>=72000,
            "Receiver-plane coverage did not exercise the intended view, slope, resolution and sampling cases.");
        check(counts.wrongDiscrepancies>counts.planes/3 && counts.maximumOldError>1e-3,
            "The old HLSL column interpretation was not rejected by independent world-plane geometry.");
        std::cout<<"Directional receiver-plane checks passed: "<<counts.projections<<" projections, "<<counts.planes<<" planes, "
            <<counts.footprints<<" weighted PCF footprints / "<<counts.centers<<" same-center references; old column error up to "<<counts.maximumOldError<<" depth units.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
