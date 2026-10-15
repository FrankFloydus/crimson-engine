#include "ClusteredLighting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>

namespace crimson::clustered_lighting {
namespace {
constexpr std::uint32_t InvalidIndex=std::numeric_limits<std::uint32_t>::max();

std::uint32_t tileCoordinate(double pixel,std::uint32_t size,std::uint32_t count) {
    if(!(pixel>0))return 0;
    const double tile=std::floor(pixel/size);
    return tile>=count ? count-1 : static_cast<std::uint32_t>(tile);
}

std::uint32_t sliceCoordinate(double depth,float scale,float bias,std::uint32_t count) {
    if(!(depth>0))return 0;
    const double slice=std::floor(std::log2(depth)*scale+bias);
    if(!(slice>0))return 0;
    return slice>=count ? count-1 : static_cast<std::uint32_t>(slice);
}
}

std::uint32_t Grid::clusterIndex(std::uint32_t x,std::uint32_t y,std::uint32_t z) const noexcept {
    if(!ready() || x>=tilesX || y>=tilesY || z>=depthSlices)return InvalidIndex;
    return (z*tilesY+y)*tilesX+x;
}

std::uint32_t Grid::depthSlice(float depth) const noexcept {
    return ready() ? sliceCoordinate(depth,depthScale,depthBias,depthSlices) : 0;
}

std::uint32_t Grid::clusterForPixelDepth(float x,float y,float depth) const noexcept {
    if(!ready() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth))return InvalidIndex;
    return clusterIndex(tileCoordinate(x,tileSize,tilesX),tileCoordinate(y,tileSize,tilesY),depthSlice(depth));
}

std::span<const std::uint32_t> Grid::lightsForCluster(std::uint32_t cluster) const noexcept {
    if(!ready() || cluster>=headers.size())return {};
    const auto& header=headers[cluster];
    return std::span<const std::uint32_t>(indices).subspan(header.offset,header.count);
}

const char* statusText(Status status) noexcept {
    switch(status) {
    case Status::Ready:return "Ready";
    case Status::InvalidConfiguration:return "Invalid clustered-light configuration";
    case Status::LightCapacityExceeded:return "Clustered-light source capacity exceeded";
    case Status::ClusterCapacityExceeded:return "Cluster grid capacity exceeded";
    case Status::IndexCapacityExceeded:return "Clustered-light index capacity exceeded";
    case Status::AllocationFailed:return "Clustered-light allocation failed";
    }
    return "Unknown clustered-light status";
}

bool build(Grid& grid,const Camera& camera,std::uint32_t width,std::uint32_t height,
           std::span<const PointLight> lights,const Config& config) {
    grid.headers.clear();grid.indices.clear();grid.visibleLightIndices.clear();grid.bounds_.clear();
    grid.stats={};grid.width=width;grid.height=height;grid.tileSize=config.tileSize;
    grid.tilesX=0;grid.tilesY=0;grid.depthSlices=config.depthSlices;
    grid.nearPlane=0;grid.farPlane=0;grid.depthScale=0;grid.depthBias=0;
    grid.status=Status::InvalidConfiguration;
    auto fail=[&](Status status) {
        grid.headers.clear();grid.indices.clear();grid.visibleLightIndices.clear();
        grid.status=status;return false;
    };
    if(width==0 || height==0 || config.tileSize==0 || config.depthSlices==0 || config.maxClusters==0)return false;
    if(lights.size()>config.maxLights || lights.size()>std::numeric_limits<std::uint32_t>::max())
        return fail(Status::LightCapacityExceeded);
    grid.stats.inputLights=static_cast<std::uint32_t>(lights.size());
    grid.tilesX=1+(width-1)/config.tileSize;grid.tilesY=1+(height-1)/config.tileSize;
    const std::uint64_t tileCount=std::uint64_t(grid.tilesX)*grid.tilesY;
    if(tileCount>config.maxClusters || config.depthSlices>config.maxClusters/tileCount)
        return fail(Status::ClusterCapacityExceeded);
    const auto clusterCount=static_cast<std::uint32_t>(tileCount*config.depthSlices);

    // Match Camera::projectionMatrix's near/far preparation exactly. FOV comes
    // from the same shorter-axis/vertical policy as the visible projection.
    grid.nearPlane=std::isfinite(camera.nearPlane)?std::clamp(camera.nearPlane,1e-7f,1e8f):.01f;
    grid.farPlane=std::max(grid.nearPlane*2,std::isfinite(camera.farPlane)?std::clamp(camera.farPlane,1e-6f,1e12f):2048.f);
    const double aspect=double(width)/height;
    const double tanY=std::tan(double(camera.verticalFov(static_cast<float>(aspect)))*std::numbers::pi/360);
    const double tanX=tanY*aspect;
    const double logRange=std::log2(double(grid.farPlane)/grid.nearPlane);
    grid.depthScale=static_cast<float>(config.depthSlices/logRange);
    grid.depthBias=static_cast<float>(-std::log2(grid.nearPlane)*grid.depthScale);
    if(!(tanX>0 && tanY>0) || !std::isfinite(tanX) || !std::isfinite(tanY) ||
        !std::isfinite(grid.depthScale) || !std::isfinite(grid.depthBias))return false;

    try {
        grid.headers.resize(clusterCount);
        grid.columns_.resize(grid.tilesX);grid.rows_.resize(grid.tilesY);
        grid.depths_.resize(std::size_t(config.depthSlices)+1);
        grid.cursors_.resize(clusterCount);
        for(std::uint32_t x=0;x<grid.tilesX;++x) {
            const double begin=double(x)*config.tileSize;
            const double end=std::min(double(width),begin+config.tileSize);
            const double low=(2*begin/width-1)*tanX,high=(2*end/width-1)*tanX;
            grid.columns_[x]={low,high,std::hypot(1.,low),std::hypot(1.,high)};
        }
        for(std::uint32_t y=0;y<grid.tilesY;++y) {
            const double begin=double(y)*config.tileSize;
            const double end=std::min(double(height),begin+config.tileSize);
            const double low=(1-2*end/height)*tanY,high=(1-2*begin/height)*tanY;
            grid.rows_[y]={low,high,std::hypot(1.,low),std::hypot(1.,high)};
        }
        grid.depths_.front()=grid.nearPlane;grid.depths_.back()=grid.farPlane;
        // Reconstruct the planes from the published float mapping, rather than
        // independently computing another set of logarithmic boundaries.
        for(std::uint32_t z=1;z<config.depthSlices;++z)
            grid.depths_[z]=std::exp2((double(z)-grid.depthBias)/grid.depthScale);

        const auto view=camera.viewMatrix();
        for(std::uint32_t source=0;source<lights.size();++source) {
            const auto& light=lights[source];
            if(!light.enabled || !std::isfinite(light.intensity) || light.intensity<=0 || !math::finite(light.position))continue;
            ++grid.stats.activeLights;
            const auto position=math::transformPoint(view,light.position);
            if(!math::finite(position))continue;
            const double radius=std::isfinite(light.radius)?std::clamp(light.radius,.01f,100000.f):20.f;
            const double x=position[0],y=position[1],z=-double(position[2]);
            // Inflate for the float camera/fragment transforms and tile/slice
            // boundary rounding. Extra references are safe; missing ones are not.
            const double padding=std::max({1e-5,std::abs(x),std::abs(y),std::abs(z),radius})*
                (16*std::numeric_limits<float>::epsilon())+1e-5;
            const double r=radius+padding;
            if(z+r<grid.nearPlane || z-r>grid.farPlane)continue;
            if(x+tanX*z < -r*std::hypot(1.,tanX) || tanX*z-x < -r*std::hypot(1.,tanX) ||
               y+tanY*z < -r*std::hypot(1.,tanY) || tanY*z-y < -r*std::hypot(1.,tanY))continue;
            const double minDepth=std::max(double(grid.nearPlane),z-r);
            const double maxDepth=std::min(double(grid.farPlane),z+r);
            const double minSlopeX=std::min((x-r)/minDepth,(x-r)/maxDepth);
            const double maxSlopeX=std::max((x+r)/minDepth,(x+r)/maxDepth);
            const double minSlopeY=std::min((y-r)/minDepth,(y-r)/maxDepth);
            const double maxSlopeY=std::max((y+r)/minDepth,(y+r)/maxDepth);
            Grid::Bounds bounds{x,y,z,r,source,
                tileCoordinate((minSlopeX/tanX+1)*.5*width,config.tileSize,grid.tilesX),
                tileCoordinate((maxSlopeX/tanX+1)*.5*width,config.tileSize,grid.tilesX),
                tileCoordinate((1-maxSlopeY/tanY)*.5*height,config.tileSize,grid.tilesY),
                tileCoordinate((1-minSlopeY/tanY)*.5*height,config.tileSize,grid.tilesY),
                sliceCoordinate(minDepth,grid.depthScale,grid.depthBias,grid.depthSlices),
                sliceCoordinate(maxDepth,grid.depthScale,grid.depthBias,grid.depthSlices)};
            // Candidate bounds deliberately overlap adjacent cells at float
            // boundaries; normalized plane tests remove the distant extras.
            if(bounds.minX>0)--bounds.minX;if(bounds.maxX+1<grid.tilesX)++bounds.maxX;
            if(bounds.minY>0)--bounds.minY;if(bounds.maxY+1<grid.tilesY)++bounds.maxY;
            if(bounds.minZ>0)--bounds.minZ;if(bounds.maxZ+1<grid.depthSlices)++bounds.maxZ;
            grid.bounds_.push_back(bounds);grid.visibleLightIndices.push_back(source);
        }
        grid.stats.visibleLights=static_cast<std::uint32_t>(grid.bounds_.size());

        auto visit=[&](const Grid::Bounds& bounds,auto&& append) {
            for(std::uint32_t z=bounds.minZ;z<=bounds.maxZ;++z) {
                if(bounds.z+bounds.radius<grid.depths_[z] || bounds.z-bounds.radius>grid.depths_[z+1])continue;
                for(std::uint32_t y=bounds.minY;y<=bounds.maxY;++y) {
                    const auto& row=grid.rows_[y];
                    if(bounds.y-row.low*bounds.z < -bounds.radius*row.lowLength ||
                       row.high*bounds.z-bounds.y < -bounds.radius*row.highLength)continue;
                    for(std::uint32_t x=bounds.minX;x<=bounds.maxX;++x) {
                        ++grid.stats.candidateTests;
                        const auto& column=grid.columns_[x];
                        if(bounds.x-column.low*bounds.z < -bounds.radius*column.lowLength ||
                           column.high*bounds.z-bounds.x < -bounds.radius*column.highLength)continue;
                        if(!append((z*grid.tilesY+y)*grid.tilesX+x,bounds.source))return false;
                    }
                }
            }
            return true;
        };
        // Count first, then publish a compact CSR list. The order inside each
        // list is source order, independent of retained allocation capacities.
        for(const auto& bounds:grid.bounds_) {
            if(!visit(bounds,[&](std::uint32_t cluster,std::uint32_t) {
                ++grid.stats.lightReferences;
                if(grid.stats.lightReferences>config.maxIndices)return false;
                ++grid.headers[cluster].count;return true;
            }))return fail(Status::IndexCapacityExceeded);
        }
        std::uint32_t offset=0;
        for(std::size_t i=0;i<grid.headers.size();++i) {
            auto& header=grid.headers[i];header.offset=offset;grid.cursors_[i]=offset;offset+=header.count;
            if(header.count>0)++grid.stats.occupiedClusters;
            grid.stats.maxLightsPerCluster=std::max(grid.stats.maxLightsPerCluster,header.count);
        }
        grid.indices.resize(offset);
        for(const auto& bounds:grid.bounds_)visit(bounds,[&](std::uint32_t cluster,std::uint32_t source) {
            grid.indices[grid.cursors_[cluster]++]=source;return true;
        });
        grid.status=Status::Ready;return true;
    } catch(const std::bad_alloc&) { return fail(Status::AllocationFailed); }
      catch(const std::length_error&) { return fail(Status::AllocationFailed); }
}

} // namespace crimson::clustered_lighting
