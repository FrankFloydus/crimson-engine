#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,float tolerance,const char* message){if(std::abs(actual-expected)>tolerance)throw std::runtime_error(std::string(message)+" actual="+std::to_string(actual)+" expected="+std::to_string(expected));}
int repeat(int v,int n){const int r=v%n;return r<0?r+n:r;}
struct Fixture {
    MaterialGraph graph;
    MaterialNodeId texture{},root{};
    std::shared_ptr<DecodedImage> image=std::make_shared<DecodedImage>();
    mutable unsigned resolves{};
    Fixture(unsigned width,unsigned height,MaterialNodeKind kind,MaterialPin output=MaterialPin::R) {
        addMaterialNode(graph,MaterialNodeKind::Output);
        texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
        auto* source=findMaterialNode(graph,texture);source->texture.image.id="fixture";source->texture.role=MaterialTextureRole::Data;
        root=addMaterialNode(graph,kind).id;connectMaterialNodesData(graph,texture,output,root,MaterialPin::Left);
        image->width=width;image->height=height;image->rgba.resize(static_cast<std::size_t>(width)*height*4);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto i=(static_cast<std::size_t>(y)*width+x)*4;image->rgba[i]=static_cast<unsigned char>((x*53+y*29)%256);image->rgba[i+1]=static_cast<unsigned char>((x*7+y*91)%256);image->rgba[i+2]=static_cast<unsigned char>((x*19+y*11)%256);image->rgba[i+3]=static_cast<unsigned char>((x*31+y*13)%256);}
    }
    FloatImage bake(MaterialImageBakeMetrics* metrics=nullptr,std::stop_token stop={}) const {
        return bakeMaterialCachedImage(graph,root,[&](const std::string& name)->std::shared_ptr<const DecodedImage>{check(name=="fixture","Unexpected image dependency.");++resolves;return image;},stop,metrics);
    }
    MaterialNode& node(){return *findMaterialNode(graph,root);}
};
std::vector<float> morphologyReference(std::vector<float> source,unsigned width,unsigned height,float amount) {
    std::vector<float> target(source.size());
    for(unsigned axis=0;axis<2;++axis) {
        const int length=static_cast<int>(axis?height:width);const float radius=std::abs(amount)*length;const int lower=static_cast<int>(std::floor(radius));const float fraction=radius-lower;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto envelope=[&](int support){float value=source[static_cast<std::size_t>(y)*width+x];for(int d=-support;d<=support;++d){const auto other=source[static_cast<std::size_t>(repeat(static_cast<int>(y)+(axis?d:0),height))*width+repeat(static_cast<int>(x)+(axis?0:d),width)];value=amount>=0?std::max(value,other):std::min(value,other);}return value;};
            const float a=envelope(lower),b=envelope(lower+1);target[static_cast<std::size_t>(y)*width+x]=a+(b-a)*fraction;
        }
        source.swap(target);
    }
    return source;
}
void morphology() {
    for(float amount:{-.128f,-.071f,0.f,.071f,.128f})for(unsigned channel=0;channel<4;++channel) {
        constexpr MaterialPin channels[]{MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A};
        Fixture fixture(17,11,MaterialNodeKind::GrowShrink,channels[channel]);fixture.node().atomic.amount=amount;
        fixture.image->rgba[0]=255;fixture.image->rgba[3]=0;fixture.image->rgba[(17*10+16)*4+1]=0;
        const auto actual=fixture.bake();
        std::vector<float> source(17*11);for(std::size_t i=0;i<source.size();++i)source[i]=fixture.image->rgba[i*4+channel]/255.f;
        const auto expected=morphologyReference(source,17,11,amount);
        for(std::size_t i=0;i<source.size();++i){near(actual.rgba[i*4],expected[i],2e-6f,"Periodic fractional morphology differs from independent brute force.");near(actual.rgba[i*4+1],actual.rgba[i*4],0,"Morphology scalar output is not broadcast.");near(actual.rgba[i*4+3],1,0,"Morphology scalar alpha is not opaque.");}
    }
}
void gaussian() {
    for(float radius:{.5f,1.7f,5.f,11.9f,12.01f,16.f,24.7f,65.f,130.f}) {
        Fixture f(1024,1,MaterialNodeKind::Blur);f.node().atomic.amount=radius/1024;
        std::fill(f.image->rgba.begin(),f.image->rgba.end(),0);f.image->rgba[0]=255;
        const auto output=f.bake();const int support=static_cast<int>(std::ceil(radius));const double sigma=radius/3;
        std::vector<double> expected(1024);double sum=0;
        for(int offset=-support;offset<=support;++offset){const double value=std::exp(-.5*offset*offset/(sigma*sigma));expected[repeat(offset,1024)]+=value;sum+=value;}
        double actualSum=0,l1=0;for(unsigned x=0;x<1024;++x){actualSum+=output.rgba[x*4];l1+=std::abs(output.rgba[x*4]-expected[x]/sum);}
        near(static_cast<float>(actualSum),1,2e-6f,"Gaussian impulse energy is not normalized.");
        check(l1<=(support<=12?2e-6:.08),"Gaussian approximation exceeds declared representative impulse L1 bound.");
        for(unsigned x=0;x<1024;++x)near(output.rgba[x*4+3],1,1e-6f,"Scalar Gaussian produced nonopaque alpha.");
    }
    Fixture constant(37,19,MaterialNodeKind::Blur,MaterialPin::RGBA);constant.node().atomic.amount=.128f;
    for(std::size_t i=0;i<constant.image->rgba.size();i+=4){constant.image->rgba[i]=200;constant.image->rgba[i+1]=64;constant.image->rgba[i+2]=155;constant.image->rgba[i+3]=0;}
    const auto result=constant.bake();
    for(std::size_t i=0;i<result.rgba.size();i+=4){near(result.rgba[i],200/255.f,1e-6f,"Blur lost transparent RGB or constant energy.");near(result.rgba[i+1],64/255.f,1e-6f,"Non-square Blur normalization failed.");near(result.rgba[i+3],0,1e-6f,"Blur premultiplied independent alpha.");}
}
void fractionalBroadBlurResponse() {
    Fixture fixture(1024,1,MaterialNodeKind::Blur,MaterialPin::RGBA);
    std::fill(fixture.image->rgba.begin(),fixture.image->rgba.end(),0);
    fixture.image->rgba[0]=255;fixture.image->rgba[1]=127;fixture.image->rgba[2]=63;fixture.image->rgba[3]=191;
    for(const float radius:{13.1f,20.f,100.f}) {
        fixture.node().atomic.amount=radius/1024;const auto first=fixture.bake();
        fixture.node().atomic.amount=(radius+.1f)/1024;const auto next=fixture.bake();
        double difference=0,energy=0,variance=0;
        for(unsigned x=0;x<1024;++x) {
            const auto pixel=x*4;const int distance=std::min(x,1024-x);
            difference+=std::abs(first.rgba[pixel]-next.rgba[pixel]);energy+=next.rgba[pixel];variance+=distance*distance*next.rgba[pixel];
            near(next.rgba[pixel+1],next.rgba[pixel]*127/255,2e-8f,"Fractional broad blur changed RGB channel proportions.");
            near(next.rgba[pixel+3],next.rgba[pixel]*191/255,2e-8f,"Fractional broad blur changed straight-alpha convolution.");
        }
        check(difference>1e-5,"Broad Blur Amount changes still produce identical pixels.");
        near(static_cast<float>(energy),1,2e-6f,"Fractional broad blur failed impulse energy normalization.");
        near(static_cast<float>(variance),((radius+.1f)/3)*((radius+.1f)/3),.001f,"Fractional box variance does not follow the authored blur amount.");
    }
}
double imageDifference(const FloatImage& first,const FloatImage& second) {
    check(first.width==second.width&&first.height==second.height,"Property test changed the image extent unexpectedly.");
    double sum=0;for(std::size_t i=0;i<first.rgba.size();++i)sum+=std::abs(first.rgba[i]-second.rgba[i]);return sum/first.rgba.size();
}
FloatImage previewFixture(const Fixture& fixture) {
    return bakeMaterialGraphImagePreview(fixture.graph,{fixture.root,MaterialPin::Value},[&](const std::string&)->std::shared_ptr<const DecodedImage>{return fixture.image;},{fixture.image->width,fixture.image->height,0});
}
void atomicPropertyPixels() {
    // Test pixels and expected direction/identity behavior, not just signatures.
    // Asymmetric images avoid hiding a real effect behind rotational symmetry,
    // uniform inputs, clipping or an unrelated inactive material output.
    for(const auto kind:{MaterialNodeKind::MaskAdjust,MaterialNodeKind::HistogramScan}) {
        Fixture fixture(31,19,kind);fixture.node().atomic.coverage=.5f;fixture.node().atomic.contrast=0;
        const auto identity=previewFixture(fixture);
        for(std::size_t i=0;i<identity.rgba.size();i+=4)near(identity.rgba[i],fixture.image->rgba[i]/255.f,1e-6f,"Neutral mask controls do not preserve the source.");
        fixture.node().atomic.coverage=.62f;const auto covered=previewFixture(fixture);
        check(imageDifference(identity,covered)>.01,"Mask Coverage did not alter actual output pixels.");
        for(std::size_t i=0;i<identity.rgba.size();i+=4)check(covered.rgba[i]+1e-6f>=identity.rgba[i],"Higher Coverage removed white mask coverage.");
        fixture.node().atomic.coverage=.5f;fixture.node().atomic.contrast=.7f;const auto contrasted=previewFixture(fixture);
        check(imageDifference(identity,contrasted)>.01,"Mask Contrast did not alter actual output pixels.");
        for(std::size_t i=0;i<identity.rgba.size();i+=4)check(std::abs(contrasted.rgba[i]-.5f)+1e-6f>=std::abs(identity.rgba[i]-.5f),"Higher mask Contrast moved values toward neutral gray.");
        fixture.node().atomic.coverage=0;const auto empty=previewFixture(fixture);fixture.node().atomic.coverage=1;const auto full=previewFixture(fixture);
        for(std::size_t i=0;i<empty.rgba.size();i+=4){near(empty.rgba[i],0,0,"Zero Coverage must produce a black mask.");near(full.rgba[i],1,0,"Full Coverage must produce a white mask.");}
    }
    Fixture warp(43,29,MaterialNodeKind::DirectionalWarp,MaterialPin::RGBA);warp.node().atomic.amount=.023f;warp.node().right={MaterialValueType::Scalar,{.7f,.7f,.7f,.7f}};
    const auto firstWarp=warp.bake();warp.node().atomic.amount=.051f;
    check(imageDifference(firstWarp,warp.bake())>.01,"Warp Amount did not alter the completed source image.");
    warp.node().atomic.amount=.023f;warp.node().atomic.angle=71;
    check(imageDifference(firstWarp,warp.bake())>.01,"Warp Angle did not rotate the sampling displacement.");
    warp.node().right={MaterialValueType::Scalar,{0,0,0,0}};const auto zeroMask=warp.bake();warp.node().atomic.amount=0;const auto zeroAmount=warp.bake();
    check(imageDifference(zeroMask,zeroAmount)==0,"Zero Warp mask does not match zero Amount identity.");
    const auto mask=addMaterialNode(warp.graph,MaterialNodeKind::HistogramScan).id;findMaterialNode(warp.graph,mask)->atomic.contrast=1;
    connectMaterialNodesData(warp.graph,warp.texture,MaterialPin::R,mask,MaterialPin::Left);connectMaterialNodesData(warp.graph,mask,MaterialPin::Value,warp.root,MaterialPin::Right);
    warp.node().atomic.amount=.051f;const auto masked=warp.bake();double changed=0;
    for(std::size_t i=0;i<masked.rgba.size();i+=4)for(unsigned channel=0;channel<4;++channel) {
        if(warp.image->rgba[i]<128)near(masked.rgba[i+channel],zeroAmount.rgba[i+channel],0,"Warp resampled a pixel excluded by a zero mask.");
        else changed+=std::abs(masked.rgba[i+channel]-zeroAmount.rgba[i+channel]);
    }
    check(changed>1,"Warp zero-mask shortcut also bypassed pixels with nonzero masks.");
    Fixture transform(43,29,MaterialNodeKind::ImageTransform,MaterialPin::RGBA);const auto unmapped=previewFixture(transform);
    const auto defaultMapping=transform.node().uv;
    transform.node().uv.scale={1.37f,.71f,1};check(imageDifference(unmapped,previewFixture(transform))>.01,"Image Transform Scale is ignored by node preview pixels.");
    transform.node().uv=defaultMapping;transform.node().uv.offset={.13f,.21f,0};check(imageDifference(unmapped,previewFixture(transform))>.01,"Image Transform Offset is ignored by node preview pixels.");
    transform.node().uv=defaultMapping;transform.node().uv.rotation[2]=37;check(imageDifference(unmapped,previewFixture(transform))>.01,"Image Transform Rotation is ignored by node preview pixels.");
    Fixture direction(37,23,MaterialNodeKind::DirectionSelect,MaterialPin::RGB);direction.node().atomic.direction=0;const auto positiveX=previewFixture(direction);
    for(unsigned axis=1;axis<6;++axis) {direction.node().atomic.direction=axis;check(imageDifference(positiveX,previewFixture(direction))>.01,"Direction Select axis did not change the normal component mask.");}
    direction.node().atomic.direction=0;direction.node().atomic.bothDirections=true;const auto both=previewFixture(direction);
    check(imageDifference(positiveX,both)>.01,"Direction Select Both directions did not include negative normals.");
    direction.node().atomic.direction=1;check(imageDifference(both,previewFixture(direction))==0,"Both directions incorrectly depends on the axis sign.");
    for(const bool height:{true,false}) {
        Fixture fixture(43,29,height?MaterialNodeKind::HeightOcclusion:MaterialNodeKind::NormalOcclusion,height?MaterialPin::R:MaterialPin::RGB);
        for(unsigned y=0;y<29;++y)for(unsigned x=0;x<43;++x) {
            const auto i=(y*43+x)*4;
            if(height)fixture.image->rgba[i]=static_cast<unsigned char>(120+53*std::sin(x*.23)+37*std::cos(y*.31)+19*std::sin((x+y)*.17));
            else {const double nx=.35*std::sin(y*.43)+.2*std::cos(x*.13),ny=.3*std::cos(x*.37)+.25*std::sin(y*.19),nz=std::sqrt(1-nx*nx-ny*ny);fixture.image->rgba[i]=static_cast<unsigned char>((nx+1)*127.5);fixture.image->rgba[i+1]=static_cast<unsigned char>((ny+1)*127.5);fixture.image->rgba[i+2]=static_cast<unsigned char>((nz+1)*127.5);}
        }
        fixture.node().atomic.radius=.08f;fixture.node().atomic.depth=.08f;fixture.node().atomic.strength=.8f;const auto baseSettings=fixture.node().atomic;
        const auto baseline=fixture.bake();fixture.node().atomic.strength=1.6f;const auto stronger=fixture.bake();
        check(imageDifference(baseline,stronger)>1e-5,"Occlusion Strength did not change a non-flat image.");
        for(std::size_t i=0;i<baseline.rgba.size();i+=4)check(stronger.rgba[i]<=baseline.rgba[i]+1e-6f,"Higher Occlusion Strength brightened visibility.");
        fixture.node().atomic=baseSettings;fixture.node().atomic.depth=.19f;check(imageDifference(baseline,fixture.bake())>1e-5,"Occlusion Depth did not change a non-flat image.");
        fixture.node().atomic=baseSettings;fixture.node().atomic.radius=.19f;check(imageDifference(baseline,fixture.bake())>1e-5,"Occlusion Radius did not change a non-flat image.");
        if(!height){fixture.node().atomic=baseSettings;fixture.node().image.normalFlipY=true;check(imageDifference(baseline,fixture.bake())>1e-5,"Normal Occlusion Flip Y did not change normal interpretation.");}
        for(unsigned field=0;field<3;++field) {fixture.node().atomic=baseSettings;if(field==0)fixture.node().atomic.strength=0;else if(field==1)fixture.node().atomic.radius=0;else fixture.node().atomic.depth=0;const auto clear=fixture.bake();for(std::size_t i=0;i<clear.rgba.size();i+=4)near(clear.rgba[i],1,0,"A zero Occlusion control did not clear the effect.");}
    }
}
void warpAndCompletedTransform() {
    Fixture warp(4,2,MaterialNodeKind::DirectionalWarp);warp.node().atomic.amount=.25f;warp.node().atomic.angle=0;warp.node().right={MaterialValueType::Scalar,{1,1,1,1}};
    const auto output=warp.bake();for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x)near(output.rgba[(y*4+x)*4],warp.image->rgba[(y*4+(x+1)%4)*4]/255.f,1e-6f,"Warp angle0 did not move +U with repeat addressing.");
    warp.node().atomic.amount=0;const auto identity=warp.bake();for(unsigned i=0;i<8;++i)near(identity.rgba[i*4],warp.image->rgba[i*4]/255.f,1e-6f,"Zero Warp is not identity.");

    Fixture transform(2,1,MaterialNodeKind::ImageTransform);transform.image->rgba={0,0,0,255,255,255,255,255};
    const auto levels=addMaterialNode(transform.graph,MaterialNodeKind::Levels).id;findMaterialNode(transform.graph,levels)->image.gamma=2;
    connectMaterialNodesData(transform.graph,transform.texture,MaterialPin::R,levels,MaterialPin::Left);
    connectMaterialNodesData(transform.graph,levels,MaterialPin::Value,transform.root,MaterialPin::Left);
    transform.node().uv.offset={.25f,0,0};const auto originalRoot=transform.root;
    // The transform's own binding is unmapped and can be reused for live edits.
    auto raw=transform.bake();near(raw.rgba[0],0,1e-6f,"Outer Image Transform applied runtime mapping during bake.");near(raw.rgba[4],1,1e-6f,"Outer completed image is incorrect.");
    const auto before=materialGraphValueHashData(materialImageBakeContentGraph(transform.graph,originalRoot),MaterialShading::StandardPbr,MaterialBlend::Opaque,originalRoot);
    transform.node().uv.offset={.125f,.25f,0};const auto after=materialGraphValueHashData(materialImageBakeContentGraph(transform.graph,originalRoot),MaterialShading::StandardPbr,MaterialBlend::Opaque,originalRoot);
    check(before==after,"Mapping-only Image Transform edit changed content key.");transform.node().uv.offset={.25f,0,0};
    transform.root=addMaterialNode(transform.graph,MaterialNodeKind::Resize).id;findMaterialNode(transform.graph,transform.root)->resize={2,1,MaterialResizeFilter::Linear};
    connectMaterialNodesData(transform.graph,originalRoot,MaterialPin::Value,transform.root,MaterialPin::Left);
    auto mapped=transform.bake();near(mapped.rgba[0],.5f,1e-6f,"Nested Image Transform remapped a source before Levels rather than its completed image.");near(mapped.rgba[4],.5f,1e-6f,"Nested completed-image transform did not repeat.");
    findMaterialNode(transform.graph,originalRoot)->uv.mode=MaterialUvMode::UV1;bool rejected=false;try{(void)transform.bake();}catch(const std::exception&){rejected=true;}check(rejected,"Nested UV1 Image Transform was silently baked.");
    // Structural validation also checks dormant cached branches. Remove the
    // intentionally invalid nested consumer before testing a valid outer use.
    check(removeMaterialNode(transform.graph,transform.root),"Cannot remove nested transform test consumer.");
    transform.root=originalRoot;(void)transform.bake(); // Outer UV1 is valid runtime mapping.
}
// Independent direct-tap reference: no axis/index tables and unconditional
// normal normalization. This guards the optimized sampler and slope shortcut.
std::vector<float> directAoReference(const DecodedImage& image,const MaterialNode& node,bool height) {
    const unsigned width=image.width,heightPixels=image.height;std::vector<float> result(static_cast<std::size_t>(width)*heightPixels);
    const double ru=std::max(static_cast<double>(node.atomic.radius),1./width),rv=std::max(static_cast<double>(node.atomic.radius),1./heightPixels),step=std::max(ru,rv)/4;
    for(unsigned y=0;y<heightPixels;++y)for(unsigned x=0;x<width;++x) {
        const auto at=[&](double u,double v,unsigned channel) {
            const double px=x+u*width,py=y+v*heightPixels;const int left=static_cast<int>(std::floor(px)),top=static_cast<int>(std::floor(py));const double tx=px-left,ty=py-top;
            const auto tap=[&](int xx,int yy){return image.rgba[(static_cast<std::size_t>(repeat(yy,heightPixels))*width+repeat(xx,width))*4+channel]/255.;};
            return (tap(left,top)*(1-tx)+tap(left+1,top)*tx)*(1-ty)+(tap(left,top+1)*(1-tx)+tap(left+1,top+1)*tx)*ty;
        };
        const auto slope=[&](double u,double v){double nx=2*at(u,v,0)-1,ny=2*at(u,v,1)-1,nz=2*at(u,v,2)-1;if(std::abs(nx)<=.00392257)nx=0;if(std::abs(ny)<=.00392257)ny=0;nx*=node.image.normalStrength;ny*=node.image.normalStrength*(node.image.normalFlipY?-1:1);const double length=std::sqrt(nx*nx+ny*ny+nz*nz);if(length<=1.e-6)return std::array<double,2>{0,0};nx/=length;ny/=length;nz/=length;return std::array<double,2>{-nx/std::max(nz,.1),-ny/std::max(nz,.1)};};
        const auto centerSlope=height?std::array<double,2>{(at(ru,0,0)-at(-ru,0,0))/(2*ru),(at(0,rv,0)-at(0,-rv,0))/(2*rv)}:slope(0,0);
        const double center=at(0,0,0);double occlusion=0;
        for(unsigned direction=0;direction<8;++direction) {
            const double angle=direction*3.14159265358979323846/4,dx=std::cos(angle),dy=std::sin(angle),projected=centerSlope[0]*dx+centerSlope[1]*dy,tangent=node.atomic.depth*projected;
            double horizon=tangent,integral=0,previous=projected;
            for(unsigned j=1;j<=4;++j) {
                const double distance=step*j;double candidate;
                if(height)candidate=(at(dx*distance,dy*distance,0)-center)*node.atomic.depth/distance;
                else {const auto normalSlope=slope(dx*distance,dy*distance);const double current=normalSlope[0]*dx+normalSlope[1]*dy;integral+=(previous+current)*.5*step;previous=current;candidate=integral*node.atomic.depth/distance;}
                horizon=std::max(horizon,candidate);
            }
            const auto sine=[](double value){return value/std::sqrt(1+value*value);};occlusion+=std::max(0.,sine(horizon)-sine(tangent));
        }
        result[static_cast<std::size_t>(y)*width+x]=static_cast<float>(std::clamp(1-node.atomic.strength*occlusion/8,0.,1.));
    }
    return result;
}
void occlusionReferenceParity() {
    for(bool height:{true,false}) {
        Fixture fixture(19,13,height?MaterialNodeKind::HeightOcclusion:MaterialNodeKind::NormalOcclusion,height?MaterialPin::R:MaterialPin::RGB);
        fixture.node().atomic.radius=.17f;fixture.node().atomic.depth=.12f;fixture.node().atomic.strength=1.3f;
        fixture.node().image.normalStrength=.8f;fixture.node().image.normalFlipY=true;
        const auto reference=directAoReference(*fixture.image,fixture.node(),height);const auto actual=fixture.bake();
        for(std::size_t i=0;i<reference.size();++i)near(actual.rgba[i*4],reference[i],4e-5f,"Optimized AO differs from independent direct-tap normalized reference.");
    }
}

void occlusion() {
    Fixture height(65,49,MaterialNodeKind::HeightOcclusion);height.node().atomic.radius=.04f;height.node().atomic.depth=.1f;
    for(unsigned y=0;y<49;++y)for(unsigned x=0;x<65;++x)height.image->rgba[(y*65+x)*4]=static_cast<unsigned char>(60+x*2+y);
    const auto ramp=height.bake();for(unsigned y=5;y<44;++y)for(unsigned x=5;x<60;++x)near(ramp.rgba[(y*65+x)*4],1,2e-5f,"Tilted-plane interior incorrectly occludes against its own tangent.");
    for(unsigned y=0;y<49;++y)for(unsigned x=0;x<65;++x)height.image->rgba[(y*65+x)*4]=static_cast<unsigned char>(std::abs(static_cast<int>(x)-32)*7);
    const auto valley=height.bake();check(valley.rgba[(24*65+32)*4]<.99f,"Height Occlusion did not darken a valley.");
    height.node().atomic.strength=0;const auto neutral=height.bake();for(std::size_t i=0;i<neutral.rgba.size();i+=4)near(neutral.rgba[i],1,0,"Zero AO strength must be exactly clear.");
    Fixture normal(19,13,MaterialNodeKind::NormalOcclusion,MaterialPin::RGB);normal.node().atomic.radius=.08f;
    const auto scalar=addMaterialNode(normal.graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(normal.graph,scalar)->scalar=.5f;
    check(!materialCanConnect(normal.graph,scalar,MaterialPin::Value,normal.root,MaterialPin::Left),"Encoded Normal Occlusion unexpectedly accepted an untyped Scalar normal.");
    const auto promote=addMaterialNode(normal.graph,MaterialNodeKind::ToColor).id;
    connectMaterialNodesData(normal.graph,scalar,MaterialPin::Value,promote,MaterialPin::Left);
    const auto promotedLink=connectMaterialNodesData(normal.graph,promote,MaterialPin::RGB,normal.root,MaterialPin::Left);
    const auto promoted=normal.bake();for(std::size_t i=0;i<promoted.rgba.size();i+=4)near(promoted.rgba[i],1,0,"Promoted constant scalar RGB normals must remain clear.");
    check(removeMaterialLink(normal.graph,promotedLink),"Cannot restore encoded normal image fixture.");
    connectMaterialNodesData(normal.graph,normal.texture,MaterialPin::RGB,normal.root,MaterialPin::Left);
    for(std::size_t i=0;i<normal.image->rgba.size();i+=4){normal.image->rgba[i]=160;normal.image->rgba[i+1]=90;normal.image->rgba[i+2]=240;}
    const auto normalPlane=normal.bake();for(std::size_t i=0;i<normalPlane.rgba.size();i+=4)near(normalPlane.rgba[i],1,2e-6f,"Uniform tilted normals must remain unoccluded.");
    // Existing Height to Normal encodes negative Y for height increasing in
    // +UV.v. A valley normal field must therefore agree with height analysis.
    for(unsigned y=0;y<13;++y)for(unsigned x=0;x<19;++x){const auto i=(y*19+x)*4;normal.image->rgba[i]=128;normal.image->rgba[i+1]=static_cast<unsigned char>(y<6?220:y>6?35:128);normal.image->rgba[i+2]=200;}
    normal.node().atomic.radius=.15f;
    const auto normalValley=normal.bake();check(normalValley.rgba[(6*19+9)*4]<.99f,"Normal Occlusion Y convention disagrees with Height to Normal on a valley.");
    normal.node().image.normalFlipY=true;const auto invertedValley=normal.bake();check(invertedValley.rgba[(6*19+9)*4]>normalValley.rgba[(6*19+9)*4]+.01f,"Normal Occlusion Flip Y did not change normal slope interpretation.");normal.node().image.normalFlipY=false;
    for(std::size_t i=0;i<normal.image->rgba.size();i+=4){normal.image->rgba[i]=255;normal.image->rgba[i+1]=0;normal.image->rgba[i+2]=127;}
    const auto grazing=normal.bake();for(float value:grazing.rgba)check(std::isfinite(value),"Normal Occlusion generated nonfinite output at grazing Z.");
}
void sourcesTypesAndCancellation() {
    Fixture f(13,9,MaterialNodeKind::Blur,MaterialPin::RGBA);f.node().atomic.amount=0;
    const auto input=addMaterialNode(f.graph,MaterialNodeKind::ImageInput).id;
    auto* node=findMaterialNode(f.graph,input);node->color={.2f,.4f,.7f,.1f};node->atomic.enabled=false;
    connectMaterialNodesData(f.graph,f.texture,MaterialPin::RGBA,input,MaterialPin::Left);connectMaterialNodesData(f.graph,input,MaterialPin::RGBA,f.root,MaterialPin::Left);
    check(materialResizeImageSources(f.graph,f.root).empty(),"Disabled Image Input retained a source dependency.");const auto disabled=f.bake();check(f.resolves==0,"Disabled Image Input evaluated its hidden Base.");near(disabled.rgba[0],.2f,1e-6f,"Image Input did not use constant fallback.");
    node=findMaterialNode(f.graph,input);node->atomic.enabled=true;node->texture.image.id="fixture";node->texture.role=MaterialTextureRole::Data;
    const auto extent=materialCachedImageExtent(f.graph,f.root,[](const std::string&){return std::array<unsigned,2>{13,9};});check(extent==std::array<unsigned,2>{13,9},"Metadata lattice query ignored override dimensions.");const auto overrideImage=f.bake();near(overrideImage.rgba[0],f.image->rgba[0]/255.f,1e-6f,"Image Input override precedence failed.");
    const auto histogram=addMaterialNode(f.graph,MaterialNodeKind::HistogramScan).id,adjust=addMaterialNode(f.graph,MaterialNodeKind::MaskAdjust).id;
    connectMaterialNodesData(f.graph,input,MaterialPin::RGBA,histogram,MaterialPin::Left);connectMaterialNodesData(f.graph,histogram,MaterialPin::Value,adjust,MaterialPin::Left);connectMaterialNodesData(f.graph,adjust,MaterialPin::Value,f.root,MaterialPin::Left);
    findMaterialNode(f.graph,histogram)->atomic.coverage=.7f;findMaterialNode(f.graph,adjust)->atomic.coverage=.3f;
    const auto adjusted=f.bake();const auto a=findMaterialNode(f.graph,adjust)->atomic,h=findMaterialNode(f.graph,histogram)->atomic;near(adjusted.rgba[4],materialAtomicMaskAdjust(materialAtomicHistogramScan(f.image->rgba[4]/255.f,h),a),1e-6f,"New point nodes inside cached branch differ from canonical math.");
    std::stop_source stop;stop.request_stop();bool canceled=false;try{(void)f.bake(nullptr,stop.get_token());}catch(const std::exception& error){canceled=std::string(error.what()).find("cancel")!=std::string::npos;}check(canceled,"Canceled atomic bake published a completed image.");
    const auto procedural=addMaterialNode(f.graph,MaterialNodeKind::ProceduralTexture).id;findMaterialNode(f.graph,procedural)->procedural.width=32;findMaterialNode(f.graph,procedural)->procedural.height=16;f.root=procedural;
    const auto generated=f.bake();check(generated.width==32&&generated.height==16,"Procedural source ignored explicit raster resolution.");for(std::size_t i=0;i<generated.rgba.size();i+=4){near(generated.rgba[i],generated.rgba[i+1],0,"Procedural scalar is not broadcast consistently.");near(generated.rgba[i+3],1,0,"Procedural source altered alpha.");}
}
void sharedIntermediateAndDynamicRejection() {
    Fixture fixture(7,5,MaterialNodeKind::Blur,MaterialPin::RGBA);fixture.node().atomic.amount=0;
    const auto nested=addMaterialNode(fixture.graph,MaterialNodeKind::Blur).id,invert=addMaterialNode(fixture.graph,MaterialNodeKind::Invert).id,add=addMaterialNode(fixture.graph,MaterialNodeKind::Add).id;
    findMaterialNode(fixture.graph,nested)->atomic.amount=.128f;
    connectMaterialNodesData(fixture.graph,fixture.texture,MaterialPin::RGBA,nested,MaterialPin::Left);
    connectMaterialNodesData(fixture.graph,nested,MaterialPin::Value,invert,MaterialPin::Left);
    connectMaterialNodesData(fixture.graph,nested,MaterialPin::Value,add,MaterialPin::Left);
    connectMaterialNodesData(fixture.graph,invert,MaterialPin::Value,add,MaterialPin::Right);
    connectMaterialNodesData(fixture.graph,add,MaterialPin::Value,fixture.root,MaterialPin::Left);
    MaterialImageBakeMetrics metrics;const auto output=fixture.bake(&metrics);
    check(fixture.resolves==1&&metrics.boundaryCount==2,"A shared completed intermediate or source was evaluated more than once.");
    for(std::size_t i=0;i<output.rgba.size();i+=4)for(unsigned channel=0;channel<3;++channel)near(output.rgba[i+channel],1,1e-6f,"In-place point fusion corrupted a shared completed image.");
    Fixture dynamic(4,4,MaterialNodeKind::Blur);
    const auto scalar=addMaterialNode(dynamic.graph,MaterialNodeKind::Scalar).id,multiply=addMaterialNode(dynamic.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodesData(dynamic.graph,dynamic.texture,MaterialPin::R,multiply,MaterialPin::Left);
    connectMaterialNodesData(dynamic.graph,scalar,MaterialPin::Value,multiply,MaterialPin::Right);
    connectMaterialNodesData(dynamic.graph,multiply,MaterialPin::Value,dynamic.root,MaterialPin::Left);
    findMaterialNode(dynamic.graph,scalar)->kind=MaterialNodeKind::Time;
    bool rejected=false;try{(void)dynamic.bake();}catch(const std::exception& error){rejected=std::string(error.what()).find("Time")!=std::string::npos;}
    check(rejected,"A dynamic Time input was silently frozen into a cached image.");
}

void nodeImagePreviews() {
    // Nonlinear processing must happen on the completed authored source BEFORE
    // area reduction. Averaging first and then applying Levels is different.
    Fixture source(8,4,MaterialNodeKind::Blur,MaterialPin::RGBA);
    findMaterialNode(source.graph,source.texture)->texture.role=MaterialTextureRole::Color;source.node().atomic.amount=0;
    const auto levels=addMaterialNode(source.graph,MaterialNodeKind::Levels).id;findMaterialNode(source.graph,levels)->image.gamma=2;
    connectMaterialNodesData(source.graph,source.texture,MaterialPin::RGBA,levels,MaterialPin::Left);
    connectMaterialNodesData(source.graph,levels,MaterialPin::Value,source.root,MaterialPin::Left);
    const MaterialImagePreviewOptions small{1,1,1024*1024};MaterialImageBakeMetrics metrics;
    const auto resolver=[&](const std::string& name)->std::shared_ptr<const DecodedImage>{check(name=="fixture","Preview requested an unrelated source.");++source.resolves;return source.image;};
    const auto definition=serializeMaterialGraphData(source.graph);
    const auto preview=bakeMaterialGraphImagePreview(source.graph,{source.root,MaterialPin::Value},resolver,small,{},&metrics);
    check(preview.width==1&&preview.height==1&&source.resolves==1,"Preview did not use its requested display dimensions or shared source snapshot.");
    check(metrics.evaluatedPixels>=32,"Preview silently reevaluated a nonlinear source at display resolution.");
    std::array<double,4> expected{};
    for(std::size_t i=0;i<source.image->rgba.size();i+=4)for(unsigned channel=0;channel<4;++channel){const double value=source.image->rgba[i+channel]/255.;const double linear=channel==3?value:value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);expected[channel]+=(channel==3?linear:std::sqrt(linear))/32;}
    for(unsigned channel=0;channel<4;++channel)near(preview.rgba[channel],static_cast<float>(expected[channel]),2e-6f,"Preview did not area-reduce the completed nonlinear source or altered straight alpha.");
    check(serializeMaterialGraphData(source.graph)==definition,"Preview changed the authored graph.");
    check(materialGraphImagePreviewSources(source.graph,{source.root,MaterialPin::Value})==std::vector<std::string>{"fixture"},"Arbitrary static preview source enumeration is incorrect.");

    Fixture transparent(2,1,MaterialNodeKind::Blur,MaterialPin::RGBA);transparent.image->rgba={255,0,0,0,0,0,255,0};
    const auto raw=bakeMaterialGraphImagePreview(transparent.graph,{transparent.texture,MaterialPin::RGBA},[&](const std::string&)->std::shared_ptr<const DecodedImage>{return transparent.image;},{1,1,0});
    near(raw.rgba[0],.5f,1e-6f,"RGBA preview discarded transparent red.");near(raw.rgba[2],.5f,1e-6f,"RGBA preview discarded transparent blue.");near(raw.rgba[3],0,0,"RGBA preview premultiplied or checker-composited canonical pixels.");
    findMaterialNode(transparent.graph,transparent.texture)->texture.role=MaterialTextureRole::Color;transparent.image->rgba={128,128,128,255,128,128,128,255};
    const auto linear=bakeMaterialGraphImagePreview(transparent.graph,{transparent.texture,MaterialPin::RGB},[&](const std::string&)->std::shared_ptr<const DecodedImage>{return transparent.image;},{1,1,0});
    near(linear.rgba[0],.2158605f,1e-6f,"Preview failed Color-role decoding or applied display encoding inside the canonical evaluator.");

    MaterialGraph procedural;addMaterialNode(procedural,MaterialNodeKind::Output);const auto pattern=addMaterialNode(procedural,MaterialNodeKind::ProceduralTexture).id;
    auto* node=findMaterialNode(procedural,pattern);node->procedural.settings=material_procedural::default_settings(material_procedural::Pattern::Clouds1);node->procedural.settings.scale=.2f;node->procedural.width=node->procedural.height=1024;
    const MaterialImagePreviewOptions tile{128,128,64ull*1024*1024};
    const auto generated=bakeMaterialGraphImagePreview(procedural,{pattern,MaterialPin::Value},{},tile,{},&metrics);
    check(metrics.evaluatedPixels>=1024*1024,"Procedural preview did not evaluate its authored source lattice.");
    // Scale .2's lowest Clouds1 band still exists in a 1K source. Reevaluation
    // with a 128px footprint drops it and loses nonlinear-clamp energy.
    float largestEarlyDifference=0;
    for(unsigned py:{3u,29u,64u,110u})for(unsigned px:{0u,17u,43u,81u,127u}) {
        double sum=0;for(unsigned y=py*8;y<(py+1)*8;++y)for(unsigned x=px*8;x<(px+1)*8;++x)
            sum+=material_procedural::sample({(x+.5f)/1024,(y+.5f)/1024},node->procedural.settings,1.f/1024,1);
        const float actual=generated.rgba[(py*128+px)*4];near(actual,static_cast<float>(sum/64),4e-5f,"Clouds1 Scale .2 preview differs from area-filtered actual 1K source samples.");
        const float early=material_procedural::sample({(px+.5f)/128,(py+.5f)/128},node->procedural.settings,1.f/128,1);
        largestEarlyDifference=std::max(largestEarlyDifference,std::abs(actual-early));
    }
    check(largestEarlyDifference>1e-4f,"Clouds1 source-resolution regression failed to distinguish premature coarse reevaluation.");
    const auto vector=addMaterialNode(procedural,MaterialNodeKind::Vector2).id;findMaterialNode(procedural,vector)->vector2={.17f,.83f};
    const auto xy=bakeMaterialGraphImagePreview(procedural,{vector,MaterialPin::XY},{},{2,3,0});near(xy.rgba[0],.17f,0,"Vector2 preview lost X.");near(xy.rgba[1],.83f,0,"Vector2 preview lost Y.");near(xy.rgba[2],0,0,"Vector2 preview has nonzero blue.");near(xy.rgba[3],1,0,"Vector2 preview must be opaque.");
    while(procedural.nodes.size()<MaterialGraph::MaximumNodes)addMaterialNode(procedural,MaterialNodeKind::Scalar);
    (void)bakeMaterialGraphImagePreview(procedural,{vector,MaterialPin::XY},{},{2,3,0});
    check(procedural.nodes.size()==MaterialGraph::MaximumNodes,"Preview inserted an authored helper into a full graph.");

    Fixture mapped(2,1,MaterialNodeKind::ImageTransform);mapped.image->rgba={0,0,0,255,255,255,255,255};mapped.node().uv.offset={.25f,0,0};
    const auto mappedPreview=bakeMaterialGraphImagePreview(mapped.graph,{mapped.root,MaterialPin::Value},[&](const std::string&)->std::shared_ptr<const DecodedImage>{return mapped.image;},{2,1,0});near(mappedPreview.rgba[0],.5f,1e-6f,"Static Image Transform preview omitted the node's mapping.");
    mapped.node().uv.mode=MaterialUvMode::UV1;bool rejected=false;try{validateMaterialGraphImagePreview(mapped.graph,{mapped.root,MaterialPin::Value});}catch(const std::exception&){rejected=true;}check(rejected,"Preview silently replaced contextual Image Transform UV1 with UV0.");
    MaterialGraph dynamic;addMaterialNode(dynamic,MaterialNodeKind::Output);const auto time=addMaterialNode(dynamic,MaterialNodeKind::Time).id;
    rejected=false;try{validateMaterialGraphImagePreview(dynamic,{time,MaterialPin::Value});}catch(const std::exception& error){rejected=std::string(error.what()).find("Time")!=std::string::npos;}check(rejected,"Static preview froze a Time expression silently.");
    const auto mapping=addMaterialNode(dynamic,MaterialNodeKind::UV).id;rejected=false;try{validateMaterialGraphImagePreview(dynamic,{mapping,MaterialPin::UV});}catch(const std::exception&){rejected=true;}check(rejected,"Static image preview accepted a Mapping output as color.");
    std::stop_source stop;stop.request_stop();rejected=false;try{(void)bakeMaterialGraphImagePreview(source.graph,{source.root,MaterialPin::Value},resolver,small,stop.get_token());}catch(const std::exception& error){rejected=std::string(error.what()).find("cancel")!=std::string::npos;}check(rejected,"Canceled preview published pixels.");
}

void benchmark(const std::filesystem::path& file) {
    nlohmann::json report{{"scope","CPU static material image generation. Excludes caller-owned RGBA8 decode, asynchronous host cache, GPU uploads, resident output generations and frame rendering. Each cold invocation uses a new evaluator; warm-cache behavior is owned and tested by ResourceManager."},{"samples",nlohmann::json::array()}};
    for(unsigned extent:{2048u,4096u})for(auto kind:{MaterialNodeKind::Blur,MaterialNodeKind::GrowShrink,MaterialNodeKind::DirectionalWarp,MaterialNodeKind::HeightOcclusion,MaterialNodeKind::NormalOcclusion}) {
        Fixture fixture(extent,extent,kind,kind==MaterialNodeKind::NormalOcclusion?MaterialPin::RGB:MaterialPin::RGBA);fixture.node().atomic.amount=kind==MaterialNodeKind::DirectionalWarp?.02f:.128f;
        fixture.node().right={MaterialValueType::Scalar,{.5f,.5f,.5f,.5f}};MaterialImageBakeMetrics metrics;
        const auto begin=std::chrono::steady_clock::now();const auto image=fixture.bake(&metrics);const auto end=std::chrono::steady_clock::now();
        report["samples"].push_back({{"kind",materialNodeKindName(kind)},{"width",extent},{"height",extent},{"cold_ms",std::chrono::duration<double,std::milli>(end-begin).count()},{"peak_temporary_image_bytes",metrics.peakTemporaryBytes},{"rgba8_input_bytes_excluded",fixture.image->rgba.size()},{"public_rgba32f_output_bytes",image.rgba.size()*sizeof(float)},{"source_resolves",fixture.resolves},{"evaluated_pixels",metrics.evaluatedPixels},{"boundaries",metrics.boundaryCount},{"checksum",image.rgba[image.rgba.size()/2]}});
        check(metrics.peakTemporaryBytes<=512ull*1024*1024,"4K spatial operation exceeded its image allocation budget.");
    }
    Fixture cancellation(4096,4096,MaterialNodeKind::Blur,MaterialPin::RGBA);cancellation.node().atomic.amount=.128f;std::stop_source stop;
    const auto begin=std::chrono::steady_clock::now();std::jthread request([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));stop.request_stop();});bool canceled=false;
    try{(void)cancellation.bake(nullptr,stop.get_token());}catch(const std::exception& error){canceled=std::string(error.what()).find("cancel")!=std::string::npos;}const auto end=std::chrono::steady_clock::now();check(canceled,"Live canceled 4K generation unexpectedly completed.");report["cancel_total_ms"]=std::chrono::duration<double,std::milli>(end-begin).count();
    std::filesystem::create_directories(file.parent_path());std::ofstream output(file);output<<report.dump(2)<<'\n';check(bool(output),"Unable to write image bake benchmark.");std::cout<<report.dump(2)<<'\n';
}
}
int main(int argc,char** argv) {
    try{morphology();gaussian();fractionalBroadBlurResponse();atomicPropertyPixels();warpAndCompletedTransform();occlusionReferenceParity();occlusion();sourcesTypesAndCancellation();sharedIntermediateAndDynamicRejection();nodeImagePreviews();if(argc==3&&std::string(argv[1])=="--benchmark")benchmark(argv[2]);std::cout<<"Atomic static image DAG checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
