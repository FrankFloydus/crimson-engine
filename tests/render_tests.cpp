#include "crimson-renderer/Mesh.h"
#include "crimson-renderer/RenderTypes.h"
#include "crimson-renderer/ModelTransform.h"
#include "crimson-renderer/ParticleRenderTypes.h"
#include "crimson-renderer/shaders/point_light.sh"
#include "crimson-renderer/shaders/grid_depth.sh"
#include "input/Navigation.h"
#include <SDL3/SDL_init.h>

#include <cmath>
#include "crimson-renderer/shaders/glow_kernel.sh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance = 1e-5) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            std::to_string(actual) + " differs from " + std::to_string(expected));
}

template <typename Exception, typename Function>
void throws(Function&& function) {
    try { function(); }
    catch (const Exception&) { return; }
    catch (...) { throw std::runtime_error("Unexpected exception type"); }
    throw std::runtime_error("Expected exception was not thrown");
}

double length(double x, double y, double z) { return std::sqrt(x*x + y*y + z*z); }

// Project shader inputs in float, then clip in double: sequential CPU float
// intersections introduce artificial seam cracks and are not a hardware clipper
// model. A giant ground quad can cross the eye while visible fragments remain
// well inside the near/far planes.
crimson::Vec4 shaderProject(const crimson::Mat4& matrix,const crimson::Vec3& point) {
    crimson::Vec4 result;
    for(unsigned row=0;row<4;++row)
        result[row]=matrix.at(row,0)*point[0]+matrix.at(row,1)*point[1]+matrix.at(row,2)*point[2]+matrix.at(row,3);
    return result;
}
struct ClippedGridVertex {
    std::array<double,4> clip{};std::array<double,3> world{};
    ClippedGridVertex()=default;
    ClippedGridVertex(const crimson::Vec4& position,const crimson::Vec3& varying)
        :clip{position[0],position[1],position[2],position[3]},world{varying[0],varying[1],varying[2]} {}
};
std::vector<ClippedGridVertex> clipGridTriangle(std::vector<ClippedGridVertex> polygon,crimson::ClipDepth depth) {
    const std::array<crimson::Vec4,6> planes{{{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1},
        {0,0,1,depth==crimson::ClipDepth::ZeroToOne?0.f:1.f},{0,0,-1,1}}};
    for(const auto& plane:planes) {
        if(polygon.empty())break;
        const auto distance=[&](const auto& vertex) {
            double value=0;for(unsigned i=0;i<4;++i)value+=vertex.clip[i]*plane[i];return value;
        };
        std::vector<ClippedGridVertex> clipped;
        auto previous=polygon.back();double before=distance(previous);
        for(const auto& current:polygon) {
            const double after=distance(current);
            if((before>=0)!=(after>=0)) {
                const double t=before/(before-after);ClippedGridVertex intersection;
                for(unsigned i=0;i<4;++i)intersection.clip[i]=previous.clip[i]+t*(current.clip[i]-previous.clip[i]);
                for(unsigned i=0;i<3;++i)intersection.world[i]=previous.world[i]+t*(current.world[i]-previous.world[i]);
                clipped.push_back(intersection);
            }
            if(after>=0)clipped.push_back(current);
            previous=current;before=after;
        }
        polygon=std::move(clipped);
    }
    return polygon;
}
template<class Sample> void sampleGridPolygon(const std::vector<ClippedGridVertex>& polygon,const crimson::Vec2& ndc,Sample sample) {
    for(std::size_t index=1;index+1<polygon.size();++index) {
        const std::array<ClippedGridVertex,3> triangle{polygon[0],polygon[index],polygon[index+1]};
        std::array<std::array<double,2>,3> screen;
        bool valid=true;
        for(unsigned i=0;i<3;++i) {
            if(triangle[i].clip[3]<=0) {valid=false;break;}
            screen[i]={double(triangle[i].clip[0])/triangle[i].clip[3],double(triangle[i].clip[1])/triangle[i].clip[3]};
        }
        if(!valid)continue;
        const auto& a=screen[0];const auto& b=screen[1];const auto& c=screen[2];
        const double determinant=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
        if(std::abs(determinant)<1e-14)continue;
        const double first=((b[1]-c[1])*(ndc[0]-c[0])+(c[0]-b[0])*(ndc[1]-c[1]))/determinant;
        const double second=((c[1]-a[1])*(ndc[0]-c[0])+(a[0]-c[0])*(ndc[1]-c[1]))/determinant;
        const std::array<double,3> barycentric{first,second,1-first-second};
        if(*std::min_element(barycentric.begin(),barycentric.end())<-1e-6)continue;
        double total=0;std::array<double,3> world{};double rasterDepth=0;
        for(unsigned i=0;i<3;++i) {
            const double weight=barycentric[i]/triangle[i].clip[3];total+=weight;
            for(unsigned axis=0;axis<3;++axis)world[axis]+=weight*triangle[i].world[axis];
            rasterDepth+=barycentric[i]*triangle[i].clip[2]/triangle[i].clip[3];
        }
        sample(crimson::Vec3{world[0]/total,world[1]/total,world[2]/total},rasterDepth);
    }
}
void gridDepthSurvivesEyeCrossing() {
    using namespace crimson;
    using grid_rendering::gridFragmentDepth;
    const std::array<Vec3,4> corners{{{-512,0,-512},{512,0,-512},{512,0,512},{-512,0,512}}};
    const std::array<std::array<unsigned,3>,2> triangles{{{0,2,1},{0,3,2}}};
    unsigned samples=0,oldOcclusionFailures=0;
    for(const auto depth:{ClipDepth::ZeroToOne,ClipDepth::NegativeOneToOne})
    for(const float nearPlane:{.00001f,.000866f,.01f})for(const float distance:{4.f,10.f,50.f})
    for(int pitch=5;pitch<=85;pitch+=20)for(int yaw=0;yaw<360;yaw+=15) {
        const double elevation=pitch*std::numbers::pi/180,azimuth=yaw*std::numbers::pi/180;
        Camera camera;camera.nearPlane=nearPlane;camera.farPlane=2048;camera.target={};camera.up={0,1,0};
        camera.position={distance*std::cos(elevation)*std::sin(azimuth),distance*std::sin(elevation),distance*std::cos(elevation)*std::cos(azimuth)};
        const auto view=camera.viewMatrix(),projection=camera.projectionMatrix(1.6f,depth);
        Mat4 matrix=Mat4::zero();
        for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)for(unsigned i=0;i<4;++i)
            matrix.at(row,column)+=projection.at(row,i)*view.at(i,column);
        const float homogeneous=depth==ClipDepth::NegativeOneToOne?1.f:0.f;
        // Stay inside one original triangle; the shared diagonal's raster
        // ownership is independent from the depth behavior under regression.
        const Vec3 point{.5f,0,-.25f};
        const auto projected=shaderProject(matrix,point);const Vec2 ndc{projected[0]/projected[3],projected[1]/projected[3]};
        // The same viewing ray reaches this plane AFTER reaching the grid.
        const auto lower=shaderProject(matrix,point+(point-camera.position)*(.118f/camera.position[1]));
        const double floorDepth=(lower[2]/lower[3])*(1-.5*homogeneous)+.5*homogeneous;
        std::array<ClippedGridVertex,4> vertices,oldVertices;
        for(unsigned i=0;i<4;++i) {
            auto clip=shaderProject(matrix,corners[i]);vertices[i]={clip,corners[i]};
            // Frozen regression: the old vertex-depth bias is not sufficient
            // after clipping amplifies forward-depth rounding at some angles.
            const float normalized=(clip[2]/clip[3])*(1-.5f*homogeneous)+.5f*homogeneous;
            const float bias=.000001f+(.005f-.000001f)*(1-std::clamp(normalized,0.f,1.f));
            clip[2]-=clip[3]*bias*(1+homogeneous);
            oldVertices[i]={clip,corners[i]};
        }
        unsigned visibleSamples=0;
        for(const auto& triangle:triangles) {
            sampleGridPolygon(clipGridTriangle({vertices[triangle[0]],vertices[triangle[1]],vertices[triangle[2]]},depth),ndc,
                [&](const Vec3& world,double) {
                    const auto clip=shaderProject(matrix,world);
                    const float corrected=gridFragmentDepth(clip[2],clip[3],.005f,homogeneous);
                    require(std::isfinite(corrected)&&corrected>=0&&corrected<=1,"Grid fragment depth left the hardware depth range.");
                    require(corrected<floorDepth,"A physically lower floor occluded the grid after clipping.");
                    ++visibleSamples;++samples;
                });
            sampleGridPolygon(clipGridTriangle({oldVertices[triangle[0]],oldVertices[triangle[1]],oldVertices[triangle[2]]},depth),ndc,
                [&](const Vec3&,double raster) {if(raster*(1-.5*homogeneous)+.5*homogeneous>floorDepth)++oldOcclusionFailures;});
        }
        require(visibleSamples>0,"The angular regression did not cover its interior grid sample: depth="+std::to_string(static_cast<int>(depth))+
            ", near="+std::to_string(nearPlane)+", distance="+std::to_string(distance)+", pitch="+std::to_string(pitch)+", yaw="+std::to_string(yaw));
    }
    require(samples>2000&&oldOcclusionFailures>0,"The grid regression did not exercise the original clipping-depth loss.");
    // Preserve occlusion by a genuinely nearer surface; this is not an always-on-top grid.
    Camera camera;camera.position={0,5,10};camera.target={};camera.nearPlane=.01f;
    const auto matrix=camera.projectionMatrix(1.6f,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const auto grid=shaderProject(matrix,{}),foreground=shaderProject(matrix,camera.position*.5);
    require(gridFragmentDepth(grid[2],grid[3],.005f,0)>foreground[2]/foreground[3],"Grid bias pierced a foreground surface.");
}

void pointLightRadiusContract() {
    using namespace crimson;
    using point_lighting::pointLightAttenuation;
    PointLight light;near(light.radius,20);
    const auto attenuation=[&](float distance){return pointLightAttenuation(distance*distance,light.inverseRadiusSquared());};
    near(light.inverseRadiusSquared(),1.f/400.f);
    near(attenuation(0),100); // Existing finite inverse-square source cap.
    require(attenuation(5)*25>.99f,"Default radius unexpectedly changed near-field legacy illumination.");
    float previous=attenuation(0);
    for(unsigned index=1;index<=200;++index) {
        const auto value=attenuation(static_cast<float>(index)*.1f);
        require(std::isfinite(value) && value>=0 && value<=previous,"Point-light attenuation is not finite, positive and monotonic.");
        previous=value;
    }
    near(attenuation(20),0);near(attenuation(21),0);near(attenuation(1000),0);
    const float nearEdge=attenuation(19.99f),fartherFromEdge=attenuation(19.98f);
    require(nearEdge>0 && fartherFromEdge>0 && nearEdge<fartherFromEdge*.3f,
        "Point-light radius did not approach zero with a smooth zero-slope boundary.");
    const auto smallRadius=attenuation(10);light.radius=40;
    require(attenuation(10)>smallRadius && attenuation(30)>0,"Increasing radius did not extend and brighten its influence region.");
    near(attenuation(40),0);
    for(const float radius:{.01f,100000.f,0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        light.radius=radius;const auto inverse=light.inverseRadiusSquared();
        require(std::isfinite(inverse) && inverse>0 && std::isfinite(pointLightAttenuation(0,inverse)),
            "Point-light radius packing generated a nonfinite shader parameter.");
    }
}

void pointLightNearFalloffContract() {
    using crimson::point_lighting::pointLightAttenuation;
    constexpr float inverseRangeSquared=1.f/64.f,nearRadius=1.5f;
    const auto softened=[&](float distance) {
        return pointLightAttenuation(distance*distance,inverseRangeSquared,nearRadius*nearRadius);
    };
    // Keep ordinary sources bit-identical, including the finite source cap,
    // while bounding the rig's close-wall energy by its authored soft core.
    for(const float distance:{0.f,.05f,.1f,.25f,1.f,2.f,7.9f,8.f,20.f}) {
        const float squared=distance*distance;
        require(pointLightAttenuation(squared,inverseRangeSquared)==
            pointLightAttenuation(squared,inverseRangeSquared,0),
            "Zero near falloff changed an ordinary point light's illumination.");
        require(softened(distance)<=1.f/(nearRadius*nearRadius),
            "Softened point light exceeded its finite near-source energy bound.");
    }
    near(softened(0),1.f/(nearRadius*nearRadius));
    require(softened(.25f)<pointLightAttenuation(.25f*.25f,inverseRangeSquared)*.03f,
        "Player fill retained the concentrated quarter-metre wall hotspot.");
    require(softened(2)>pointLightAttenuation(4,inverseRangeSquared)*.6f,
        "Near-source softening excessively suppressed the surrounding world fill.");
    float previous=softened(0);
    for(unsigned step=1;step<=800;++step) {
        const float value=softened(static_cast<float>(step)*.01f);
        require(std::isfinite(value)&&value>=0&&value<=previous,
            "Softened light falloff is not finite, positive and monotonic.");
        previous=value;
    }
    near(softened(8),0);near(softened(9),0);
    require(softened(7.99f)<softened(7.98f)*.3f,
        "Near-source softening changed the smooth outer range cutoff.");
    const float leftSlope=(softened(nearRadius)-softened(nearRadius-.001f))/.001f;
    const float rightSlope=(softened(nearRadius+.001f)-softened(nearRadius))/.001f;
    require(std::abs(leftSlope-rightSlope)<.001f,
        "Near falloff introduced a visible hard ring at the soft-core radius.");
}

void pointLightProfileContract() {
    using namespace crimson::point_lighting;
    constexpr float inverse=1.f/64.f,nearSquared=2.25f;
    for(float distance:{0.f,.05f,.25f,1.f,2.f,4.f,7.99f,8.f,20.f}) {
        const float squared=distance*distance;
        const float relative=squared*inverse,window=1-relative*relative,fade=std::max(window,0.f);
        const float original=fade*fade/std::max(squared+nearSquared,.01f);
        require(pointLightAttenuation(squared,inverse,nearSquared,4,1)==original&&
            pointLightAttenuation(squared,inverse,nearSquared)==original,
            "Default exponent/edge response changed original quartic point lighting.");
        near(pointLightAttenuation(squared,inverse,nearSquared,1,1),
            std::pow(std::max(1-double(distance)/8,0.),2)/std::max(double(squared)+nearSquared,.01),1e-6);
    }
    for(float exponent:{.25f,1.f,4.f,8.f})for(float softness:{.05f,.25f,1.f}) {
        float previous=pointLightAttenuation(0,inverse,nearSquared,exponent,softness);
        for(unsigned step=1;step<=1000;++step) {
            const float distance=step*.01f;
            const float value=pointLightAttenuation(distance*distance,inverse,nearSquared,exponent,softness);
            require(std::isfinite(value)&&value>=0&&value<=previous+1e-7f,
                "Tuned finite point profile is not finite, positive and monotonic.");
            if(distance>=8)require(value==0,"Tuned point illumination escaped its authored radius.");
            previous=value;
        }
        require(pointLightAttenuation(7.999f*7.999f,inverse,nearSquared,exponent,softness)<
            pointLightAttenuation(7.998f*7.998f,inverse,nearSquared,exponent,softness)*.3f,
            "Tuned point profile lost its zero-slope outer boundary.");
    }
    require(pointLightAttenuation(4,inverse,nearSquared,1,1)<pointLightAttenuation(4,inverse,nearSquared,4,1),
        "Lower falloff exponent did not broaden the progressive range fade.");
    near(pointLightAttenuation(4,inverse,nearSquared,1,.25f),1.f/(4+nearSquared));
    for(float decay:{-3.f,0.f,1.f,2.f,3.f,10.f})for(float distance:{.05f,.5f,1.f,2.f,4.f,7.9f,8.f,10.f}) {
        const auto squared=distance*distance;
        const auto value=pointLightAttenuation(squared,inverse,0,4,1,decay);
        const double window=std::pow(std::max(1-std::pow(double(distance)/8,4),0.),2);
        const double expected=window*std::pow(std::max(double(squared),.01),-.5*double(decay));
        require(std::isfinite(value)&&value>=0,"Distance attenuation produced nonfinite or negative light.");
        require(std::abs(double(value)-expected)<=std::max(1e-6,std::abs(expected)*2e-5),
            "Distance attenuation disagrees with the independent Godot-style power reference.");
        if(decay==2)require(value==pointLightAttenuation(squared,inverse,0,4,1),
            "Default attenuation changed the prior point-light arithmetic.");
    }
    for(float decay:{-10.f,0.f,10.f}) {
        require(std::isfinite(pointLightAttenuation(1e10f,1.f/1e10f,1e10f,4,1,decay)),
            "Extreme valid distance attenuation overflowed HDR at the range boundary.");
        require(std::isfinite(pointLightAttenuation(9e9f,1.f/1e10f,1e10f,4,1,decay)),
            "Extreme valid distance attenuation overflowed HDR inside its range.");
    }
    for(float distance:{.1f,1.f,2.f,100000.f}) {
        require(pointLightSourceSinSquared(distance*distance,0)==0,
            "Zero Source Radius changed punctual specular response.");
        near(pointLightSourceSinSquared(distance*distance,1),std::min(1./(double(distance)*distance),1.),1e-6);
    }
    constexpr float alphaSquared=.0016f;
    require(pointLightSpecularAlphaSquared(alphaSquared,.7f,0)==alphaSquared,
        "Zero source angular size changed the original GGX lobe.");
    const auto broad=pointLightSpecularAlphaSquared(alphaSquared,1,.25f);
    near(broad,alphaSquared+.25/4,1e-6);
    require(broad>alphaSquared&&pointLightSpecularAlphaSquared(alphaSquared,0,1)==1,
        "Source Radius failed to broaden and bound GGX roughness.");
}

double glowDisplayCode(double hdr) {
    // The scene's Reinhard presentation followed by its one sRGB encoding.
    const double mapped=hdr/(1+hdr);
    return 255*(mapped<=.0031308 ? 12.92*mapped : 1.055*std::pow(mapped,1/2.4)-.055);
}

void glowGaussianKernelContract() {
    using namespace crimson::glow_rendering;
    std::vector<float> sizes{.01f,.1f,.5f,1.f,1.69f,8.f};
    for(unsigned i=1;i<=800;++i)sizes.push_back(static_cast<float>(i)*.01f);
    // Just below/at/above each support-size change catches the worst tail,
    // including tiny kernels whose normalized central sample is nearly one.
    for(unsigned edge=2;edge<=195;++edge) {
        const float size=static_cast<float>(edge)/(7.5f*3.25f);
        for(const float offset:{-0.000001f,0.f,0.000001f})
            if(size+offset>0 && size+offset<=8)sizes.push_back(size+offset);
    }
    constexpr double BrightMeshImpulse=60000.0*100.0*10.0;
    for(const float size:sizes) {
        const float sigma=glowKernelSigma(size);
        const int radius=glowKernelRadius(sigma);
        require(std::isfinite(sigma) && sigma>=.25f && radius>=2 && radius<=195,
            "Glow Size escaped its finite Gaussian support/tap bound.");
        near(sigma,std::max(.25f,3.25f*size));
        std::vector<double> weights(static_cast<std::size_t>(radius*2+1));
        double total=0,previous=1;
        for(int offset=-radius;offset<=radius;++offset) {
            const double weight=glowKernelWeight(static_cast<float>(offset),sigma);
            require(std::isfinite(weight) && weight>=0 && weight<=1,"Glow kernel weight is nonfinite or negative.");
            near(weight,glowKernelWeight(static_cast<float>(-offset),sigma),0);
            near(weight,std::exp(-.5*std::pow(static_cast<double>(offset)/sigma,2)),2e-7);
            if(offset>=0) {
                require(weight<=previous,"Glow kernel introduced a separated bright lobe.");
                previous=weight;
            }
            weights[static_cast<std::size_t>(offset+radius)]=weight;
            total+=weight;
        }
        double normalizedSum=0,mean=0,variance=0;
        for(int offset=-radius;offset<=radius;++offset) {
            auto& weight=weights[static_cast<std::size_t>(offset+radius)];
            weight/=total;normalizedSum+=weight;mean+=offset*weight;variance+=offset*offset*weight;
        }
        near(normalizedSum,1,1e-12);near(normalizedSum*normalizedSum,1,1e-12);near(mean,0,1e-12);
        // A sampled Gaussian at sigma >= 1 agrees with the continuous variance;
        // this protects Size/core width against narrowing the blur to hide its edge.
        if(sigma>=1)near(variance,static_cast<double>(sigma)*sigma,1e-5*static_cast<double>(sigma)*sigma);
        const double edgeHdr=BrightMeshImpulse*weights.back()*weights[static_cast<std::size_t>(radius)];
        require(glowDisplayCode(edgeHdr)<1,
            "Glow's last supported tap still creates a visible cutoff for a bright mesh sample.");
        const int oldRadius=std::min(64,static_cast<int>(std::ceil(8.f*size)));
        double oldTotal=0;
        for(int offset=-oldRadius;offset<=oldRadius;++offset)
            oldTotal+=std::exp(-.5*std::pow(static_cast<double>(offset)/sigma,2));
        require(oldTotal/total>.98 && oldTotal/total<=1.0000001,
            "Extending Glow tails unexpectedly changed central energy instead of preserving its Gaussian.");
    }
    near(glowKernelRadius(glowKernelSigma(8)),195,0);
    const float sigma=glowKernelSigma(1.69f);
    const int oldRadius=static_cast<int>(std::ceil(8.f*1.69f));
    double oldTotal=0;
    for(int offset=-oldRadius;offset<=oldRadius;++offset)
        oldTotal+=std::exp(-.5*std::pow(static_cast<double>(offset)/sigma,2));
    const double oldEdge=15000*std::exp(-.5*std::pow(static_cast<double>(oldRadius)/sigma,2))/(oldTotal*oldTotal);
    require(glowDisplayCode(oldEdge)>200,
        "The bright-impulse regression no longer exercises the old square Glow cutoff.");
}

void glowGaussianContours() {
    using namespace crimson::glow_rendering;
    for(const float size:{.5f,1.f,1.69f,8.f}) {
        const float sigma=glowKernelSigma(size);
        const int radius=glowKernelRadius(sigma);
        std::vector<double> weights(static_cast<std::size_t>(radius+1));
        double total=0;
        for(int i=0;i<=radius;++i) {
            weights[static_cast<std::size_t>(i)]=glowKernelWeight(static_cast<float>(i),sigma);
            total+=weights[static_cast<std::size_t>(i)]*(i==0?1:2);
        }
        const auto sample=[&](double x) {
            x=std::abs(x);const auto index=static_cast<int>(std::floor(x));const double blend=x-index;
            const double a=index<=radius?weights[static_cast<std::size_t>(index)]/total:0;
            const double b=index+1<=radius?weights[static_cast<std::size_t>(index+1)]/total:0;
            return a*(1-blend)+b*blend;
        };
        // Model the final bilinear upsample of an isolated HDR impulse. Equal
        // brightness contours should remain circular; separable compact-tail
        // windows can pass endpoint tests while turning these contours box-shaped.
        for(const double width:{2.,3.,4.,5.}) {
            const double axisRadius=width*sigma,target=sample(axisRadius)*sample(0);
            for(const double impulse:{15000.,60000000.}) {
                const double display=glowDisplayCode(impulse*target);
                if(display<1 || display>254)continue;
                double lower=0,upper=2.*radius;
                for(unsigned step=0;step<60;++step) {
                    const double middle=(lower+upper)*.5;
                    const double diagonal=sample(middle/std::sqrt(2.));
                    if(diagonal*diagonal>target)lower=middle;else upper=middle;
                }
                const double diagonalRadius=(lower+upper)*.5;
                require(std::abs(diagonalRadius/axisRadius-1)<.025,
                    "Glow's HDR contour became square despite a continuous support edge.");
            }
        }
    }
}

void checkSphere(uint32_t segments, uint32_t rings, float radius) {
    const auto mesh = crimson::makeUvSphere(segments, rings, radius);
    require(mesh.vertices.size() == (uint64_t{segments} + 1) * (uint64_t{rings} + 1), "Unexpected sphere vertex count");
    require(mesh.indices.size() == uint64_t{6} * segments * (rings - 1), "Unexpected sphere triangle count");
    require(mesh.indices.size() % 3 == 0, "Incomplete triangle");
    for (const auto& v : mesh.vertices) {
        require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), "Nonfinite sphere position");
        require(std::isfinite(v.nx) && std::isfinite(v.ny) && std::isfinite(v.nz), "Nonfinite normal");
        require(std::isfinite(v.tx) && std::isfinite(v.ty) && std::isfinite(v.tz) && std::isfinite(v.tw), "Nonfinite tangent");
        near(length(v.x, v.y, v.z), radius);
        near(length(v.nx, v.ny, v.nz), 1);
        near(length(v.tx, v.ty, v.tz), 1);
        near(v.nx*v.tx + v.ny*v.ty + v.nz*v.tz, 0);
        near(v.tw, 1);
        near((v.x*v.nx + v.y*v.ny + v.z*v.nz) / radius, 1);
        require(v.u >= 0 && v.u <= 1 && v.v >= 0 && v.v <= 1, "UV outside [0,1]");
    }
    for (const auto index : mesh.indices) require(index < mesh.vertices.size(), "Index outside vertex buffer");
}

void sphereCountsAndNormals() {
    checkSphere(3, 2, 1.0f);
    checkSphere(16, 8, 2.75f);
    checkSphere(64, 32, .5f);
}

void sphereWinding() {
    const auto mesh = crimson::makeUvSphere(16, 8, 1);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const double ux = b.x-a.x, uy = b.y-a.y, uz = b.z-a.z;
        const double vx = c.x-a.x, vy = c.y-a.y, vz = c.z-a.z;
        const double nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
        require(length(nx, ny, nz) > 1e-8, "Degenerate pole triangle was retained");
        require(nx*(a.x+b.x+c.x) + ny*(a.y+b.y+c.y) + nz*(a.z+b.z+c.z) > 0,
                "Triangle winding faces into the sphere");
    }
}

void sphereSeamAndPoles() {
    constexpr uint32_t segments = 16, rings = 8;
    const auto mesh = crimson::makeUvSphere(segments, rings, 1);
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        const auto& first = mesh.vertices[ring * (segments + 1)];
        const auto& last = mesh.vertices[ring * (segments + 1) + segments];
        near(first.x, last.x); near(first.y, last.y); near(first.z, last.z);
        near(first.nx, last.nx); near(first.ny, last.ny); near(first.nz, last.nz);
        near(first.tx, last.tx); near(first.ty, last.ty); near(first.tz, last.tz); near(first.tw, last.tw);
        near(first.u, 0); near(last.u, 1); near(first.v, last.v);
    }
    for (uint32_t segment = 0; segment <= segments; ++segment) {
        const auto& top = mesh.vertices[segment];
        const auto& bottom = mesh.vertices[rings * (segments + 1) + segment];
        near(top.x, 0); near(top.y, 1); near(top.z, 0);
        near(bottom.x, 0); near(bottom.y, -1); near(bottom.z, 0);
    }
}

void sphereTangentOrientation() {
    constexpr uint32_t segments = 16, rings = 8;
    const auto mesh = crimson::makeUvSphere(segments, rings, 1);
    for (uint32_t row = 0; row <= rings; ++row) {
        for (uint32_t col = 1; col < segments; ++col) {
            const auto& v = mesh.vertices[row * (segments + 1) + col];
            // B must follow +V, which travels from the north pole to the south.
            const double bx = (v.ny*v.tz - v.nz*v.ty)*v.tw;
            const double by = (v.nz*v.tx - v.nx*v.tz)*v.tw;
            const double bz = (v.nx*v.ty - v.ny*v.tx)*v.tw;
            near(length(bx,by,bz),1);
            const uint32_t previousRow = row == 0 ? 0 : row-1;
            const uint32_t nextRow = row == rings ? rings : row+1;
            const auto& previousV = mesh.vertices[previousRow*(segments+1)+col];
            const auto& nextV = mesh.vertices[nextRow*(segments+1)+col];
            require(bx*(nextV.x-previousV.x) + by*(nextV.y-previousV.y) + bz*(nextV.z-previousV.z) > 0,
                    "Normal-map green direction disagrees with increasing V");

            // U collapses at the poles; there its analytic tangent must agree
            // with the neighboring ring instead of inheriting a zero derivative.
            const uint32_t derivativeRow = row == 0 ? 1 : row == rings ? rings-1 : row;
            const auto& previousU = mesh.vertices[derivativeRow*(segments+1)+col-1];
            const auto& nextU = mesh.vertices[derivativeRow*(segments+1)+col+1];
            require(v.tx*(nextU.x-previousU.x) + v.ty*(nextU.y-previousU.y) + v.tz*(nextU.z-previousU.z) > 0,
                    "Normal-map red direction disagrees with increasing U at ring or pole");
            const auto& adjacent = mesh.vertices[derivativeRow*(segments+1)+col];
            near(v.tx,adjacent.tx); near(v.ty,adjacent.ty); near(v.tz,adjacent.tz);
        }
    }
    // A conventional flat tangent-space normal transforms exactly back to N.
    for (const auto& v : mesh.vertices) {
        const double bx = (v.ny*v.tz-v.nz*v.ty)*v.tw;
        const double by = (v.nz*v.tx-v.nx*v.tz)*v.tw;
        const double bz = (v.nx*v.ty-v.ny*v.tx)*v.tw;
        // Check full orientation rather than just independent unit lengths.
        near(v.ty*bz-v.tz*by,v.nx);
        near(v.tz*bx-v.tx*bz,v.ny);
        near(v.tx*by-v.ty*bx,v.nz);
    }
}

void sphereRejectsInvalidInput() {
    throws<std::invalid_argument>([] { crimson::makeUvSphere(2, 2, 1); });
    throws<std::invalid_argument>([] { crimson::makeUvSphere(3, 1, 1); });
    for (const auto radius : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        throws<std::invalid_argument>([radius] { crimson::makeUvSphere(3, 2, radius); });
}

void sphereIndexCapacity() {
    // Exactly 65,535 vertices fits the declared uint16_t mesh policy.
    const auto limit = crimson::makeUvSphere(254, 256, 1);
    require(limit.vertices.size() == 65535, "Valid capacity boundary rejected");
    for (const auto index : limit.indices) require(index < limit.vertices.size(), "Capacity-boundary index wrapped");
    throws<std::length_error>([] { crimson::makeUvSphere(255, 255, 1); });
    throws<std::length_error>([] { crimson::makeUvSphere(std::numeric_limits<uint32_t>::max(), 2, 1); });
    throws<std::length_error>([] { crimson::makeUvSphere(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 1); });
}

void cameraCoordinateContract() {
    crimson::Camera camera;
    near(camera.target[1], 1); near(camera.distance(), 4.2);
    camera.position = {0,1,4}; camera.target = {0,1,0};
    const auto eye = camera.eye(), forward = camera.forward(), right = camera.right();
    near(eye[0],0); near(eye[1],1); near(eye[2],4);
    near(forward[0],0); near(forward[1],0); near(forward[2],-1); near(right[0],1);
    near(camera.verticalFov(2),45); require(camera.verticalFov(.5f) > 45,"Narrow viewport must preserve its shorter-dimension FOV");
}

void gameCameraVerticalFraming() {
    using namespace crimson;
    Camera camera; camera.position={0,0,5}; camera.target={0,0,0}; camera.up={0,1,0};
    camera.fov=60; camera.fovAxis=CameraFovAxis::Vertical;
    const double expectedVertical=1/std::tan(std::numbers::pi_v<double>/6);
    for (const float aspect:{.5f,1.f,2.f}) {
        near(camera.verticalFov(aspect),60);
        for (const auto depth:{ClipDepth::ZeroToOne,ClipDepth::NegativeOneToOne}) {
            const auto projection=camera.projectionMatrix(aspect,depth);
            near(projection.at(1,1),expectedVertical);
            near(projection.at(0,0),expectedVertical/aspect);
            const float halfHeight=static_cast<float>(5*std::tan(std::numbers::pi_v<double>/6));
            const auto top=math::project({0,halfHeight,0},projection*camera.viewMatrix(),{20,30,600*aspect,600},depth);
            require(top.has_value(),"Game camera could not project its vertical frustum edge.");
            near((*top)[1],30,1e-4);
        }
        const ViewportRegion region{20,30,600*aspect,600,true,false};
        const auto ray=camera.screenRay(20+300*aspect,30,region);
        require(ray.has_value(),"Portrait/landscape Game camera lost its screen ray.");
        near(math::dot(ray->direction,camera.up)/math::dot(ray->direction,camera.forward()),
            std::tan(std::numbers::pi_v<double>/6),1e-5);
    }
    camera.fov=120; near(camera.verticalFov(.5f),120);
    camera.fov=std::numeric_limits<float>::quiet_NaN(); near(camera.verticalFov(.5f),45);
    camera.fovAxis=CameraFovAxis::ShorterDimension; camera.fov=60;
    require(camera.verticalFov(.5f)>60,"Game FOV policy changed the existing editor framing convention.");
}

void cameraOrbitAndZoomBounds() {
    crimson::Camera camera;
    const auto pivot = camera.target; const auto originalDistance = camera.distance();
    camera.orbit(40,0,pivot); require(camera.position[0] < 0,"Orbit drag-right must rotate eye left around the subject");
    const auto height = camera.position[1]; camera.orbit(0,20,pivot); require(camera.position[1] > height,"Orbit drag-down must raise the eye");
    for (int i=0;i<100;++i) camera.orbit(3,20,pivot);
    require(camera.up[1] >= -1e-5,"Orbit crossed into an upside-down orientation");
    near(camera.distance(),originalDistance,1e-4); near(length(camera.up[0],camera.up[1],camera.up[2]),1);
    camera.zoom(.5f); require(camera.distance() < originalDistance,"Positive wheel must zoom in");
    camera.zoom(-.5f); near(camera.distance(),originalDistance,1e-4);
    camera.position = {0,1,.051f}; camera.target = {0,1,0}; camera.up = {0,1,0};
    camera.zoom(1); const auto below = camera.distance(); require(below < .05,"Accepted detent should cross the soft minimum");
    camera.zoom(100); near(camera.distance(),below); camera.zoom(-1); near(camera.distance(),.051);
    camera.position = {0,1,499}; camera.zoom(-1); const auto above = camera.distance(); require(above > 500,"Accepted detent should cross the soft maximum");
    camera.zoom(-100); near(camera.distance(),above,1e-4); camera.zoom(1); near(camera.distance(),499,1e-4);
}

void cameraPanAndFly() {
    crimson::Camera camera; camera.position = {0,1,5}; camera.target = {0,1,0}; camera.up = {0,1,0};
    camera.pan(100,50,1000,500); require(camera.position[0] < 0 && camera.position[1] > 1,"Pan signs differ from drag-to-move-scene behavior");
    near(camera.position[0],camera.target[0]); near(camera.position[1],camera.target[1]); near(camera.distance(),5);
    const auto eye = camera.eye(); const auto distance = camera.distance();
    camera.flyLook(30,20); for (size_t i=0;i<3;++i) near(camera.eye()[i],eye[i]);
    require(camera.forward()[0] > 0 && camera.forward()[1] < 0,"Fly look signs must turn right/down with the pointer"); near(camera.distance(),distance);
    const auto before = camera.eye(); camera.flyMove(1,1,.1f,25);
    near(length(camera.eye()[0]-before[0],camera.eye()[1]-before[1],camera.eye()[2]-before[2]),2.5,1e-4);
    near(camera.distance(),distance);
    const auto beforeClamp = camera.eye(); camera.flyMove(1,0,1,25);
    near(length(camera.eye()[0]-beforeClamp[0],camera.eye()[1]-beforeClamp[1],camera.eye()[2]-beforeClamp[2]),6.25,1e-4);
}

void cameraNonfiniteInput() {
    const auto invalid = std::numeric_limits<float>::quiet_NaN();
    crimson::Camera camera; const auto original = camera;
    camera.orbit(invalid,1,camera.target); camera.flyLook(1,std::numeric_limits<float>::infinity()); camera.zoom(invalid); camera.pan(1,1,invalid,2); camera.flyMove(1,1,invalid,25);
    for (size_t i=0;i<3;++i) near(camera.eye()[i],original.eye()[i]);
    camera.position = {invalid,invalid,invalid}; camera.target = {invalid,0,0}; camera.up = {invalid,invalid,invalid};
    const auto eye = camera.eye();
    require(std::isfinite(eye[0]) && std::isfinite(eye[1]) && std::isfinite(eye[2]), "Corrupt stored camera data produced a nonfinite eye");
    camera.flyLook(1,1); const auto f = camera.forward(); require(std::isfinite(f[0]) && std::isfinite(f[1]) && std::isfinite(f[2]),"Camera did not repair its invalid basis"); near(length(f[0],f[1],f[2]),1);
}

SDL_Event button(uint32_t type,uint8_t which,float x,float y,SDL_WindowID windowId=0) { SDL_Event e{}; e.type=type; e.button.windowID=windowId; e.button.button=which; e.button.x=x; e.button.y=y; return e; }
SDL_Event motion(float x,float y,float dx=0,float dy=0,SDL_WindowID windowId=0) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.windowID=windowId; e.motion.x=x; e.motion.y=y; e.motion.xrel=dx; e.motion.yrel=dy; return e; }
SDL_Event key(uint32_t type,SDL_Scancode code,SDL_WindowID windowId=0) { SDL_Event e{}; e.type=type; e.key.windowID=windowId; e.key.scancode=code; return e; }
SDL_Event wheel(float steps,SDL_WindowID windowId=0) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_WHEEL; e.wheel.windowID=windowId; e.wheel.y=steps; return e; }
SDL_Event windowEvent(uint32_t type,SDL_WindowID windowId) { SDL_Event e{}; e.type=type; e.window.windowID=windowId; return e; }
crimson::ViewportRegion viewport() { return {0,0,800,600,true,false}; }
void same(const crimson::Vec3& a,const crimson::Vec3& b) { for (size_t i=0;i<3;++i) near(a[i],b[i]); }

void navigationOwnershipAndAnchor() {
    crimson::Camera camera; camera.position={0,1,5}; camera.target={0,1,0}; const auto initial=camera;
    crimson::Navigation nav(nullptr); auto region=viewport(); region.inputBlocked=true;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.1f); require(!nav.active(),"Blocked UI acquired navigation"); same(camera.eye(),initial.eye());
    region.inputBlocked=false;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,900,300)); nav.update(camera,region,.1f); require(!nav.active(),"Outside-image press acquired navigation");
    region.hovered=false;
    for (const auto mouseButton : std::array<std::uint8_t,2>{SDL_BUTTON_MIDDLE,SDL_BUTTON_RIGHT}) {
        nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,mouseButton,400,300)); nav.update(camera,region,.1f);
        require(!nav.active(),"Occluded viewport acquired navigation from a geometric hit.");
        same(camera.eye(),initial.eye()); same(camera.target,initial.target);
    }
    region.hovered=true;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.1f); require(nav.active(),"Viewport press failed to acquire orbit"); same(camera.eye(),initial.eye()); same(camera.target,initial.target);
    nav.handleEvent(motion(410,300)); nav.update(camera,region,.01f); near(camera.distance(),4,1e-4); require(camera.position[0] < 0,"Sphere anchor was not used on the first orbit motion");
    region.hovered=false; nav.handleEvent(motion(850,300)); nav.update(camera,region,.01f); require(nav.active(),"Leaving viewport ended an owned gesture");
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,850,300)); nav.update(camera,region,.01f); require(!nav.active(),"MMB release failed to end orbit");
    const auto released=camera.eye(); nav.handleEvent(motion(900,300)); nav.update(camera,region,.01f); same(camera.eye(),released);
}
void navigationPanAndShortEdges() {
    crimson::Camera camera; camera.position={0,1,5}; camera.target={0,1,0}; crimson::Navigation nav(nullptr); auto region=viewport();
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_LSHIFT)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,10,10)); nav.update(camera,region,.01f);
    const auto f=camera.forward(); nav.handleEvent(key(SDL_EVENT_KEY_UP,SDL_SCANCODE_LSHIFT)); nav.handleEvent(motion(30,25)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,35,25)); nav.handleEvent(motion(500,300)); nav.update(camera,region,.01f);
    require(!nav.active(),"Release within a frame was lost"); same(camera.forward(),f); require(camera.position[0] < 0 && camera.position[1] > 1,"Latched Shift pan changed mode after modifier release"); near(camera.distance(),5);
    const auto before=camera; nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,.01f); same(camera.eye(),before.eye()); same(camera.target,before.target);
}
void modelPreviewFramingAndNavigation() {
    crimson::Camera camera;
    for (const float aspect : {.4f, 1.0f, 2.0f}) {
        const crimson::Vec3 minimum{-4,-2,-1}, maximum{8,6,3};
        require(camera.frameBounds(minimum,maximum,aspect),"Model bounds could not be framed");
        same(camera.target,{2,2,1});
        const float radius = std::sqrt(56.0f);
        const float vertical = camera.verticalFov(aspect)*std::numbers::pi_v<float>/360;
        const float horizontal = std::atan(std::tan(vertical)*aspect);
        require(camera.distance()*std::sin(std::min(vertical,horizontal)) > radius,"Bounds clipped by framed viewport");
        require(camera.nearPlane < camera.distance()-radius && camera.farPlane > camera.distance()+radius,"Bounds clipped by depth range");
    }
    const auto before = camera;
    require(!camera.frameBounds({1,0,0},{-1,0,0},1),"Invalid bounds accepted");
    same(camera.eye(),before.eye());
    camera.position={0,1,5}; camera.target={0,1,0};
    crimson::Navigation nav(nullptr); auto region=viewport();
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300));
    nav.handleEvent(motion(410,300));
    nav.update(camera,region,.01f,false);
    near(camera.distance(),5,1e-4); same(camera.target,{0,1,0});
    nav.cancel();
}
void navigationFlySpeedAndMovement() {
    crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); const auto initial=camera;
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W)); nav.update(camera,region,.1f); same(camera.eye(),initial.eye());
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,400,300)); nav.handleEvent(motion(440,340,40,40)); nav.update(camera,region,.1f); require(nav.flying(),"RMB failed to acquire fly"); same(camera.eye(),initial.eye()); same(camera.target,initial.target);
    nav.handleEvent(motion(430,310,10,10)); nav.update(camera,region,.1f); near(length(camera.eye()[0]-initial.eye()[0],camera.eye()[1]-initial.eye()[1],camera.eye()[2]-initial.eye()[2]),2.5,1e-4);
    nav.handleEvent(wheel(25)); nav.update(camera,region,0); near(nav.flySpeed(),50);
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,0); require(nav.flying(),"MMB speed reset ended fly"); near(nav.flySpeed(),25);
    nav.handleEvent(wheel(-10000)); nav.update(camera,region,0); near(nav.flySpeed(),1); nav.handleEvent(wheel(10000)); nav.update(camera,region,0); near(nav.flySpeed(),1000);
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,400,300)); nav.update(camera,region,.1f); require(!nav.active(),"RMB release did not end fly"); const auto finalEye=camera.eye(); nav.update(camera,region,.1f); same(camera.eye(),finalEye);
}
void navigationCancellationAndWheel() {
    for (const bool escape : {false,true}) {
        crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,10,10)); nav.update(camera,region,.01f); const auto before=camera;
        nav.handleEvent(motion(60,20)); SDL_Event canceled{}; canceled.type=SDL_EVENT_WINDOW_FOCUS_LOST; nav.handleEvent(escape ? key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_ESCAPE) : canceled); nav.handleEvent(motion(90,50)); nav.update(camera,region,.1f); require(!nav.active(),"Cancellation retained gesture ownership"); same(camera.eye(),before.eye()); same(camera.target,before.target);
    }
    crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); const auto d=camera.distance(); region.hovered=false; nav.handleEvent(wheel(1)); nav.update(camera,region,.01f); near(camera.distance(),d);
    region.hovered=true; nav.handleEvent(wheel(1)); nav.update(camera,region,.01f); near(camera.distance(),d/1.18); nav.handleEvent(wheel(-1)); nav.update(camera,region,.01f); near(camera.distance(),d);
}

void collapsedModelTransforms() {
    using namespace crimson;
    const auto rotation=math::eulerRotationXYZ({.4f,-.6f,.8f});
    const auto node=math::translation({1,2,3})*math::eulerRotationXYZ({-.7f,.2f,.5f});
    const auto original=math::translation({2,-1,4})*rotation*math::scaling({-2,3,4});
    Mat4 world,normal;float handedness{};
    for(std::size_t axis=0;axis<3;++axis) {
        Vec3 scale{-2,3,4};scale[axis]=0;
        const auto collapsed=math::translation({2,-1,4})*rotation*math::scaling(scale);
        require(prepareModelMatrices(collapsed,node,world,normal,handedness)==ModelTransformStatus::Collapsed,
            "A zero scale was confused with invalid data after an imported node rotation.");
        require(prepareModelMatrices(original,node,world,normal,handedness)==ModelTransformStatus::Ready,
            "Restoring a model after zero scale did not restore its prepared draw.");
    }
    require(prepareModelMatrices(original,math::scaling({1,0,1}),world,normal,handedness)==ModelTransformStatus::Collapsed,
        "A collapsed imported node was not classified as an intentional hidden draw.");
    auto invalid=original;invalid[0]=std::numeric_limits<float>::quiet_NaN();
    require(prepareModelMatrices(invalid,node,world,normal,handedness)==ModelTransformStatus::Invalid,
        "Nonfinite model data was classified as an ordinary zero scale.");
    invalid=Mat4{};invalid[3]=1;
    require(prepareModelMatrices(original,invalid,world,normal,handedness)==ModelTransformStatus::Invalid,
        "A non-affine imported transform was accepted.");
}

void navigationForeignWindowEvents() {
    crimson::Camera camera; crimson::Navigation nav(nullptr); auto region=viewport(); const auto initial=camera;
    constexpr SDL_WindowID foreign=17;
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300,foreign));
    nav.handleEvent(wheel(2,foreign)); nav.update(camera,region,.1f,false);
    require(!nav.active(),"Another native window acquired scene navigation."); same(camera.eye(),initial.eye());
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT,400,300)); nav.update(camera,region,.1f,false);
    require(nav.flying(),"Windowless own event failed to acquire navigation.");
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W,foreign));
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_ESCAPE,foreign));
    nav.handleEvent(motion(600,500,200,200,foreign)); nav.handleEvent(wheel(30,foreign));
    nav.handleEvent(windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST,foreign));
    nav.handleEvent(windowEvent(SDL_EVENT_WINDOW_HIDDEN,foreign)); nav.update(camera,region,.1f,false);
    require(nav.flying(),"Another native window canceled an owned gesture.");
    same(camera.eye(),initial.eye()); same(camera.target,initial.target); near(nav.flySpeed(),25);
    nav.handleEvent(key(SDL_EVENT_KEY_DOWN,SDL_SCANCODE_W)); nav.update(camera,region,.1f,false);
    const auto moved=camera.eye(); near(crimson::math::length(crimson::math::subtract(moved,initial.eye())),2.5,1e-4);
    nav.handleEvent(key(SDL_EVENT_KEY_UP,SDL_SCANCODE_W,foreign));
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,400,300,foreign)); nav.update(camera,region,.1f,false);
    require(nav.flying(),"Another native window released an owned mouse button.");
    near(crimson::math::length(crimson::math::subtract(camera.eye(),moved)),2.5,1e-4);
    nav.handleEvent(key(SDL_EVENT_KEY_UP,SDL_SCANCODE_W));
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT,400,300)); nav.update(camera,region,0,false);
    require(!nav.active(),"Own release failed after foreign events.");
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,400,300)); nav.update(camera,region,0,false);
    require(nav.active(),"Own orbit failed after foreign events."); nav.setDesktopCoordinates(true);
    require(!nav.active(),"Coordinate-space switch retained an old gesture.");
}

void navigationDesktopCoordinatesAndWindowLifetime() {
    require(SDL_InitSubSystem(SDL_INIT_VIDEO),"Hidden-window navigation check could not initialize SDL video.");
    struct VideoLifetime { ~VideoLifetime() { SDL_QuitSubSystem(SDL_INIT_VIDEO); } } video;
    using Window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>;
    Window main(SDL_CreateWindow("Navigation hidden owner",800,600,SDL_WINDOW_HIDDEN),&SDL_DestroyWindow);
    Window peer(SDL_CreateWindow("Navigation hidden peer",800,600,SDL_WINDOW_HIDDEN),&SDL_DestroyWindow);
    require(main && peer,"Hidden-window navigation check could not create SDL windows.");
    require(SDL_SetWindowPosition(main.get(),1200,900),"Hidden owner position could not be set.");
    require(SDL_SetWindowPosition(peer.get(),-1200,-900),"Hidden peer position could not be set.");
    int mainX{},mainY{},peerX{},peerY{};
    require(SDL_GetWindowPosition(main.get(),&mainX,&mainY) && SDL_GetWindowPosition(peer.get(),&peerX,&peerY),"Hidden window positions could not be read.");
    const auto mainId=SDL_GetWindowID(main.get()),peerId=SDL_GetWindowID(peer.get());
    crimson::Camera camera; crimson::Navigation nav(main.get()); nav.setDesktopCoordinates(true);
    auto region=viewport(); region.x=static_cast<float>(mainX); region.y=static_cast<float>(mainY); region.windowId=mainId;
    // Local (20,20) must hit the image at its desktop origin, including negative origins.
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,20,20,mainId)); nav.update(camera,region,0,false);
    require(nav.active(),"SDL window-local press missed a desktop-space viewport.");
    nav.handleEvent(motion(40,30,20,10,mainId)); nav.update(camera,region,0,false); const auto moved=camera;
    nav.handleEvent(windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST,peerId));
    nav.handleEvent(motion(300,300,200,200,peerId)); nav.update(camera,region,0,false);
    require(nav.active(),"Peer focus change canceled native owner navigation."); same(camera.eye(),moved.eye()); same(camera.target,moved.target);
    region.x=static_cast<float>(peerX); region.y=static_cast<float>(peerY); region.windowId=peerId;
    nav.update(camera,region,0,false); require(!nav.active(),"Viewport window switch retained an old gesture.");
    nav.handleEvent(button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_MIDDLE,20,20,peerId)); nav.update(camera,region,0,false);
    require(nav.active(),"Navigation failed to acquire the replacement desktop viewport.");
    peer.reset(); nav.update(camera,region,0,false);
    require(!nav.active(),"Destroyed SDL viewport retained capture ownership."); same(camera.eye(),moved.eye());
}

void particlePresentationValidation() {
    crimson::RenderParticle particle;
    require(crimson::particle_rendering::valid(particle),"Starter particle should be renderable.");
    require(particle.billboardMode==crimson::ParticleBillboardMode::Disabled,"New render packets must not opt into billboarding.");
    for(const auto mode:{crimson::ParticleBillboardMode::Disabled,crimson::ParticleBillboardMode::Enabled,crimson::ParticleBillboardMode::FixedY}) {
        particle.billboardMode=mode;particle.right={0,0,-1};particle.up={0,1,0};
        require(crimson::particle_rendering::valid(particle),"Valid oriented particle basis was rejected.");
    }
    particle.billboardMode=static_cast<crimson::ParticleBillboardMode>(3);
    require(!crimson::particle_rendering::valid(particle),"Unsupported billboard mode reached instance upload.");
    particle.billboardMode=crimson::ParticleBillboardMode::Disabled;particle.right={};
    require(!crimson::particle_rendering::valid(particle),"Collapsed particle basis reached instance upload.");
    particle.right={1,0,0};particle.up={1,0,0};
    require(!crimson::particle_rendering::valid(particle),"Parallel particle basis reached instance upload.");
    particle.up={0,2,0};
    require(!crimson::particle_rendering::valid(particle),"Scaled particle basis changed world-unit sprite size.");
    particle.up={0,1,0};particle.right[0]=std::numeric_limits<float>::quiet_NaN();
    require(!crimson::particle_rendering::valid(particle),"Nonfinite particle orientation reached instance upload.");
    particle.right={1,0,0};
    particle.color={crimson::MaxParticleRadiance,1,0,.25f};
    require(crimson::particle_rendering::valid(particle),"HDR radiance is independent of opacity.");
    particle.color[0]=std::numeric_limits<float>::infinity();
    require(!crimson::particle_rendering::valid(particle),"Nonfinite particle radiance must be rejected before upload.");
    particle.color={1,1,1,1}; particle.position[1]=std::numeric_limits<float>::quiet_NaN();
    require(!crimson::particle_rendering::valid(particle),"Nonfinite particle position must be rejected before upload.");
    particle.position={0,0,0}; particle.size=0;
    require(!crimson::particle_rendering::valid(particle),"Zero-width particle should be rejected.");
    particle.size=.08f; particle.color[3]=2;
    require(!crimson::particle_rendering::valid(particle),"Opacity outside the covered-fraction contract should be rejected.");
    particle.color[3]=1;particle.rotation=std::numbers::pi_v<float>;
    require(crimson::particle_rendering::valid(particle),"Finite billboard rotation should be renderable.");
    particle.rotation=std::numeric_limits<float>::quiet_NaN();
    require(!crimson::particle_rendering::valid(particle),"Nonfinite billboard rotation reached instance upload.");
}

void particleHdrResolveContract() {
    const auto resolve=[](double radiance,double coverage,double exposure,double matte) {
        const auto hdr=std::max(0.0,radiance)*exposure;
        return std::clamp(1-1/(1+hdr)+(1-std::clamp(coverage,0.0,1.0))*matte,0.0,1.0);
    };
    const auto encode=[](double linear) { return linear<=.0031308 ? linear*12.92 : 1.055*std::pow(linear,1/2.4)-.055; };
    for (const float background:{0.f,21.f/255,.5f,.999f,1.f}) {
        const auto matte=crimson::particle_rendering::backgroundLinear({background,background,background});
        require(crimson::math::finite(matte),"Editor display matte must remain finite at white.");
        for (const auto exposure:{0.,1.,100.}) {
            const auto empty=resolve(0,0,exposure,matte[0]);
            near(encode(empty),background,2e-6);
            near(resolve(3,1,exposure,matte[0]),3*exposure/(1+3*exposure),1e-8);
            near(resolve(0,.5,exposure,matte[0]),.5*matte[0],1e-8);
            near(resolve(0,.99,exposure,matte[0]),.01*matte[0],1e-8);
        }
        const auto uncovered=resolve(0,0,1,matte[0]);
        if(background<1)require(resolve(.25,0,1,matte[0])>uncovered,"Additive emission must remain visible until the display matte saturates.");
        near(resolve(.25,0,0,matte[0]),uncovered,1e-8);
        const double overlap=double(crimson::MaxParticleRadiance)*crimson::MaxParticleDrawCount;
        const auto bright=resolve(overlap,0,100,matte[0]);
        require(std::isfinite(bright) && bright>=0 && bright<=1,"Supported additive overlap must resolve to finite display color.");
    }
}

void particleGlobalOrdering() {
    using namespace crimson;
    const Vec3 eye{0,0,3},forward{0,0,-1};
    const std::array<RenderParticle,5> particles{{
        {{0,0,.5f},1,{0,0,0,.5f},0},{{0,0,-.5f},1,{1,0,0,1},1},
        {{0,0,.5f},1,{0,0,1,.5f},2},{{0,0,-1},1,{1,1,1,1},1},
        {{100,0,1},1,{1,1,1,1},0}}};
    std::vector<particle_rendering::SortKey> keys;
    for(std::size_t i=0;i<particles.size();++i)keys.push_back({i,particle_rendering::viewDepth(particles[i].position,eye,forward)});
    particle_rendering::sortBackToFront(keys);
    require(keys[0].index==3 && keys[1].index==1 && keys[2].index==0 && keys[3].index==2 && keys[4].index==4,
        "Transparency ordering grouped by material, used radial distance, or lost stable center-depth ties.");
    std::vector<particle_rendering::TransparentSortKey> mixed{{0,4,true},{1,2,true},{0,5,false},{1,3,false},{2,2,false},{3,1,false}};
    particle_rendering::sortTransparentBackToFront(mixed);
    require(!mixed[0].mesh && mixed[1].mesh && !mixed[2].mesh && mixed[3].mesh && !mixed[4].mesh,
        "Transparent meshes and particles did not interleave or lost stable ties.");
    require(mixed[3].depth==mixed[4].depth && mixed[3].index==1 && mixed[4].index==2,
        "Transparent equal-depth ties did not preserve extraction order.");
    // Looking in the opposite direction must reverse depth priority, while
    // retaining the two equal-depth particles' extraction order.
    for(auto& key:keys)key.depth=particle_rendering::viewDepth(particles[key.index].position,{},Vec3{0,0,1});
    particle_rendering::sortBackToFront(keys);
    require(keys[0].index==4 && keys[1].index==0 && keys[2].index==2 && keys[3].index==1 && keys[4].index==3,
        "Particle ordering did not follow camera forward or stable equal-depth input order.");
}

} // namespace

int main() {
    int failures = 0;
    const auto run = [&failures](const char* name, auto function) {
        try { function(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        catch (...) { ++failures; std::cerr << "FAIL " << name << ": unknown exception\n"; }
    };
    run("sphere counts, finite positions and outward unit normals", sphereCountsAndNormals);
    run("sphere outward winding and nondegenerate pole topology", sphereWinding);
    run("sphere duplicated UV seam and pole positions", sphereSeamAndPoles);
    run("sphere tangent handedness and +U/+V orientation through seams and poles", sphereTangentOrientation);
    run("sphere input validation", sphereRejectsInvalidInput);
    run("sphere uint16 index capacity and overflow rejection", sphereIndexCapacity);
    run("camera Y-up coordinate contract", cameraCoordinateContract);
    run("point-light radius bounds inverse-square influence with smooth finite falloff", pointLightRadiusContract);
    run("player-world near falloff bounds wall hotspots without a hard ring or ordinary-light change", pointLightNearFalloffContract);
    run("finite point profiles retain quartic defaults, bounded falloff and angular source broadening", pointLightProfileContract);
    run("Glow Gaussian retains energy and width with an HDR-invisible support edge", glowGaussianKernelContract);
    run("Glow isolated HDR impulse contours stay circular through bilinear upsampling", glowGaussianContours);
    run("grid depth survives eye-crossing clips and retains foreground occlusion", gridDepthSurvivesEyeCrossing);
    run("Game vertical FOV preserves projection and rays across window aspects", gameCameraVerticalFraming);
    run("camera orbit signs, upright lock and reversible soft zoom", cameraOrbitAndZoomBounds);
    run("camera pan, fly look and normalized time-based movement", cameraPanAndFly);
    run("camera finite-input handling and stored-state fallback", cameraNonfiniteInput);
    run("navigation viewport ownership, lazy sphere anchor and release", navigationOwnershipAndAnchor);
    run("navigation latched pan and same-frame pointer edges", navigationPanAndShortEdges);
    run("navigation fly speed, relative start and keyboard gating", navigationFlySpeedAndMovement);
    run("navigation cancellation precedes motion and wheel is viewport-scoped", navigationCancellationAndWheel);
    run("navigation ignores foreign native window input and cancellation", navigationForeignWindowEvents);
    run("navigation translates desktop coordinates and releases destroyed window ownership", navigationDesktopCoordinatesAndWindowLifetime);
    run("model framing preserves bounds and navigation ignores hidden sphere", modelPreviewFramingAndNavigation);
    run("collapsed model transforms are legal and recover without invalid-data diagnostics", collapsedModelTransforms);
    run("particle presentation rejects nonfinite and unsupported upload inputs", particlePresentationValidation);
    run("particle HDR resolve preserves background and additive emission", particleHdrResolveContract);
    run("particle global center-depth ordering preserves material interleaving and stable ties", particleGlobalOrdering);
    if (failures) std::cerr << failures << " renderer-core check(s) failed.\n";
    else std::cout << "All renderer-core checks passed.\n";
    return failures ? 1 : 0;
}
