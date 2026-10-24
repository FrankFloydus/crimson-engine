#include "QuaderPaintMigration.h"
#include "resources/StreamingModel.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace crimson {
namespace {
void canceled(std::stop_token stop) {if(stop.stop_requested())throw std::runtime_error("Quader paint migration canceled.");}
[[noreturn]] void fail(const std::string& text) {throw std::runtime_error("Quader paint migration: "+text);}
class PaintMigrationBudgetError final:public std::runtime_error {
public:explicit PaintMigrationBudgetError(const std::string& text):std::runtime_error("Quader paint migration: "+text){}
};
std::shared_ptr<const MeshPaintGeometry> targetGeometry(const ModelPrimitive& source,std::uint32_t level,std::stop_token stop) {
    try {return buildMeshPaintGeometry(source,level,stop);}
    catch(const std::runtime_error& error) {
        const std::string_view message=error.what();
        if(message=="Subdivision exceeds the paint geometry budget."||message=="Subdivision exceeds the paint triangle budget."||message=="Subdivision exceeds the paint vertex budget.")
            throw PaintMigrationBudgetError(std::string{message});
        throw;
    }
}
using Weights=std::array<double,4>;
Weights weights(const MeshPaintPayload& paint,std::uint32_t vertex) {
    const auto rgb=paint.rgb(vertex);return {double(255-rgb[0]-rgb[1]-rgb[2])/255.,double(rgb[0])/255.,double(rgb[1])/255.,double(rgb[2])/255.};
}
double difference(const Weights& a,const Weights& b) {double result=0;for(unsigned i=0;i<4;++i)result=std::max(result,std::abs(a[i]-b[i]));return result;}
MeshPaintRgb encoded(const Weights& value) {return normalizedMeshPaintRgb({float(value[0]),float(value[1]),float(value[2]),float(value[3])});}
double extent(const ModelPrimitive& p) {
    Vec3 low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high=-low;
    for(const auto& vertex:p.vertices)for(unsigned axis=0;axis<3;++axis){if(!std::isfinite(vertex.position[axis]))fail("source contains nonfinite positions.");low[axis]=std::min(low[axis],vertex.position[axis]);high[axis]=std::max(high[axis],vertex.position[axis]);}
    return std::max(1.,math::length(high-low));
}
std::shared_ptr<const ModelPrimitive> original(const std::shared_ptr<const ModelAsset>& model,std::uint32_t primitive,std::stop_token stop) {
    canceled(stop);if(!model||primitive>=model->primitives.size())fail("original model primitive is unavailable.");
    if(!model->primitives[primitive].vertices.empty())return {model,&model->primitives[primitive]};
    if(!model->stream||primitive>=model->stream->primitives.size()||model->stream->primitives[primitive].levels.empty())fail("original LOD0 is unavailable.");
    const auto lod=streaming::loadModelLod(model->stream->primitives[primitive].levels[0],primitive,0,false,stop);
    if(!lod)fail("original LOD0 could not be loaded.");return {lod,&lod->geometry};
}
std::shared_ptr<const ModelPrimitive> original(const ModelAsset& model,std::uint32_t primitive,std::stop_token stop) {
    // The incoming import generation is retained by the synchronous worker caller.
    return original(std::shared_ptr<const ModelAsset>{&model,[](const ModelAsset*){}},primitive,stop);
}
std::string material(const ModelAsset& model,const ModelPrimitive& primitive) {
    if(primitive.material>=model.materialIds.size()||model.materialIds[primitive.material].empty())fail("source material identity is unavailable.");
    return model.materialIds[primitive.material];
}
using Point=std::array<std::int64_t,3>;
using Triangle=std::array<Point,3>;
std::vector<Triangle> triangles(const ModelPrimitive& primitive,double quantum,std::stop_token stop) {
    if(primitive.indices.empty()||primitive.indices.size()%3)fail("source triangle table is invalid.");
    std::vector<Triangle> result;result.reserve(primitive.indices.size()/3);
    for(std::size_t first=0;first<primitive.indices.size();first+=3) {
        canceled(stop);Triangle value;
        for(unsigned corner=0;corner<3;++corner) {
            const auto vertex=primitive.indices[first+corner];if(vertex>=primitive.vertices.size())fail("source triangle index is invalid.");
            for(unsigned axis=0;axis<3;++axis){const double coordinate=primitive.vertices[vertex].position[axis]/quantum;if(!std::isfinite(coordinate)||std::abs(coordinate)>double(std::numeric_limits<std::int64_t>::max()/2))fail("source position exceeds matching limits.");value[corner][axis]=std::llround(coordinate);}
        }
        const auto start=std::min_element(value.begin(),value.end())-value.begin();Triangle ordered;for(unsigned i=0;i<3;++i)ordered[i]=value[(start+i)%3];result.push_back(ordered);
    }
    std::sort(result.begin(),result.end());return result;
}
bool sameTransform(const ModelDraw& a,const ModelDraw& b) {
    for(unsigned i=0;i<16;++i)if(!std::isfinite(a.transform[i])||!std::isfinite(b.transform[i])||std::abs(a.transform[i]-b.transform[i])>1e-6f)return false;return true;
}
struct Projection {
    std::array<double,2> value{};
    Projection operator+(const Projection& other)const{return {{value[0]+other.value[0],value[1]+other.value[1]}};}
    Projection operator-(const Projection& other)const{return {{value[0]-other.value[0],value[1]-other.value[1]}};}
    Projection operator*(double scale)const{return {{value[0]*scale,value[1]*scale}};}
};
double cross(const Projection& a,const Projection& b) {return a.value[0]*b.value[1]-a.value[1]*b.value[0];}
Projection project(Vec3 point,unsigned dropped) {Projection result;unsigned next=0;for(unsigned axis=0;axis<3;++axis)if(axis!=dropped)result.value[next++]=point[axis];return result;}
Vec3 normal(const MeshPaintGeometry& geometry,const MeshPaintCell& cell) {
    const auto a=geometry.vertices.at(cell.vertices[0]).position,b=geometry.vertices.at(cell.vertices[1]).position,c=geometry.vertices.at(cell.vertices[cell.cornerCount-1]).position;
    const auto n=math::cross(b-a,c-a);if(math::length(n)<1e-12)fail("surface contains a degenerate paint cell.");return math::normalized(n);
}
struct Cell {
    std::array<Vec3,4> points{};
    std::array<Weights,4> values{};
    Vec3 low{},high{},normal{};
    unsigned corners{},dropped{};
    double tolerance{};
    Cell(const MeshPaintGeometry& geometry,const MeshPaintCell& cell,const MeshPaintPayload& paint,double epsilon):corners(cell.cornerCount),tolerance(epsilon) {
        if(corners!=3&&corners!=4)fail("paint cell has an unsupported corner count.");
        normal=::crimson::normal(geometry,cell);
        dropped=static_cast<unsigned>(std::max_element(normal.begin(),normal.end(),[](float a,float b){return std::abs(a)<std::abs(b);})-normal.begin());
        for(unsigned i=0;i<corners;++i){points[i]=geometry.vertices.at(cell.vertices[i]).position;values[i]=weights(paint,cell.vertices[i]);if(!i)low=high=points[i];else for(unsigned axis=0;axis<3;++axis){low[axis]=std::min(low[axis],points[i][axis]);high[axis]=std::max(high[axis],points[i][axis]);}}
        if(corners==4&&std::abs(math::dot(points[2]-points[0],normal))>tolerance)fail("nonplanar source paint cannot be transferred unambiguously.");
    }
    bool sample(Vec3 point,Vec3 targetNormal,Weights& result)const {
        if(math::dot(normal,targetNormal)<.999f||std::abs(math::dot(point-points[0],normal))>tolerance)return false;
        for(unsigned axis=0;axis<3;++axis)if(point[axis]<low[axis]-tolerance||point[axis]>high[axis]+tolerance)return false;
        const auto origin=project(points[0],dropped),p=project(point,dropped),u=project(points[1],dropped)-origin,v=project(points[corners-1],dropped)-origin;
        const double determinant=cross(u,v);if(std::abs(determinant)<1e-18)return false;
        const auto delta=p-origin;double x=cross(delta,v)/determinant,y=cross(u,delta)/determinant;
        if(corners==4) {
            const auto twist=project(points[2],dropped)-project(points[1],dropped)-project(points[3],dropped)+origin;
            for(unsigned iteration=0;iteration<16;++iteration){const auto residual=origin+u*x+v*y+twist*(x*y)-p,du=u+twist*y,dv=v+twist*x;const auto det=cross(du,dv);if(std::abs(det)<1e-18)return false;const double dx=cross(residual,dv)/det,dy=cross(du,residual)/det;x-=dx;y-=dy;if(std::abs(dx)+std::abs(dy)<1e-10)break;}
            if(x<-.0001||x>1.0001||y<-.0001||y>1.0001)return false;
            const auto reconstructed=origin+u*x+v*y+twist*(x*y);if(std::hypot(reconstructed.value[0]-p.value[0],reconstructed.value[1]-p.value[1])>tolerance)return false;
            x=std::clamp(x,0.,1.);y=std::clamp(y,0.,1.);const std::array<double,4> factors{(1-x)*(1-y),x*(1-y),x*y,(1-x)*y};
            result={};for(unsigned i=0;i<4;++i)for(unsigned lane=0;lane<4;++lane)result[lane]+=values[i][lane]*factors[i];return true;
        }
        if(x<-.0001||y<-.0001||x+y>1.0001)return false;x=std::max(x,0.);y=std::max(y,0.);const auto total=std::max(1.,x+y);x/=total;y/=total;
        for(unsigned lane=0;lane<4;++lane)result[lane]=values[0][lane]*(1-x-y)+values[1][lane]*x+values[2][lane]*y;return true;
    }
};
class Surface {
    struct Node {Vec3 low{},high{};std::uint32_t first{},count{},left{},right{};};
    std::vector<Cell> cells_;std::vector<std::uint32_t> order_;std::vector<Node> nodes_;double tolerance_{};std::stop_token stop_;
    std::uint32_t build(std::uint32_t first,std::uint32_t count) {
        canceled(stop_);Node node;node.first=first;node.count=count;node.low=cells_[order_[first]].low;node.high=cells_[order_[first]].high;
        for(auto i=first;i<first+count;++i)for(unsigned axis=0;axis<3;++axis){node.low[axis]=std::min(node.low[axis],cells_[order_[i]].low[axis]);node.high[axis]=std::max(node.high[axis],cells_[order_[i]].high[axis]);}
        const auto index=static_cast<std::uint32_t>(nodes_.size());nodes_.push_back(node);
        if(count>8){const auto size=node.high-node.low;const auto axis=static_cast<unsigned>(std::max_element(size.begin(),size.end())-size.begin());const auto middle=first+count/2;
            std::nth_element(order_.begin()+first,order_.begin()+middle,order_.begin()+first+count,[&](auto a,auto b){const double ca=double(cells_[a].low[axis])+cells_[a].high[axis],cb=double(cells_[b].low[axis])+cells_[b].high[axis];return ca==cb?a<b:ca<cb;});
            const auto left=build(first,count/2),right=build(middle,count-count/2);nodes_[index].left=left;nodes_[index].right=right;nodes_[index].count=0;}
        return index;
    }
    void query(std::uint32_t index,Vec3 point,Vec3 direction,bool& found,Weights& result)const {
        const auto& node=nodes_[index];for(unsigned axis=0;axis<3;++axis)if(point[axis]<node.low[axis]-tolerance_||point[axis]>node.high[axis]+tolerance_)return;
        if(!node.count){query(node.left,point,direction,found,result);query(node.right,point,direction,found,result);return;}
        for(auto i=node.first;i<node.first+node.count;++i){Weights value;if(!cells_[order_[i]].sample(point,direction,value))continue;if(found&&difference(value,result)>1.5/255.)fail("overlapping source surfaces have different paint.");if(!found){result=value;found=true;}}
    }
public:
    Surface(const MeshPaintGeometry& geometry,const MeshPaintPayload& paint,double tolerance,std::stop_token stop):tolerance_(tolerance),stop_(stop) {
        if(geometry.cells.empty()||geometry.cells.size()>MaximumMeshPaintTriangles)fail("source paint cell table exceeds its budget.");
        cells_.reserve(geometry.cells.size());for(const auto& cell:geometry.cells){canceled(stop);cells_.emplace_back(geometry,cell,paint,tolerance);}
        order_.resize(cells_.size());std::iota(order_.begin(),order_.end(),0);nodes_.reserve(cells_.size()*2);build(0,static_cast<std::uint32_t>(cells_.size()));
    }
    Weights sample(Vec3 point,Vec3 direction)const {canceled(stop_);bool found=false;Weights result{};query(0,point,direction,found,result);if(!found)fail("the corrected grid is not completely covered by the old paint surface.");return result;}
};
void rigid(const ModelAsset& asset) {
    if(!asset.skins.empty()||!asset.animations.empty())fail("only rigid static meshes can migrate paint.");
    for(const auto& primitive:asset.primitives)if(primitive.hasSkinning||!primitive.skinWeights.empty()||!primitive.morphTargets.empty())fail("only rigid static meshes can migrate paint.");
}
QuaderPaintReplacement migrateAtLevel(const QuaderPaintInput& input,const ModelAsset& after,std::uint32_t targetLevel,std::stop_token stop) {
    CRIMSON_PROFILE_ZONE("Quader vertex paint migration worker");canceled(stop);
    QuaderPaintReplacement result;result.sourceKey=input.sourceKey;result.beforeModelId=input.modelId;result.before=input.paint;result.after=input.paint;
    if(!input.paint)return result;
    if(!input.model||input.model->id!=input.modelId)fail("painted source model is unavailable.");
    if(input.paint->recipeVersion!=MeshPaintRecipeVersion||input.paint->subdivisionLevel>MaximumMeshPaintLevel)fail("unsupported saved paint recipe.");
    if(targetLevel>MaximumMeshPaintLevel||targetLevel<input.paint->subdivisionLevel)fail("unsupported destination paint level.");
    result.after->subdivisionLevel=targetLevel;
    rigid(*input.model);rigid(after);
    const auto& before=*input.model;std::map<std::uint32_t,std::shared_ptr<const ModelPrimitive>> oldOriginals,newOriginals;
    std::map<std::uint32_t,std::shared_ptr<const MeshPaintGeometry>> newGeometries;
    const auto getOld=[&](std::uint32_t index){auto& p=oldOriginals[index];if(!p)p=original(input.model,index,stop);return p;};
    const auto getNew=[&](std::uint32_t index){auto& p=newOriginals[index];if(!p)p=original(after,index,stop);return p;};
    const auto getNewGeometry=[&](std::uint32_t index){auto& geometry=newGeometries[index];if(!geometry)geometry=targetGeometry(*getNew(index),targetLevel,stop);return geometry;};
    // Every draw uses the occurrence's authored subdivision level, including
    // slots with no RGB payload. Validate the complete runtime geometry before
    // publishing an import that would make the paint preparation unusable.
    std::uint64_t totalVertices=0,totalTriangles=0;
    for(const auto& draw:after.draws) {
        canceled(stop);const auto geometry=getNewGeometry(draw.primitive);totalVertices+=geometry->vertices.size();totalTriangles+=geometry->indices.size()/3;
        if(totalVertices>MaximumMeshPaintVertices||totalTriangles>MaximumMeshPaintTriangles)throw PaintMigrationBudgetError("corrected subdivision exceeds the entity vertex or triangle budget.");
    }
    std::set<std::uint32_t> oldDraws,newDraws;double squaredError=0;std::uint64_t errorSamples=0;
    for(auto& stored:result.after->draws) {
        canceled(stop);if(stored.draw>=before.draws.size()||!oldDraws.insert(stored.draw).second||before.draws[stored.draw].primitive!=stored.primitive)fail("stored paint draw identity no longer matches its source.");
        if(stored.level!=input.paint->subdivisionLevel||!stored.weights||!validMeshPaintPayload(*stored.weights))fail("saved paint payload or level is invalid.");
        const auto oldPrimitive=getOld(stored.primitive);const auto oldGeometry=buildMeshPaintGeometry(*oldPrimitive,stored.level,stop);
        if(oldGeometry->sourceSignature!=stored.sourceSignature||stored.weights->vertexCount!=oldGeometry->vertices.size())fail("saved paint signature or vertex count no longer matches its source.");
        const double tolerance=std::max(1e-6,extent(*oldPrimitive)*1e-6),quantum=tolerance;
        const auto sourceTriangles=triangles(*oldPrimitive,quantum,stop);const auto sourceMaterial=material(before,*oldPrimitive);
        const auto corresponds=[&](std::uint32_t draw) {
            if(draw>=after.draws.size()||!sameTransform(before.draws[stored.draw],after.draws[draw]))return false;
            const auto p=getNew(after.draws[draw].primitive);return material(after,*p)==sourceMaterial&&triangles(*p,quantum,stop)==sourceTriangles;
        };
        std::optional<std::uint32_t> target;
        if(corresponds(stored.draw))target=stored.draw;
        else for(std::uint32_t draw=0;draw<after.draws.size();++draw)if(corresponds(draw)){if(target)fail("painted draw correspondence is ambiguous.");target=draw;}
        if(!target||!newDraws.insert(*target).second)fail("a painted draw has no unique unchanged surface in the new import.");
        const auto newPrimitive=getNew(after.draws[*target].primitive);const auto newGeometry=getNewGeometry(after.draws[*target].primitive);
        if(oldGeometry->sourceSignature==newGeometry->sourceSignature&&oldGeometry->level==newGeometry->level) {stored.draw=*target;stored.primitive=after.draws[*target].primitive;continue;}
        Surface oldSurface(*oldGeometry,*stored.weights,tolerance,stop);
        std::vector<std::uint8_t> bytes(newGeometry->vertices.size()*3);std::vector<bool> written(newGeometry->vertices.size());
        for(const auto& cell:newGeometry->cells) {
            const auto direction=normal(*newGeometry,cell);
            for(unsigned corner=0;corner<cell.cornerCount;++corner) {
                const auto vertex=cell.vertices[corner];const auto value=oldSurface.sample(newGeometry->vertices[vertex].position,direction);const auto rgb=encoded(value);
                if(written[vertex]){const MeshPaintRgb prior{bytes[vertex*3],bytes[vertex*3+1],bytes[vertex*3+2]};for(unsigned lane=0;lane<3;++lane)if(std::abs(int(prior[lane])-int(rgb[lane]))>1)fail("shared target controls require conflicting source paint.");}
                else {std::copy(rgb.begin(),rgb.end(),bytes.begin()+vertex*3);written[vertex]=true;}
            }
        }
        // Import primitives can carry unused vertices from other material slots.
        // Preserve matching original controls without requiring unrelated sheets
        // to belong to this draw's paint surface.
        for(std::size_t vertex=0;vertex<written.size();++vertex)if(!written[vertex]&&vertex<oldPrimitive->vertices.size()&&vertex<newPrimitive->vertices.size()&&oldPrimitive->vertices[vertex].position==newPrimitive->vertices[vertex].position){const auto rgb=stored.weights->rgb(static_cast<std::uint32_t>(vertex));std::copy(rgb.begin(),rgb.end(),bytes.begin()+vertex*3);}
        stored.weights=makeMeshPaintPayload(static_cast<std::uint32_t>(newGeometry->vertices.size()),bytes);stored.sourceSignature=newGeometry->sourceSignature;stored.draw=*target;stored.primitive=after.draws[*target].primitive;stored.level=targetLevel;
        constexpr std::array<double,3> samples{.211324865405187,.5,.788675134594813};
        for(const auto& cell:newGeometry->cells) {
            const auto direction=normal(*newGeometry,cell);Cell targetCell(*newGeometry,cell,*stored.weights,tolerance);
            for(const double x:samples)for(const double y:samples) {
                if(cell.cornerCount==3&&x+y>=1)continue;Vec3 point{};
                if(cell.cornerCount==4){const std::array<double,4> factors{(1-x)*(1-y),x*(1-y),x*y,(1-x)*y};for(unsigned corner=0;corner<4;++corner)point=point+newGeometry->vertices[cell.vertices[corner]].position*float(factors[corner]);}
                else point=newGeometry->vertices[cell.vertices[0]].position*float(1-x-y)+newGeometry->vertices[cell.vertices[1]].position*float(x)+newGeometry->vertices[cell.vertices[2]].position*float(y);
                const auto oldValue=oldSurface.sample(point,direction);Weights newValue;if(!targetCell.sample(point,direction,newValue))fail("target interior validation failed.");
                for(unsigned lane=0;lane<4;++lane){const double error=std::abs(oldValue[lane]-newValue[lane]);result.maxError=std::max(result.maxError,float(error));squaredError+=error*error;++errorSamples;}
            }
        }
    }
    result.rmsError=errorSamples?float(std::sqrt(squaredError/double(errorSamples))):0;return result;
}
}
QuaderPaintReplacement migrateQuaderPaint(const QuaderPaintInput& input,const ModelAsset& after,std::stop_token stop) {
    auto best=migrateAtLevel(input,after,input.paint?input.paint->subdivisionLevel:0,stop);
    if(!input.paint)return best;
    constexpr float acceptableMaximumError=.015f;
    for(std::uint32_t level=input.paint->subdivisionLevel+1;level<=MaximumMeshPaintLevel&&best.maxError>acceptableMaximumError;++level) {
        canceled(stop);
        try {
            // Always sample the retained original field. Refining an already
            // transferred result would retain its lost boundary detail.
            auto candidate=migrateAtLevel(input,after,level,stop);
            if(candidate.rmsError<best.rmsError)best=std::move(candidate);
        }catch(const PaintMigrationBudgetError&) {
            // A finer subdivision cannot reduce geometry memory. Keep the
            // complete valid candidate; all other preparation errors propagate.
            break;
        }
    }
    return best;
}
}
