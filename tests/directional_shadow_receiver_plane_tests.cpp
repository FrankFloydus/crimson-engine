#include "crimson-renderer/DirectionalShadowMath.h"
#include "crimson-renderer/ShadowReceiverBounds.h"

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
struct Counts {std::size_t projections{},planes{},wrongDiscrepancies{},footprints{};double maximumOldError{};};

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
        // Exercise the fractional texel phases that made the incorrect gradient
        // produce periodic acne. A bilinear comparison uses four centers; the
        // reference must match the shallowest receiver depth among all four.
        for(const auto phaseX:{.05,.3,.55,.95})for(const auto phaseY:{.1,.35,.6,.9})
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            const std::array<double,2> pixel{uv[0]*resolution+phaseX,uv[1]*resolution+phaseY};
            const std::array<double,2> sample{pixel[0]+x,pixel[1]+y};
            const std::array<double,2> lower{std::floor(sample[0]-.5)+.5,std::floor(sample[1]-.5)+.5};
            const std::array<double,2> shallow{lower[0]+(expected[0]<0?1:0),lower[1]+(expected[1]<0?1:0)};
            const auto center=receiverAt(inverse,pixel[0]/resolution,pixel[1]/resolution,origin,normal);
            const double centerDepth=project(matrix,center)[2];
            const double corrected=centerDepth+expected[0]*(shallow[0]-pixel[0])+expected[1]*(shallow[1]-pixel[1]);
            double minimum=std::numeric_limits<double>::infinity();
            for(unsigned corner=0;corner<4;++corner) {
                const auto p=receiverAt(inverse,(lower[0]+(corner&1?1:0))/resolution,(lower[1]+(corner&2?1:0))/resolution,origin,normal);
                minimum=std::min(minimum,project(matrix,p)[2]);
            }
            check(close(corrected-centerDepth,minimum-centerDepth),"PCF reference does not match the shallowest independently projected receiver depth.");
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
void particleReceiverChecks() {
    context="particle receivers";
    RenderParticle particle;particle.position={3,4,5};particle.size=2;
    std::array<RenderParticle,1> particles{particle};
    std::array<RenderParticleMaterial,1> materials;
    check(!particleShadowReceiverBounds(particles,{}).valid,"The empty unlit material fallback created particle receiver bounds.");
    check(!particleShadowReceiverBounds(particles,materials).valid,"An unlit particle created shadow receiver bounds.");
    materials[0].shading=MaterialShading::StandardPbr;
    auto bounds=particleShadowReceiverBounds(particles,materials);
    check(bounds.valid && close(bounds.minimum[0],3-2*.708,1e-6,0) && close(bounds.maximum[2],5+2*.708,1e-6,0),
        "A lit fallback particle lost its conservative aggregate receiver extent.");
    const auto contains=[](const Aabb& box,const Vec3& p) {
        return box.valid && p[0]>=box.minimum[0] && p[1]>=box.minimum[1] && p[2]>=box.minimum[2] &&
            p[0]<=box.maximum[0] && p[1]<=box.maximum[1] && p[2]<=box.maximum[2];
    };
    auto graph=std::make_shared<PreparedGraphMaterial>();graph->shading=MaterialShading::StandardPbr;
    materials[0].graph=graph;materials[0].shading=MaterialShading::Unlit;
    check(particleShadowReceiverBounds(particles,materials).valid,"Graph-lit shading did not override an unlit fallback.");
    graph->shading=MaterialShading::Unlit;materials[0].shading=MaterialShading::StandardPbr;
    check(!particleShadowReceiverBounds(particles,materials).valid,"Graph-unlit shading did not override a lit fallback.");
    materials[0].graph.reset();
    const auto authoredRight=math::normalized(Vec3{1,.4f,.2f});
    const auto authoredUp=math::normalized(math::cross(Vec3{.1f,1,.3f},authoredRight));
    Camera camera;camera.position={6,8,4};camera.target={};
    for(const auto mode:{ParticleBillboardMode::Disabled,ParticleBillboardMode::Enabled,ParticleBillboardMode::FixedY})
        for(const auto rotation:{0.f,.4f,1.3f,3.14159f}) {
        particles[0]=particle;particles[0].right=authoredRight;particles[0].up=authoredUp;
        particles[0].billboardMode=mode;particles[0].rotation=rotation;
        bounds=particleShadowReceiverBounds(particles,materials);check(bounds.valid,"A valid lit billboard did not contribute receivers.");
        auto right=authoredRight,up=authoredUp;
        if(mode==ParticleBillboardMode::Enabled) {right=camera.right();up=math::normalized(math::cross(right,camera.forward()));}
        if(mode==ParticleBillboardMode::FixedY) {up={0,1,0};right=math::cross(up,math::normalized(Vec3{.6f,0,.8f}));}
        for(unsigned corner=0;corner<4;++corner) {
            const double x=(corner&1?.5:-.5)*particle.size,y=(corner&2?.5:-.5)*particle.size;
            const auto rotatedX=std::cos(rotation)*x-std::sin(rotation)*y,rotatedY=std::sin(rotation)*x+std::cos(rotation)*y;
            check(contains(bounds,particle.position+right*rotatedX+up*rotatedY),
                "A rotated Disabled/Enabled/FixedY sprite corner escaped its aggregate receiver bounds.");
        }
    }
    particles[0]=particle;
    particles[0].right={std::sqrt(1.0009f),0,0};
    particles[0].up[0]=.0009f/particles[0].right[0];
    particles[0].up[1]=std::sqrt(1.0009f-particles[0].up[0]*particles[0].up[0]);particles[0].up[2]=0;
    check(particle_rendering::valid(particles[0]),"The orientation-tolerance fixture exceeds the renderer's accepted bounds.");
    bounds=particleShadowReceiverBounds(particles,materials);
    for(unsigned corner=0;corner<4;++corner)
        check(contains(bounds,particle.position+particles[0].right*((corner&1?.5:-.5)*particle.size)+particles[0].up*((corner&2?.5:-.5)*particle.size)),
            "An accepted near-tolerance sprite basis escaped the conservative receiver extent.");

    particles[0]=particle;particles[0].size=0;
    check(!particleShadowReceiverBounds(particles,materials).valid,"A zero-size particle contributed receiver bounds.");
    particles[0]=particle;particles[0].position[0]=std::numeric_limits<float>::infinity();
    check(!particleShadowReceiverBounds(particles,materials).valid,"A nonfinite particle contributed receiver bounds.");
    particles[0]=particle;particles[0].right={2,0,0};
    check(!particleShadowReceiverBounds(particles,materials).valid,"An invalid orientation contributed receiver bounds.");
    particles[0]=particle;particles[0].materialIndex=1;
    check(!particleShadowReceiverBounds(particles,materials).valid,"An absent particle material contributed receiver bounds.");

    std::vector<RenderParticleMaterial> cappedMaterials(MaxParticleMaterialCount+1);
    cappedMaterials.back().shading=MaterialShading::StandardPbr;
    particles[0]=particle;particles[0].materialIndex=static_cast<std::uint32_t>(MaxParticleMaterialCount);
    check(!particleShadowReceiverBounds(particles,cappedMaterials).valid,"A material beyond the renderer cap contributed receivers.");
    particles[0].materialIndex=static_cast<std::uint32_t>(MaxParticleMaterialCount-1);
    cappedMaterials[MaxParticleMaterialCount-1].shading=MaterialShading::StandardPbr;
    check(particleShadowReceiverBounds(particles,cappedMaterials).valid,"The last admitted particle material lost receiver bounds.");
    std::vector<RenderParticle> cappedParticles(MaxParticleDrawCount+1);
    for(auto& p:cappedParticles)p.size=0;
    cappedParticles.front()=particle;cappedParticles.front().position={};
    cappedParticles.back()=particle;cappedParticles.back().position={500,0,0};
    bounds=particleShadowReceiverBounds(cappedParticles,materials);
    check(bounds.valid && bounds.maximum[0]<2,"A particle beyond the renderer cap expanded shadow receivers.");
    cappedParticles[MaxParticleDrawCount-1]=particle;cappedParticles[MaxParticleDrawCount-1].position={100,0,0};
    check(particleShadowReceiverBounds(cappedParticles,materials).maximum[0]>100,"The last admitted particle lost receiver bounds.");
    context.clear();
}
} // namespace

int main() {
    try {
        particleReceiverChecks();numericRegression();Counts counts;
        ShadowCamera camera;camera.camera.position={12,15,12};camera.camera.target={0,1,0};camera.camera.nearPlane=.1f;
        camera.camera.farPlane=250;camera.camera.fovAxis=CameraFovAxis::Vertical;camera.aspect=16.f/9;
        const Aabb character{{-.4f,0,-.4f},{.4f,1.8f,.4f},true};
        const std::array<Aabb,1> casters{{{{-12,-4,-12},{14,8,12},true}}};
        const std::array<Aabb,2> receivers{Aabb{{-10000,0,-10000},{10000,0,10000},true},character};
        for(const auto resolution:{256u,512u,1024u})for(const auto fov:{15.f,65.f})
            for(const auto direction:{Vec3{-.5f,-.8f,-.6f},Vec3{.6f,-.7f,.3f},Vec3{.9f,-.02f,.4f},
                Vec3{-.8f,-.035f,-.55f},Vec3{0,-1,0},Vec3{0,0,-1}}) {
            DirectionalLight light;light.direction=direction;light.shadowDistance=100;camera.camera.fov=fov;
            const auto scene=directional_shadow::build(light,camera,resolution,casters);
            const auto game=directional_shadow::buildGame(light,camera,resolution,casters,character);
            const auto cropped=directional_shadow::buildGame(light,camera,resolution,casters,character,receivers);
            check(scene.valid && scene.projectionCount==4 && game.valid && game.projectionCount==2 && cropped.valid && cropped.projectionCount==2,
                "Receiver-plane fixtures failed Scene/Game/character projection preparation.");
            const bool footprints=resolution==512 && fov==15;
            const auto label="resolution="+std::to_string(resolution)+", fov="+std::to_string(fov)+", sun="+
                std::to_string(direction[0])+","+std::to_string(direction[1])+","+std::to_string(direction[2]);
            for(unsigned projection=0;projection<scene.projectionCount;++projection) {
                context=label+", Scene projection="+std::to_string(projection);checkProjection(scene.values[projection],resolution,counts,footprints);
            }
            for(unsigned projection=0;projection<game.projectionCount;++projection) {
                context=label+", Game projection="+std::to_string(projection);checkProjection(game.values[projection],resolution,counts,footprints);
            }
            for(unsigned projection=0;projection<cropped.projectionCount;++projection) {
                context=label+", cropped Game projection="+std::to_string(projection);checkProjection(cropped.values[projection],resolution,counts,footprints);
            }
        }
        context.clear();
        check(counts.projections>=200 && counts.planes>=700 && counts.footprints>=10000,
            "Receiver-plane coverage did not exercise the intended view, slope, resolution and sampling cases.");
        check(counts.wrongDiscrepancies>counts.planes/3 && counts.maximumOldError>1e-3,
            "The old HLSL column interpretation was not rejected by independent world-plane geometry.");
        std::cout<<"Directional receiver-plane checks passed: "<<counts.projections<<" projections, "<<counts.planes<<" planes, "
            <<counts.footprints<<" PCF footprints; old column error up to "<<counts.maximumOldError<<" depth units.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
