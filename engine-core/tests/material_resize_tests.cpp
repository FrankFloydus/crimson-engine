#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include "resources/TextureMipCache.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Fixture {
    MaterialGraph graph;
    MaterialNodeId texture{},levels{},resize{};
    std::shared_ptr<DecodedImage> source=std::make_shared<DecodedImage>();
    Fixture(unsigned width,unsigned height) {
        addMaterialNode(graph,MaterialNodeKind::Output);
        texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(graph,texture)->texture.image.id="fixture:source";
        findMaterialNode(graph,texture)->texture.role=MaterialTextureRole::Data;
        levels=addMaterialNode(graph,MaterialNodeKind::Levels).id;
        resize=addMaterialNode(graph,MaterialNodeKind::Resize).id;
        connectMaterialNodesData(graph,texture,MaterialPin::RGBA,levels,MaterialPin::Left);
        connectMaterialNodesData(graph,levels,MaterialPin::Value,resize,MaterialPin::Left);
        source->width=width;source->height=height;source->rgba.resize(std::size_t(width)*height*4);
    }
    FloatImage bake() const {
        return bakeMaterialResizeImage(graph,resize,[&](const std::string& id)->std::shared_ptr<const DecodedImage>{
            check(id=="fixture:source","Resize requested an unrelated source.");return source;
        });
    }
};
void correctness() {
    Fixture f(2,1);f.source->rgba={0,0,0,0,255,255,255,255};
    auto& settings=findMaterialNode(f.graph,f.resize)->resize;
    settings.width=3;settings.height=1;settings.filter=MaterialResizeFilter::Linear;
    findMaterialNode(f.graph,f.levels)->image.gamma=2;
    const auto value=f.bake();
    check(value.width==3&&value.height==1&&value.rgba.size()==12,"Resize output dimensions or float layout are wrong.");
    check(std::abs(value.rgba[4]-.5f)<1e-5f,
        "Resize evaluated Levels after interpolation instead of resampling its completed upstream lattice.");
    check(std::abs(value.rgba[7]-.5f)<1e-5f,"Resize did not interpolate independent alpha.");
    f.source->rgba={255,0,0,0,0,0,255,0};findMaterialNode(f.graph,f.levels)->image.gamma=1;
    const auto transparent=f.bake();
    check(std::abs(transparent.rgba[4]-.5f)<1e-5f&&std::abs(transparent.rgba[6]-.5f)<1e-5f&&transparent.rgba[7]==0,
        "Resize discarded or premultiplied RGB at zero alpha.");
    Fixture color(2,1);color.source->rgba={128,128,128,255,128,128,128,255};
    findMaterialNode(color.graph,color.texture)->texture.role=MaterialTextureRole::Color;
    findMaterialNode(color.graph,color.resize)->resize={4,2,MaterialResizeFilter::Linear};
    const auto linear=color.bake();
    check(std::abs(linear.rgba[0]-.2158605f)<1e-5f,"Color resize failed source sRGB decoding or encoded its linear result twice.");
    Fixture checker(64,64);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        const auto i=(std::size_t(y)*64+x)*4;const auto c=std::uint8_t(((x+y)&1)?255:0);
        checker.source->rgba[i]=checker.source->rgba[i+1]=checker.source->rgba[i+2]=c;checker.source->rgba[i+3]=255;
    }
    findMaterialNode(checker.graph,checker.resize)->resize={7,5,MaterialResizeFilter::Area};
    const auto reduced=checker.bake();
    for(std::size_t i=0;i<reduced.rgba.size();i+=4)
        check(std::abs(reduced.rgba[i]-.5f)<.01f&&std::abs(reduced.rgba[i+3]-1)<1e-5f,
            "Area reduction aliased a checkerboard or changed alpha.");
    MaterialGraph constant;addMaterialNode(constant,MaterialNodeKind::Output);
    const auto c=addMaterialNode(constant,MaterialNodeKind::Color).id,r=addMaterialNode(constant,MaterialNodeKind::Resize).id;
    findMaterialNode(constant,c)->color={-2,4,.5f,.25f};findMaterialNode(constant,r)->resize={2,3,MaterialResizeFilter::Linear};
    connectMaterialNodesData(constant,c,MaterialPin::RGBA,r,MaterialPin::Left);
    const auto hdr=bakeMaterialResizeImage(constant,r,{});
    check(hdr.rgba[0]==-2&&hdr.rgba[1]==4&&hdr.rgba[3]==.25f,"Resize clamped finite HDR or altered alpha.");
    std::stop_source stop;stop.request_stop();bool canceled{};
    try{(void)bakeMaterialResizeImage(constant,r,{},stop.get_token());}catch(const std::exception&){canceled=true;}
    check(canceled,"Canceled Resize returned a completed image.");
}
void benchmark(const std::filesystem::path& file) {
    Fixture f(256,256);
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
        const auto i=(std::size_t(y)*256+x)*4;
        f.source->rgba[i]=std::uint8_t(x);f.source->rgba[i+1]=std::uint8_t(y);
        f.source->rgba[i+2]=std::uint8_t((x+y)/2);f.source->rgba[i+3]=255;
    }
    findMaterialNode(f.graph,f.levels)->image.gamma=1.285f;
    nlohmann::json report{{"scope","single-thread CPU Resize generation from decoded 256x256 source plus Levels, followed by full RGBA16F mip preparation; excludes image decode, GPU upload and warm-cache frame time"},{"samples",5},{"phases",nlohmann::json::array()}};
    for(const auto extent:{512u,1024u,2048u}) {
        findMaterialNode(f.graph,f.resize)->resize={extent,extent,MaterialResizeFilter::Linear};
        (void)f.bake();std::vector<double> times,mipTimes,totalTimes;double checksum{};std::size_t gpuBytes{};
        for(unsigned sample=0;sample<5;++sample) {
            const auto start=std::chrono::steady_clock::now();auto image=f.bake();const auto baked=std::chrono::steady_clock::now();
            checksum+=image.rgba[(std::size_t(extent/2)*extent+extent/2)*4];
            const auto mips=streaming::generatedFloatTextureMipCache(1,std::move(image));const auto finished=std::chrono::steady_clock::now();
            times.push_back(std::chrono::duration<double,std::milli>(baked-start).count());
            mipTimes.push_back(std::chrono::duration<double,std::milli>(finished-baked).count());
            totalTimes.push_back(std::chrono::duration<double,std::milli>(finished-start).count());
            gpuBytes=0;for(const auto& level:mips.levels)gpuBytes+=std::size_t(level.bytes);
        }
        std::sort(times.begin(),times.end());std::sort(mipTimes.begin(),mipTimes.end());std::sort(totalTimes.begin(),totalTimes.end());
        report["phases"].push_back({{"width",extent},{"height",extent},{"median_ms",times[2]},{"max_ms",times.back()},
            {"rgba16f_mip_median_ms",mipTimes[2]},{"total_worker_median_ms",totalTimes[2]},{"gpu_texel_bytes_full_mips",gpuBytes},
            {"rgba32_cpu_output_bytes",std::size_t(extent)*extent*16},{"checksum",checksum},{"raw_sorted_ms",times},
            {"raw_sorted_mip_ms",mipTimes},{"raw_sorted_total_ms",totalTimes}});
    }
    std::filesystem::create_directories(file.parent_path());std::ofstream out(file);out<<report.dump(2)<<'\n';check(bool(out),"Cannot write Resize CPU benchmark.");
    std::cout<<report.dump(2)<<'\n';
}
}
int main(int argc,char** argv) {
    try{correctness();if(argc==3&&std::string(argv[1])=="--benchmark")benchmark(argv[2]);std::cout<<"Material Resize float image reference checks passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
