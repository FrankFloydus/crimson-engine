#pragma once
// Private GPU regression support. Geometry is a frozen extraction of the real
// authored room; double-precision triangle rays provide an independent oracle.
#include <nlohmann/json.hpp>
#include <limits>
#include <numeric>
#include <set>

namespace room_shadow_fixture {
using namespace crimson;
using Json=nlohmann::json;
using D3=std::array<double,3>;
D3 d3(Vec3 p){return {p[0],p[1],p[2]};}
D3 add(D3 a,D3 b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
D3 sub(D3 a,D3 b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
D3 mul(D3 a,double x){return {a[0]*x,a[1]*x,a[2]*x};}
double dot(D3 a,D3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
D3 cross(D3 a,D3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec3 vector(const Json& value){return {value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};}
Mat4 matrix(const Json& value){Mat4 result;for(unsigned i=0;i<16;++i)result[i]=value.at(i).get<float>();return result;}
struct Triangle {D3 a,b,c,normal;unsigned draw;};
struct Hit {double distance{std::numeric_limits<double>::max()},edge{};D3 position{};unsigned triangle{};};
struct Node {D3 lower,upper;unsigned begin{},count{},left{},right{};};
struct Oracle {
    std::vector<Triangle> triangles;
    std::vector<unsigned> order;
    std::vector<Node> nodes;
    unsigned build(unsigned begin,unsigned count){
        Node node;node.lower={1e100,1e100,1e100};node.upper={-1e100,-1e100,-1e100};node.begin=begin;node.count=count;
        for(unsigned i=begin;i<begin+count;++i){const auto& t=triangles[order[i]];for(const auto p:{t.a,t.b,t.c})for(unsigned a=0;a<3;++a){node.lower[a]=std::min(node.lower[a],p[a]);node.upper[a]=std::max(node.upper[a],p[a]);}}
        const auto index=static_cast<unsigned>(nodes.size());nodes.push_back(node);
        if(count>8){unsigned axis=0;for(unsigned a=1;a<3;++a)if(node.upper[a]-node.lower[a]>node.upper[axis]-node.lower[axis])axis=a;
            const auto mid=begin+count/2;std::nth_element(order.begin()+begin,order.begin()+mid,order.begin()+begin+count,[&](unsigned a,unsigned b){const auto& x=triangles[a];const auto& y=triangles[b];return x.a[axis]+x.b[axis]+x.c[axis]<y.a[axis]+y.b[axis]+y.c[axis];});
            const auto left=build(begin,count/2),right=build(mid,count-count/2);nodes[index].count=0;nodes[index].left=left;nodes[index].right=right;}
        return index;
    }
    void finish(){order.resize(triangles.size());std::iota(order.begin(),order.end(),0u);nodes.reserve(triangles.size()/3+1);build(0,static_cast<unsigned>(triangles.size()));}
    bool box(const Node& n,D3 origin,D3 direction,double maximum)const{
        double low=0,high=maximum;for(unsigned a=0;a<3;++a){if(std::abs(direction[a])<1e-15){if(origin[a]<n.lower[a]-1e-8||origin[a]>n.upper[a]+1e-8)return false;continue;}
            auto first=(n.lower[a]-origin[a])/direction[a],last=(n.upper[a]-origin[a])/direction[a];if(first>last)std::swap(first,last);low=std::max(low,first);high=std::min(high,last);if(low>high+1e-8)return false;}return true;
    }
    void query(unsigned index,D3 origin,D3 direction,Hit& hit)const{
        const auto& node=nodes[index];if(!box(node,origin,direction,hit.distance))return;
        if(!node.count){query(node.left,origin,direction,hit);query(node.right,origin,direction,hit);return;}
        for(unsigned i=node.begin;i<node.begin+node.count;++i){const auto ti=order[i];const auto& t=triangles[ti];const auto e1=sub(t.b,t.a),e2=sub(t.c,t.a),p=cross(direction,e2);const auto determinant=dot(e1,p);if(std::abs(determinant)<1e-14)continue;
            const auto delta=sub(origin,t.a);const auto u=dot(delta,p)/determinant;if(u<0||u>1)continue;const auto q=cross(delta,e1);const auto v=dot(direction,q)/determinant;if(v<0||u+v>1)continue;
            const auto distance=dot(e2,q)/determinant;if(distance<=1e-8||distance>=hit.distance)continue;
            hit.distance=distance;hit.position=add(origin,mul(direction,distance));hit.triangle=ti;hit.edge=std::min({u,v,1-u-v});}
    }
    std::optional<Hit> ray(D3 origin,D3 direction,double maximum=1e100)const{Hit hit;hit.distance=maximum;query(0,origin,direction,hit);return hit.distance<maximum?std::optional<Hit>{hit}:std::nullopt;}
    bool blocked(D3 source,const Hit& receiver)const{const auto delta=sub(receiver.position,source);const auto distance=std::sqrt(dot(delta,delta));return bool(ray(source,mul(delta,1/distance),distance-1e-5));}
};
struct Room {
    Json source;
    std::vector<std::shared_ptr<ModelAsset>> assets;
    std::vector<RenderModelInstance> instances;
    std::vector<std::string> names;
    std::set<unsigned> wallDraws;
    Oracle oracle;
    Vec3 corner{};
    explicit Room(const std::filesystem::path& path){
        std::ifstream file(path);check(bool(file),"Cannot read the actual-room shadow fixture.");file>>source;
        for(const auto& geometry:source.at("geometries")){
            auto asset=std::make_shared<ModelAsset>();asset->id="actual-room-shadow-"+std::to_string(assets.size());ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
            const auto& positions=geometry.at("positions");const auto& normals=geometry.at("normals");
            for(std::size_t i=0;i<positions.size();++i){ModelVertex vertex;vertex.position=vector(positions[i]);vertex.normal=vector(normals[i]);primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);}
            primitive.indices=geometry.at("indices").get<std::vector<std::uint32_t>>();asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
            ModelMaterial material;material.baseColor={.15f,.15f,.15f,1};material.metallic=0;material.roughness=1;material.specular=0;material.doubleSided=true;asset->materials.push_back(material);assets.push_back(std::move(asset));
        }
        for(const auto& draw:source.at("draws")){
            RenderModelInstance instance;instance.entityId=10000+instances.size();instance.model.asset=assets.at(draw.at("geometry").get<unsigned>());instance.model.transform=matrix(draw.at("world"));instance.forceFullDetail=true;
            instances.push_back(instance);names.push_back(draw.at("entityName").get<std::string>());
            const auto& primitive=instance.model.asset->primitives.front();const auto drawIndex=static_cast<unsigned>(instances.size()-1);
            for(std::size_t i=0;i<primitive.indices.size();i+=3){Triangle t;t.draw=drawIndex;
                t.a=d3(math::transformPoint(instance.model.transform,primitive.vertices[primitive.indices[i]].position));t.b=d3(math::transformPoint(instance.model.transform,primitive.vertices[primitive.indices[i+1]].position));t.c=d3(math::transformPoint(instance.model.transform,primitive.vertices[primitive.indices[i+2]].position));
                auto n=cross(sub(t.b,t.a),sub(t.c,t.a));const auto length=std::sqrt(dot(n,n));if(length<1e-14)continue;t.normal=mul(n,1/length);oracle.triangles.push_back(t);}
        }
        oracle.finish();
        for(const auto& triangle:oracle.triangles){const auto& primitive=instances[triangle.draw].model.asset->primitives.front();
            if(primitive.vertices.size()==4&&primitive.indices.size()==6&&std::abs(triangle.normal[1])<.1)wallDraws.insert(triangle.draw);}
        bool found=false;for(const auto& t:oracle.triangles)if(names[t.draw]=="Box_028"){corner={static_cast<float>(std::min({t.a[0],t.b[0],t.c[0]})),0,static_cast<float>(t.a[2])};found=true;break;}
        check(found&&instances.size()>=31,"Actual room snapshot is missing the imported corner or full room.");
    }
};

double pixelLinear(const DecodedImage& image,unsigned x,unsigned y){double value=0;for(unsigned c=0;c<3;++c){const auto s=image.rgba[(std::size_t(y)*image.width+x)*4+c]/255.;value+=s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);}return value/3;}
struct Samples {unsigned lit{},blocked{},darkLit{},brightBlocked{},filterEdges{};double minimumLit{1},maximumBlocked{},litError{},blockedLight{};Json failures=Json::array();};
bool realFilterBoundary(const Room& room,D3 eye,D3 light,const Hit& hit,bool blocked){
    const auto& triangle=room.oracle.triangles[hit.triangle];auto normal=triangle.normal;
    if(dot(normal,sub(eye,hit.position))<0)normal=mul(normal,-1);
    const auto original=sub(hit.position,light);const auto radial=std::sqrt(dot(original,original));
    const auto axial=std::max({std::abs(original[0]),std::abs(original[1]),std::abs(original[2])});
    const auto cosine=std::clamp(dot(normal,mul(original,-1/radial)),0.,1.);
    const auto query=add(original,mul(normal,2*axial/1024*std::sqrt(std::max(0.,1-cosine*cosine))));
    const auto magnitude=D3{std::abs(query[0]),std::abs(query[1]),std::abs(query[2])};
    unsigned face;std::array<double,2> coordinates;
    if(magnitude[0]>=magnitude[1]&&magnitude[0]>=magnitude[2]){face=query[0]>=0?0:1;coordinates={query[0]>=0?query[2]/magnitude[0]:-query[2]/magnitude[0],-query[1]/magnitude[0]};}
    else if(magnitude[1]>=magnitude[2]){face=query[1]>=0?2:3;coordinates={-query[0]/magnitude[1],query[1]>=0?query[2]/magnitude[1]:-query[2]/magnitude[1]};}
    else{face=query[2]>=0?4:5;coordinates={query[2]>=0?-query[0]/magnitude[2]:query[0]/magnitude[2],-query[1]/magnitude[2]};}
    std::array<double,2> pixel,lower,fraction;for(unsigned a=0;a<2;++a){pixel[a]=2+(coordinates[a]*.5+.5)*1024;lower[a]=std::floor(pixel[a]-.5)+.5;fraction[a]=pixel[a]-lower[a];}
    const auto plane=dot(normal,original);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        if((x==0&&fraction[0]==1)||(x==3&&fraction[0]==0)||(y==0&&fraction[1]==1)||(y==3&&fraction[1]==0))continue;
        const auto u=(std::clamp(lower[0]+double(x)-1,.5,1027.5)-2)/512-1;
        const auto v=(std::clamp(lower[1]+double(y)-1,.5,1027.5)-2)/512-1;
        const auto ray=face==0?D3{1,-v,u}:face==1?D3{-1,-v,-u}:face==2?D3{-u,1,v}:face==3?D3{-u,-1,-v}:face==4?D3{-u,-v,1}:D3{u,-v,-1};
        const auto denominator=dot(normal,ray);if(std::abs(denominator)<1e-12)continue;
        const auto distance=plane/denominator;if(distance<=0||!std::isfinite(distance))continue;
        auto neighbor=hit;neighbor.position=add(light,mul(ray,distance));
        if(room.oracle.blocked(light,neighbor)!=blocked)return true;
    }
    return false;
}
Samples compare(const Room& room,const Camera& camera,Vec3 light,const DecodedImage& clear,const DecodedImage& shadow,bool contact){
    Samples result;const auto forward=d3(camera.forward()),right=d3(camera.right()),up=d3(math::cross(camera.right(),camera.forward()));
    const auto tangent=std::tan(camera.verticalFov(1)*3.141592653589793/360);const auto origin=d3(camera.position);
    for(unsigned y=3;y<ImageSize-3;++y)for(unsigned x=3;x<ImageSize-3;++x){
        auto direction=add(forward,add(mul(right,(2*(x+.5)/ImageSize-1)*tangent),mul(up,(1-2*(y+.5)/ImageSize)*tangent)));direction=mul(direction,1/std::sqrt(dot(direction,direction)));
        const auto hit=room.oracle.ray(origin,direction);if(!hit)continue;const auto& triangle=room.oracle.triangles[hit->triangle];
        if(!room.wallDraws.contains(triangle.draw))continue;
        // Interior samples avoid primary-camera silhouette/triangle-edge pixel
        // coverage. Contact itself is deliberately retained by the macro view.
        if(!contact&&hit->edge<.008)continue;
        const auto base=pixelLinear(clear,x,y);if(base<.015||base>.85)continue;
        const auto visibility=pixelLinear(shadow,x,y)/base;const bool blocked=room.oracle.blocked(d3(light),*hit);
        if(!contact&&((blocked&&visibility>.1)||(!blocked&&visibility<.9))){
            // Binary rays are not an oracle for a deliberately filtered edge.
            // Intersect the actual 16 PCF texel rays with the original receiving
            // plane. This retains grazing stretch and proves a real geometric
            // shadow boundary, without excusing self-shadowing on a lit plane.
            // Macro corner views deliberately retain all contact samples.
            if(realFilterBoundary(room,origin,d3(light),*hit,blocked)){++result.filterEdges;continue;}
        }
        if(((blocked&&visibility>.1)||(!blocked&&visibility<.9))&&result.failures.size()<12){
            const auto delta=sub(hit->position,d3(light));const auto distance=std::sqrt(dot(delta,delta));
            const auto caster=room.oracle.ray(d3(light),mul(delta,1/distance),distance-1e-5);
            Json failure{{"pixel",{x,y}},{"receiver",room.names[triangle.draw]},{"position",hit->position},{"visibility",visibility},{"blocked",blocked}};
            if(caster)failure["caster"]={{"entity",room.names[room.oracle.triangles[caster->triangle].draw]},{"position",caster->position},{"sourceDistance",caster->distance},{"receiverGap",distance-caster->distance}};
            result.failures.push_back(std::move(failure));
        }
        if(blocked){
            ++result.blocked;result.maximumBlocked=std::max(result.maximumBlocked,visibility);result.blockedLight+=visibility;if(visibility>.1)++result.brightBlocked;
        }else{
            ++result.lit;result.minimumLit=std::min(result.minimumLit,visibility);result.litError+=std::abs(visibility-1);if(visibility<.9)++result.darkLit;
        }
    }
    return result;
}

template<class Read> void exercise(Renderer& renderer,const ShaderCompileConfig& config,const Read& read){
    Room room(CC_SHADOW_ROOM_FIXTURE);
    for(const auto& asset:room.assets)check(renderer.prepareModel(*asset),renderer.modelStatus());
    const auto helperPath=config.sourceDirectory/"point_shadow.sh",vertexPath=config.sourceDirectory/"vs_point_shadow.sc";
    const auto contents=[](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);check(bool(f),"Cannot read room diagnostic shader.");return std::string(std::istreambuf_iterator<char>(f),{});};
    const auto helper=contents(helperPath),caster=contents(vertexPath);
    ShaderHelperRestore restoreHelper{helperPath,helper},restoreCaster{vertexPath,caster};
    const auto replace=[](std::string text,const std::string& before,const std::string& after){std::size_t begin=0;bool found=false;while((begin=text.find(before,begin))!=std::string::npos){text.replace(begin,before.size(),after);begin+=after.size();found=true;}check(found,"Room diagnostic shader boundary changed.");return text;};
    auto prior=helper;const auto numeric=prior.find("    // Bound numerical comparison error");const auto visibility=prior.find("    float visibility = 0.0;",numeric);
    check(numeric!=std::string::npos&&visibility!=std::string::npos,"Cannot locate the historical comparison tolerance.");
    prior.replace(numeric,visibility-numeric,"    float comparisonTolerance = 0.00000012;\n");
    prior=replace(prior,"sqrt(max(0.0, 1.0 - normalCosine * normalCosine))","(1.0 - normalCosine)");
    // Shadow submissions set every scope component to zero. Keep the unused
    // split path reachable through this existing uniform so the historical
    // control retains exactly the rebuilt program's reflected interface.
    auto priorCaster=replace(caster,"gl_Position = mul(u_proj, lightView);",
        "gl_Position = u_surfaceLighting.x > 0.5 ? mul(u_proj, lightView) : mul(u_viewProj, world);");
    writeFileAtomically(helperPath,prior);writeFileAtomically(vertexPath,priorCaster);
    const auto modelVertex=compileShader(config,"vs_model").bytes;
    const auto shadowFragment=compileShader(config,"fs_point_shadow").bytes;
    const std::array oldPrograms{
        RendererShaderPair{RendererProgram::Model,modelVertex,compileShader(config,"fs_model").bytes},
        RendererShaderPair{RendererProgram::PointShadow,compileShader(config,"vs_point_shadow").bytes,shadowFragment}};
    writeFileAtomically(helperPath,helper);writeFileAtomically(vertexPath,caster);
    const std::array fixedPrograms{
        RendererShaderPair{RendererProgram::Model,modelVertex,compileShader(config,"fs_model").bytes},
        RendererShaderPair{RendererProgram::PointShadow,compileShader(config,"vs_point_shadow").bytes,shadowFragment}};

    const auto& authored=room.source.at("worldFill").at("settings");
    PointLight light;light.entityId=9999;light.radius=authored.at("radius").get<float>();light.intensity=authored.at("intensity").get<float>();
    light.nearFalloffRadius=authored.at("nearFalloffRadius").get<float>();light.attenuation=authored.at("attenuation").get<float>();
    light.rangeFalloffExponent=authored.at("rangeFalloffExponent").get<float>();light.edgeSoftness=authored.at("edgeSoftness").get<float>();
    light.sourceRadius=authored.at("sourceRadius").get<float>();light.specularStrength=authored.at("specularStrength").get<float>();
    light.color=vector(authored.at("color")); // Production uploads perform the single sRGB decode.
    light.shadowDepthBias=0;light.shadowNormalBias=1;
    const auto height=authored.at("height").get<float>();
    SceneSettings settings;settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.ambientVisibility.enabled=false;settings.environment.glow.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;
    settings.camera.nearPlane=.0001f;settings.camera.farPlane=100;settings.camera.fov=45;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=room.instances;scene.shadowInstances=room.instances;
    scene.pointLights=std::span(&light,1);scene.shadowWorldId=9911;scene.shadowResolution=1024;
    struct Pose {std::string label;Vec3 player,eye,target;bool contact{},baseline{};float fov{45},nearPlane{.0001f},exposure{1};};std::vector<Pose> poses;
    const auto world=[&](Vec3 offset){return room.corner+offset;};
    for(float y:{.25f,1.75f,2.75f})for(const auto offset:std::array<Vec2,5>{{{-.3f,.3f},{-.301f,.3f},{-.45f,.6f},{-.6f,1.2f},{-.3f,3.5f}}})
        poses.push_back({"corner-height-"+std::to_string(y)+"-"+std::to_string(poses.size()),world({offset[0],0,offset[1]}),world({.0035f,y,.012f}),world({.0035f,y,0}),true,false});
    for(unsigned wall=0;wall<2;++wall)for(float side:{-1.f,1.f})for(float along:{.3f,.6f,1.2f,2.f,3.5f,3.7f}){
        const Vec3 player=wall?Vec3{side*.3f,0,along}:Vec3{along,0,side*.3f};
        const Vec3 target=wall?Vec3{0,height,along}:Vec3{along,height,0};
        const Vec3 eye=target+(wall?Vec3{side*.9f,0,0}:Vec3{0,0,side*.9f});
        poses.push_back({"normal-wall-"+std::to_string(wall)+"-"+std::to_string(poses.size()),world(player),world(eye),world(target),false,true});
    }
    for(float y:{.2f,2.8f})for(unsigned wall=0;wall<2;++wall){
        const Vec3 player=wall?Vec3{.3f,0,3.5f}:Vec3{3.5f,0,.3f};
        const Vec3 target=wall?Vec3{0,y,.35f}:Vec3{.35f,y,0};
        const Vec3 eye=target+(wall?Vec3{.9f,0,0}:Vec3{0,0,.9f});
        poses.push_back({"grazing-wall-"+std::to_string(wall)+"-"+std::to_string(poses.size()),world(player),world(eye),world(target),false,false});
    }
    for(const auto draw:room.wallDraws){
        const auto triangle=std::find_if(room.oracle.triangles.begin(),room.oracle.triangles.end(),[&](const auto& t){return t.draw==draw;});
        Vec3 target{};Aabb bounds;const auto& instance=room.instances[draw];const auto& primitive=instance.model.asset->primitives.front();
        for(const auto& vertex:primitive.vertices){const auto p=math::transformPoint(instance.model.transform,vertex.position);target=target+p*.25;math::include(bounds,p);}
        const auto inset=std::min(.1f,(bounds.maximum[1]-bounds.minimum[1])*.25f);
        target[1]=std::clamp(height,bounds.minimum[1]+inset,bounds.maximum[1]-inset);const Vec3 normal{static_cast<float>(triangle->normal[0]),static_cast<float>(triangle->normal[1]),static_cast<float>(triangle->normal[2])};
        for(float side:{-1.f,1.f}){const auto source=target+normal*(side*.3f);auto player=source;player[1]=0;
            poses.push_back({"all-walls-"+room.names[draw]+"-"+std::to_string(poses.size()),player,target+normal*(side*.25f),target,false,true});}
    }
    const auto& camera=room.source.at("activeCamera");const auto authoredCamera=matrix(camera.at("world"));const auto follow=vector(camera.at("settings").at("followOffset"));
    const Vec3 authoredForward{-authoredCamera[8],-authoredCamera[9],-authoredCamera[10]};
    for(const auto offset:std::array<Vec2,7>{{{-.3f,.3f},{-.301f,.3f},{-.3f,.6f},{-.3f,1.2f},{-.3f,2.4f},{.3f,.3f},{1.2f,.3f}}}){
        const auto player=world({offset[0],0,offset[1]}),eye=player+follow;
        poses.push_back({"game-follow-"+std::to_string(poses.size()),player,eye,eye+authoredForward,false,false,camera.at("settings").at("verticalFov").get<float>(),camera.at("settings").at("near").get<float>()});
    }
    // Flat-wall diagnostics can be extremely dim at grazing incidence. Keep
    // real light settings, but calibrate BOTH captures' diagnostic exposure so
    // 8-bit readback can resolve the unchanged visibility thresholds. Follow
    // views keep exposure one because their target is a camera direction.
    double linearColor=0;for(const auto channel:light.color){const auto s=double(channel);linearColor+=(s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4))/3;}
    for(auto& pose:poses)if(!pose.label.starts_with("game-follow-")){
        const auto delta=d3(pose.player+Vec3{0,height,0}-pose.target);const auto squared=dot(delta,delta);
        const auto view=sub(d3(pose.eye),d3(pose.target));const auto normal=mul(view,1/std::sqrt(dot(view,view)));
        const auto cosine=std::max(0.,dot(normal,delta)/std::sqrt(squared));
        const auto attenuation=point_lighting::pointLightAttenuation(float(squared),light.inverseRadiusSquared(),
            light.nearFalloffRadius*light.nearFalloffRadius,light.rangeFalloffExponent,light.edgeSoftness,light.attenuation);
        const auto reference=.15*linearColor*light.intensity*attenuation*cosine/std::acos(-1.);
        check(std::isfinite(reference)&&reference>0,"Actual-room diagnostic target has no light response: "+pose.label);
        pose.exposure=float(std::clamp(.15/reference,1.,100.));
    }
    const std::filesystem::path output=CC_SHADOW_ROOM_RESULTS;std::filesystem::create_directories(output);
    Json report{{"fixture",CC_SHADOW_ROOM_FIXTURE},{"worldDraws",room.instances.size()},{"worldTriangles",room.oracle.triangles.size()},
        {"depthBias",0},{"normalBias",1},{"height",height},{"minimumClearLinear",.015},{"appearance","Neutral opaque matte receivers isolate shadow visibility; all actual room geometry and transforms retained. Wall diagnostics calibrate exposure equally for clear and shadow captures from the shared light attenuation and target orientation, avoiding 8-bit quantization. Follow views retain exposure one."},{"cases",Json::array()}};
    unsigned historicalDark=0,worstHistoricalDark=0,totalLit=0,totalBlocked=0,totalDark=0,totalLeak=0;
    const auto imageWrite=[&](const DecodedImage& image,const std::string& name){const auto error=lodepng::encode((output/name).string(),image.rgba,image.width,image.height);check(error==0,"Cannot write room regression image.");};
    const auto run=[&](const Pose& pose,bool game,bool historical){
        light.position=pose.player+Vec3{0,height,0};light.castShadows=false;settings.camera.position=pose.eye;settings.camera.target=pose.target;settings.camera.fov=pose.fov;settings.camera.nearPlane=pose.nearPlane;settings.exposure=pose.exposure;
        const auto capture=[&]{if(game)renderer.renderGame(scene);else renderer.render(scene);check(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
        const auto clear=capture();light.castShadows=true;const auto shadow=capture();const auto result=compare(room,settings.camera,light.position,clear,shadow,pose.contact);
        report["cases"].push_back({{"label",pose.label},{"game",game},{"historical",historical},{"exposure",pose.exposure},{"player",{pose.player[0],pose.player[1],pose.player[2]}},
            {"lit",result.lit},{"blocked",result.blocked},{"darkLit",result.darkLit},{"brightBlocked",result.brightBlocked},{"realFilterEdgeSamples",result.filterEdges},
            {"minimumLit",result.minimumLit},{"maximumBlocked",result.maximumBlocked},{"meanLitError",result.lit?result.litError/result.lit:0},{"meanBlockedLight",result.blocked?result.blockedLight/result.blocked:0},{"failures",result.failures}});
        writeFileAtomically(output/"results.json",report.dump(2)+"\n");
        if(historical){historicalDark+=result.darkLit;if(result.darkLit>worstHistoricalDark){worstHistoricalDark=result.darkLit;report["worstHistoricalCase"]=pose.label;imageWrite(clear,"historical-blotch-clear.png");imageWrite(shadow,"historical-blotch-shadow.png");}return;}
        if(!game&&report.value("worstHistoricalCase",std::string{})==pose.label){imageWrite(clear,"repaired-blotch-clear.png");imageWrite(shadow,"repaired-blotch-shadow.png");}
        if(!game&&pose.label==poses.front().label){imageWrite(clear,"repaired-corner-clear.png");imageWrite(shadow,"repaired-corner-shadow.png");}
        totalLit+=result.lit;totalBlocked+=result.blocked;totalDark+=result.darkLit;totalLeak+=result.brightBlocked;
        const auto label=std::string(game?"Game ":"Scene ")+pose.label;
        if(result.darkLit||result.brightBlocked||result.lit+result.blocked<=100){imageWrite(clear,label+"-clear.png");imageWrite(shadow,label+"-shadow.png");}
        check(result.lit+result.blocked>100,"Actual-room camera did not sample the target walls: "+label);
        check(result.darkLit==0,"Actual-room lit wall developed false self-shadowing: "+label+" pixels="+std::to_string(result.darkLit)+" minimum="+std::to_string(result.minimumLit));
        check(result.brightBlocked==0,"Actual-room blocked wall leaked: "+label+" pixels="+std::to_string(result.brightBlocked)+" maximum="+std::to_string(result.maximumBlocked));
    };
    check(renderer.reloadPrograms(oldPrograms),renderer.shaderStatus());
    for(const auto& pose:poses)if(pose.baseline)run(pose,false,true);
    check(historicalDark>10,"Actual-room historical control did not reproduce the reported self-shadowing.");
    check(renderer.reloadPrograms(fixedPrograms),renderer.shaderStatus());
    for(bool game:{false,true})for(const auto& pose:poses)run(pose,game,false);
    check(totalLit>10000&&totalBlocked>10000,"Moving-light room path failed to exercise both lit and occluded walls.");
    report["historicalDarkPixels"]=historicalDark;report["litPixels"]=totalLit;report["blockedPixels"]=totalBlocked;report["darkLitPixels"]=totalDark;report["brightBlockedPixels"]=totalLeak;report["passed"]=true;
    writeFileAtomically(output/"results.json",report.dump(2)+"\n");
    std::cout<<"Actual room: "<<room.instances.size()<<" draws, "<<room.oracle.triangles.size()<<" triangles; "<<poses.size()<<" moving-player poses per Scene/Game, bias=0/1; historical dark="<<historicalDark<<", repaired dark/leak="<<totalDark<<'/'<<totalLeak<<", lit/blocked="<<totalLit<<'/'<<totalBlocked<<"\n";
}
}
