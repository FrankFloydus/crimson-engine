#include "TextureStreamer.h"
#include "ImageDecoder.h"
#include "assets/MaterialAsset.h"
#include "assets/ContentStorage.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <map>
#include <stdexcept>

namespace crimson {
namespace {
std::uint64_t hashText(const std::string& value) { std::uint64_t hash=14695981039346656037ull; for(unsigned char byte:value){hash^=byte;hash*=1099511628211ull;} return hash; }
std::uint64_t combine(std::uint64_t a,std::uint64_t b) { return (a^b)*1099511628211ull; }
bool builtin(const std::string& id) { return id==GrayTextureId || id==NeutralNormalTextureId; }
DecodedImage builtinImage(const std::string& id) { return {1,1,id==NeutralNormalTextureId ? std::vector<std::uint8_t>{128,128,255,255} : std::vector<std::uint8_t>{128,128,128,255}}; }
struct GpuTexture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    std::uint32_t width{},height{},level{};
    std::uint64_t signature{},sourceFingerprint{};
    std::size_t bytes{};
    bool thumbnail{};
    ImagePixelFormat format{ImagePixelFormat::RGBA8};
    ~GpuTexture(){if(bgfx::isValid(handle)) bgfx::destroy(handle);}
};
struct Upload {
    std::shared_ptr<GpuTexture> texture;
    std::vector<ResourceHandle<DecodedImage>> requests;
    std::vector<std::shared_ptr<const DecodedImage>> pixels;
    std::size_t mip{},row{};
    std::uint64_t descriptorRevision{};
};
struct Entry {
    std::string id,variant;
    std::filesystem::path source;
    streaming::TextureRole role{};
    bool mipmapped{},thumbnail{},building{},wanted{};
    bool generated{};
    std::uint32_t sourceEdge{},sourceHeight{};
    std::function<DecodedImage()> generator;
    std::function<FloatImage(std::stop_token)> floatGenerator;
    bool floatGenerated{},retryFloat{};
    std::stop_source floatCancellation;
    ResourceHandle<streaming::TextureMipCache> generatedSource;
    bool repairNeeded{};
    unsigned repairAttempts{};
    std::uint64_t repairRevision{};
    std::string error;
    std::uint32_t edge{64};
    std::uint32_t desiredLevel{},pressureLevel{};
    std::chrono::steady_clock::time_point demandChanged{std::chrono::steady_clock::now()};
    ResourcePriority priority{};
    ResourceHandle<streaming::TextureMipCache> descriptor;
    ResourceHandle<DecodedImage> original;
    std::shared_ptr<GpuTexture> current;
    std::unique_ptr<Upload> pending;
    std::uint64_t buildingFingerprint{};
    std::chrono::steady_clock::time_point used{std::chrono::steady_clock::now()};
};
std::uint32_t generatedRequiredLevel(const Entry& entry) {
    auto width=entry.sourceEdge,height=entry.sourceHeight?entry.sourceHeight:entry.sourceEdge;
    std::uint32_t required{},last{};
    while(width>1 || height>1) {
        if(std::max(width,height)>entry.edge)required=last+1;
        width=std::max(1u,width/2);height=std::max(1u,height/2);++last;
    }
    // level is the original source mip index, including when the GPU texture
    // contains only that level rather than a mipmapped tail.
    return std::max(required,std::min(entry.pressureLevel,last));
}
}
struct TextureStreamer::Impl {
    ResourceManager& manager;
    std::filesystem::path root;
    std::map<std::string,Entry> entries;
    std::vector<std::weak_ptr<GpuTexture>> allocations;
    std::vector<std::shared_ptr<GpuTexture>> framePins;
    std::string error;
    explicit Impl(ResourceManager& resources,std::filesystem::path project):manager(resources),root(std::move(project)){}
    ResourceRequestOptions options(const Entry& entry) const {
        ResourceRequestOptions result; result.variant="mips:"+std::to_string(static_cast<unsigned>(entry.role));
        result.priority=entry.priority;
        if(!builtin(entry.id)) result.dependencies={entry.source};
        return result;
    }
    void requestDescriptor(Entry& entry,bool forceReload=false) {
        if(entry.generated) {
            if(entry.floatGenerator) {
                ResourceRequestOptions config;config.variant="generated-float16-mips-v1:"+std::to_string(entry.sourceEdge)+"x"+std::to_string(entry.sourceHeight);
                config.priority=entry.priority;config.retainLoader=false;config.cancelWhenUnused=true;
                config.forceReload=entry.retryFloat;entry.retryFloat=false;entry.floatCancellation=std::stop_source{};
                const auto generator=entry.floatGenerator;const auto width=entry.sourceEdge,height=entry.sourceHeight;
                const auto cancellation=entry.floatCancellation.get_token();
                const auto key=hashText(entry.id+":"+config.variant);
                entry.descriptor=manager.requestDerived<streaming::TextureMipCache>(entry.id,[generator,width,height,key,cancellation](std::stop_token token) {
                    std::stop_source combined;std::stop_callback resourceStop(token,[&]{combined.request_stop();});std::stop_callback demandStop(cancellation,[&]{combined.request_stop();});
                    if(combined.stop_requested())throw std::runtime_error("Generated float image request canceled.");
                    auto image=generator(combined.get_token());
                    if(image.width!=width||image.height!=height)throw std::runtime_error("Generated float image dimensions do not match the declared output.");
                    auto data=std::make_shared<streaming::TextureMipCache>(streaming::generatedFloatTextureMipCache(key,std::move(image),combined.get_token()));
                    return ResourceLoadResult<streaming::TextureMipCache>{data,streaming::textureMipCacheBytes(*data),key,{}};
                },std::move(config));
                return;
            }
            ResourceRequestOptions config;config.variant="generated-linear-mips-v1:"+std::to_string(entry.sourceEdge);config.priority=entry.priority;config.retainLoader=false;config.cancelWhenUnused=true;
            const auto generator=entry.generator;const auto sourceEdge=entry.sourceEdge;
            const auto key=hashText(entry.id+":"+config.variant);
            auto source=manager.requestDerived<streaming::TextureMipCache>(entry.id,[generator,sourceEdge,key](std::stop_token token) {
                if(token.stop_requested())throw std::runtime_error("Generated image request canceled.");
                auto image=generator();
                if(image.width!=sourceEdge||image.height!=sourceEdge)throw std::runtime_error("Generated image dimensions do not match the declared source edge.");
                if(token.stop_requested())throw std::runtime_error("Generated image request canceled.");
                auto data=std::make_shared<streaming::TextureMipCache>(streaming::generatedTextureMipCache(key,std::move(image)));
                return ResourceLoadResult<streaming::TextureMipCache>{data,streaming::textureMipCacheBytes(*data),key,{}};
            },std::move(config));
            if(entry.thumbnail)entry.generatedSource=std::move(source);else entry.descriptor=std::move(source);
            return;
        }
        const auto source=entry.source;const auto id=entry.id; const auto role=entry.role;
        const auto folder=resolveAssetPath(root,"assets/cache/texture-mips");
        auto config=options(entry); config.forceReload=forceReload;
        entry.descriptor=manager.request<streaming::TextureMipCache>(AssetRef<streaming::TextureMipCache>{id},
            [source,id,role,folder](std::stop_token token) {
                if(token.stop_requested()) throw std::runtime_error("Texture request canceled.");
                const auto key=builtin(id) ? hashText(id) : fingerprintFiles({source});
                auto data=std::make_shared<streaming::TextureMipCache>(); data->key=key; data->role=role;
                if(builtin(id)) data->levels.push_back({1,1,0,4,0});
                else {
                    data->file=folder/(std::to_string(hashText(id))+"-"+std::to_string(key)+"-"+std::to_string(static_cast<unsigned>(role))+".mips");
                    if(std::filesystem::is_regular_file(data->file)) try { *data=streaming::readTextureMipCache(data->file,key,role); }
                    catch(const std::exception&) { /* A disposable derived cache is regenerated. */ }
                }
                return ResourceLoadResult<streaming::TextureMipCache>{data,sizeof(*data)+data->levels.size()*sizeof(streaming::MipInfo),key,{}};
            },std::move(config));
    }
    void buildDescriptor(Entry& entry,const ResourceSnapshot<streaming::TextureMipCache>& descriptor) {
        if(!entry.original) {
            ResourceRequestOptions config; config.variant="source"; config.priority=entry.priority; config.dependencies={entry.source};
            const auto source=entry.source;
            entry.original=manager.request<DecodedImage>(AssetRef<DecodedImage>{entry.id},[source](std::stop_token token){
                if(token.stop_requested()) throw std::runtime_error("Image decode canceled.");
                auto image=std::make_shared<DecodedImage>(decodeManagedImage(source));
                return ResourceLoadResult<DecodedImage>{image,image->rgba.size(),0,{}};
            },std::move(config));
        }
        const auto image=entry.original.snapshot();
        if(entry.original.state()==ResourceState::Failed) { entry.error=error=entry.original.diagnostic(); return; }
        if(image && image.fingerprint()!=descriptor.fingerprint() && entry.original.state()==ResourceState::Ready) {
            ResourceRequestOptions config;config.variant="source";config.priority=entry.priority;config.dependencies={entry.source};config.forceReload=true;
            const auto source=entry.source;
            entry.original=manager.request<DecodedImage>(AssetRef<DecodedImage>{entry.id},[source](std::stop_token token) {
                if(token.stop_requested()) throw std::runtime_error("Image reload canceled.");
                auto pixels=std::make_shared<DecodedImage>(decodeManagedImage(source));
                return ResourceLoadResult<DecodedImage>{pixels,pixels->rgba.size(),0,{}};
            },std::move(config));
            return;
        }
        if(!image || image.fingerprint()!=descriptor.fingerprint() || entry.buildingFingerprint==descriptor.fingerprint()) return;
        auto config=options(entry); config.forceReload=true; config.retainLoader=false;
        const auto file=descriptor->file;const auto role=entry.role; const auto pixels=image.shared(); const auto key=descriptor.fingerprint();
        entry.building=true; entry.buildingFingerprint=key;
        entry.descriptor=manager.request<streaming::TextureMipCache>(AssetRef<streaming::TextureMipCache>{entry.id},
            [pixels,file,role,key](std::stop_token token) {
                if(token.stop_requested()) throw std::runtime_error("Mip generation canceled.");
                auto levels=streaming::generateTextureMips(pixels->width,pixels->height,pixels->rgba,role);
                if(token.stop_requested()) throw std::runtime_error("Mip generation canceled.");
                auto data=std::make_shared<streaming::TextureMipCache>(streaming::writeTextureMipCache(file,key,role,levels));
                return ResourceLoadResult<streaming::TextureMipCache>{data,sizeof(*data)+data->levels.size()*sizeof(streaming::MipInfo),key,{}};
            },std::move(config));
    }
    void queueLevel(Entry& entry,const ResourceSnapshot<streaming::TextureMipCache>& descriptor,std::uint32_t level) {
        auto upload=std::make_unique<Upload>(); upload->descriptorRevision=descriptor.revision();
        const auto tail=entry.mipmapped ? static_cast<std::uint32_t>(descriptor->levels.size()) : level+1;
        for(auto sourceLevel=level;sourceLevel<tail;++sourceLevel) {
            if(entry.generated) {
                if(descriptor->memory.size()!=descriptor->levels.size())throw std::runtime_error("Generated image mip payload is incomplete.");
                const auto& image=descriptor->memory[sourceLevel];const auto& info=descriptor->levels[sourceLevel];
                if(image.width!=info.width||image.height!=info.height||image.format!=descriptor->format||image.rgba.size()!=info.bytes)
                    throw std::runtime_error("Generated image mip format or dimensions do not match its descriptor.");
                upload->pixels.emplace_back(descriptor.shared(),&descriptor->memory[sourceLevel]);
                continue;
            }
            ResourceRequestOptions options; options.variant="mip:"+std::to_string(static_cast<unsigned>(entry.role))+":"+std::to_string(descriptor.fingerprint())+":"+std::to_string(descriptor.revision())+":"+std::to_string(sourceLevel);
            options.priority=entry.priority;
            // Derived filenames include the immutable source fingerprint. Per-mip
            // checksums validate exactly the payload read; hashing the complete
            // cache before/after every tiny mip would defeat actual IO streaming.
            if(!builtin(entry.id)) {options.dependencies={descriptor->file};options.dependencyValidation=DependencyValidation::FileState;}
            const auto cache=*descriptor; const auto id=entry.id;
            upload->requests.push_back(manager.request<DecodedImage>(AssetRef<DecodedImage>{entry.id},[cache,id,sourceLevel](std::stop_token token){
                if(token.stop_requested()) throw std::runtime_error("Mip read canceled.");
                auto image=std::make_shared<DecodedImage>(builtin(id) ? builtinImage(id) : streaming::readTextureMip(cache,sourceLevel));
                return ResourceLoadResult<DecodedImage>{image,image->rgba.size(),cache.key,{}};
            },std::move(options)));
        }
        upload->texture=std::make_shared<GpuTexture>();
        upload->texture->thumbnail=entry.thumbnail;
        upload->texture->width=descriptor->levels[level].width; upload->texture->height=descriptor->levels[level].height;
        upload->texture->level=level; upload->texture->sourceFingerprint=descriptor.fingerprint();
        upload->texture->format=descriptor->format;
        upload->texture->signature=combine(combine(descriptor.fingerprint(),hashText(entry.variant)),level);
        for(auto index=level;index<tail;++index) upload->texture->bytes+=static_cast<std::size_t>(descriptor->levels[index].bytes);
        entry.pending=std::move(upload);
    }
    void advanceEntry(Entry& entry,std::size_t& remaining,const std::chrono::steady_clock::time_point deadline) {
        if(!entry.wanted) {
            entry.pending.reset();entry.original={};
            if(entry.generated){entry.descriptor={};entry.generatedSource={};}
            return;
        }
        if(entry.generatedSource) {
            if(entry.generatedSource.state()==ResourceState::Failed){entry.error=error=entry.generatedSource.diagnostic();return;}
            const auto source=entry.generatedSource.snapshot();
            if(!source||entry.generatedSource.state()!=ResourceState::Ready)return;
            ResourceRequestOptions config;config.variant="generated-display-mips-v1:"+std::to_string(entry.sourceEdge);config.priority=entry.priority;config.retainLoader=false;config.cancelWhenUnused=true;
            const auto pixels=source.shared();
            entry.descriptor=manager.requestDerived<streaming::TextureMipCache>(entry.id,[pixels](std::stop_token token) {
                if(token.stop_requested())throw std::runtime_error("Generated thumbnail request canceled.");
                auto data=std::make_shared<streaming::TextureMipCache>(streaming::generatedTextureThumbnail(*pixels));
                return ResourceLoadResult<streaming::TextureMipCache>{data,streaming::textureMipCacheBytes(*data),data->key,{}};
            },std::move(config));
            entry.generatedSource={};
        }
        if(!entry.descriptor) return;
        const auto descriptor=entry.descriptor.snapshot();
        const auto state=entry.descriptor.state();
        if(state==ResourceState::Failed) { entry.error=error=entry.descriptor.diagnostic();return; }
        if(!descriptor || state!=ResourceState::Ready) return;
        if(entry.pending && descriptor.revision()!=entry.pending->descriptorRevision) entry.pending.reset();
        if(entry.repairNeeded) {
            if(descriptor.revision()!=entry.repairRevision && !descriptor->levels.empty()) {entry.repairNeeded=false;entry.error.clear();}
            else { buildDescriptor(entry,descriptor);return; }
        }
        if(descriptor->levels.empty()) {
            if(entry.descriptor.state()==ResourceState::Failed) { entry.error=error=entry.descriptor.diagnostic(); return; }
            buildDescriptor(entry,descriptor); return;
        }
        entry.original={}; entry.building=false;
        if(entry.pending) {
            auto& pending=*entry.pending;
            if(pending.pixels.empty()) {
                for(const auto& request:pending.requests) {
                    if(request.state()==ResourceState::Failed) {
                        entry.error=error=request.diagnostic(); entry.pending.reset();
                        if(!builtin(entry.id) && !entry.repairAttempts++) { entry.repairNeeded=true;entry.repairRevision=descriptor.revision();entry.buildingFingerprint=0; }
                        return;
                    }
                    auto pixels=request.snapshot(); if(!pixels) return;
                }
                for(const auto& request:pending.requests) pending.pixels.push_back(request.snapshot().shared());
                const auto first=static_cast<std::size_t>(pending.texture->level);
                for(std::size_t i=0;i<pending.pixels.size();++i) {
                    const auto& expected=descriptor->levels[first+i]; const auto& image=*pending.pixels[i];
                    if(image.width!=expected.width || image.height!=expected.height || image.rgba.size()!=expected.bytes || image.format!=descriptor->format)
                        throw std::runtime_error("Streamed mip payload dimensions do not match their descriptor.");
                }
            }
            if(!bgfx::isValid(pending.texture->handle)) {
                const auto limit=bgfx::getCaps()->limits.maxTextureSize;
                if(pending.texture->width>limit || pending.texture->height>limit) { entry.error=error="Texture dimensions exceed GPU capabilities."; entry.pending.reset(); return; }
                const auto format=pending.texture->format==ImagePixelFormat::RGBA16F?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8;
                if(!(bgfx::getCaps()->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_2D)) {entry.error=error="GPU does not support the requested streaming texture format.";entry.pending.reset();return;}
                const auto flags=entry.role==streaming::TextureRole::Color && !entry.thumbnail && format==bgfx::TextureFormat::RGBA8 ? BGFX_TEXTURE_SRGB : BGFX_TEXTURE_NONE;
                pending.texture->handle=bgfx::createTexture2D(static_cast<std::uint16_t>(pending.texture->width),static_cast<std::uint16_t>(pending.texture->height),
                    entry.mipmapped,1,format,flags);
                if(!bgfx::isValid(pending.texture->handle)) { entry.error=error="Streaming texture allocation failed; retaining prior texture."; entry.pending.reset(); return; }
                bgfx::setName(pending.texture->handle,entry.id.c_str()); allocations.push_back(pending.texture);
            }
            while(pending.mip<pending.pixels.size() && remaining && std::chrono::steady_clock::now()<deadline) {
                const auto& image=*pending.pixels[pending.mip]; const auto rowBytes=static_cast<std::size_t>(image.width)*imagePixelBytes(image.format);
                // Row-aligned bgfx updates are indivisible. Defer this upload
                // rather than forcing a row beyond the coordinator's byte cap.
                if(remaining<rowBytes) return;
                const auto rows=std::min<std::size_t>(image.height-pending.row,remaining/rowBytes); const auto size=rows*rowBytes;
                bgfx::updateTexture2D(pending.texture->handle,0,static_cast<std::uint8_t>(pending.mip),0,static_cast<std::uint16_t>(pending.row),
                    static_cast<std::uint16_t>(image.width),static_cast<std::uint16_t>(rows),
                    bgfx::copy(image.rgba.data()+pending.row*rowBytes,static_cast<std::uint32_t>(size)));
                pending.row+=rows; remaining-=size;
                if(pending.row==image.height) { pending.row=0; ++pending.mip; }
            }
            if(pending.mip==pending.pixels.size()) { entry.current=std::move(pending.texture); entry.pending.reset(); entry.error.clear();error.clear(); }
            return;
        }
        if(!entry.wanted) return;
        std::uint32_t wantedLevel=0;
        while(wantedLevel+1<descriptor->levels.size() && std::max(descriptor->levels[wantedLevel].width,descriptor->levels[wantedLevel].height)>entry.edge) ++wantedLevel;
        wantedLevel=std::max(wantedLevel,std::min(entry.pressureLevel,static_cast<std::uint32_t>(descriptor->levels.size()-1)));
        if(wantedLevel!=entry.desiredLevel) { entry.desiredLevel=wantedLevel;entry.demandChanged=std::chrono::steady_clock::now(); }
        const auto coarse=streaming::coarseTextureMip(descriptor->levels);
        const bool changed=!entry.current || entry.current->sourceFingerprint!=descriptor.fingerprint();
        auto next=changed ? std::max(coarse,wantedLevel) : (entry.current->level>wantedLevel ? entry.current->level-1 : entry.current->level);
        if(!changed && entry.current->level<wantedLevel &&
            (entry.pressureLevel || std::chrono::steady_clock::now()-entry.demandChanged>=manager.config().idleGrace)) next=wantedLevel;
        if(changed || next!=entry.current->level) queueLevel(entry,descriptor,next);
    }
    PreparedTexture prepared(Entry& entry) {
        PreparedTexture result;
        if(entry.current) {framePins.push_back(entry.current);result={entry.current->handle,entry.current->width,entry.current->height,entry.current->signature,entry.current->sourceFingerprint,entry.current};}
        if(const auto descriptor=entry.descriptor.snapshot();descriptor && !descriptor->levels.empty()) {result.sourceWidth=descriptor->levels.front().width;result.sourceHeight=descriptor->levels.front().height;}
        else if(entry.generated) {result.sourceWidth=entry.sourceEdge;result.sourceHeight=entry.sourceHeight?entry.sourceHeight:entry.sourceEdge;}
        else if(const auto original=entry.original.snapshot()) {result.sourceWidth=original->width;result.sourceHeight=original->height;}
        if(const auto descriptor=entry.descriptor.snapshot();descriptor && !descriptor->levels.empty() && entry.current && entry.descriptor.state()==ResourceState::Ready) {
            std::uint32_t required=0;
            while(required+1<descriptor->levels.size() && std::max(descriptor->levels[required].width,descriptor->levels[required].height)>entry.edge) ++required;
            required=std::max(required,std::min(entry.pressureLevel,static_cast<std::uint32_t>(descriptor->levels.size()-1)));
            result.sourceReady=entry.current->sourceFingerprint==descriptor.fingerprint();
            result.complete=result.sourceReady && entry.current->level<=required;
        } else if(entry.generated&&entry.current) {
            result.sourceReady=true;result.complete=entry.current->level<=generatedRequiredLevel(entry);
        }
        result.diagnostic=entry.error;
        if(entry.descriptor.state()==ResourceState::Failed)result.diagnostic=entry.descriptor.diagnostic();
        else if(entry.generatedSource.state()==ResourceState::Failed)result.diagnostic=entry.generatedSource.diagnostic();
        else if(entry.original.state()==ResourceState::Failed)result.diagnostic=entry.original.diagnostic();
        result.failed=!entry.error.empty()||entry.descriptor.state()==ResourceState::Failed||
            entry.generatedSource.state()==ResourceState::Failed||entry.original.state()==ResourceState::Failed;
        return result;
    }
};
TextureStreamer::TextureStreamer(ResourceManager& manager,std::filesystem::path root):impl_(std::make_unique<Impl>(manager,std::move(root))) {}
TextureStreamer::~TextureStreamer(){for(auto& [key,entry]:impl_->entries){(void)key;if(entry.floatGenerated)entry.floatCancellation.request_stop();}}
PreparedTexture TextureStreamer::request(const std::string& id,const std::filesystem::path& source,streaming::TextureRole role,bool mipmapped,std::uint32_t edge,ResourcePriority priority,bool thumbnail) {
    const auto variant=std::to_string(static_cast<unsigned>(role))+":"+(mipmapped ? "mips" : "single")+(thumbnail ? ":thumb" : "");
    auto [iterator,inserted]=impl_->entries.try_emplace(id+'\0'+variant); auto& entry=iterator->second;
    if(inserted) { entry.id=id;entry.source=source;entry.role=role;entry.mipmapped=mipmapped;entry.thumbnail=thumbnail;entry.variant=variant;entry.priority=priority;impl_->requestDescriptor(entry); }
    else if(entry.source!=source && (entry.source.empty()||source.empty()||resourcePathKey(entry.source)!=resourcePathKey(source))) {
        // A physical move preserves the asset ID and the last drawable lease,
        // while new requests must validate and load its current location.
        entry.source=source;entry.pending.reset();entry.original={};entry.descriptor={};
        entry.building=false;entry.buildingFingerprint=0;entry.repairNeeded=false;entry.repairAttempts=0;entry.error.clear();
        impl_->requestDescriptor(entry,true);
    }
    entry.edge=std::max(entry.wanted ? entry.edge : 0u,std::max(1u,edge)); entry.wanted=true; entry.priority=std::min(entry.priority,priority); entry.used=std::chrono::steady_clock::now();
    if(priority==ResourcePriority::Selected)entry.pressureLevel=0;
    if(!entry.descriptor) impl_->requestDescriptor(entry);
    return impl_->prepared(entry);
}
PreparedTexture TextureStreamer::requestGenerated(const std::string& key,std::function<DecodedImage()> generator,std::uint32_t sourceEdge,bool mipmapped,std::uint32_t edge,ResourcePriority priority,bool thumbnail) {
    if(key.empty()||key.find('\0')!=std::string::npos||!generator||!sourceEdge||sourceEdge>16384)throw std::runtime_error("Generated texture requires a content key, generator and source edge 1-16384.");
    const auto variant="generated:"+std::to_string(sourceEdge)+(mipmapped?":mips":":single")+(thumbnail?":thumb":"");
    auto [iterator,inserted]=impl_->entries.try_emplace(key+'\0'+variant);auto& entry=iterator->second;
    if(inserted){entry.id=key;entry.variant=variant;entry.generated=true;entry.sourceEdge=sourceEdge;entry.role=streaming::TextureRole::Data;entry.mipmapped=mipmapped;entry.thumbnail=thumbnail;entry.priority=priority;entry.generator=std::move(generator);}
    entry.edge=std::max(entry.wanted?entry.edge:0u,std::max(1u,edge));entry.wanted=true;entry.priority=std::min(entry.priority,priority);entry.used=std::chrono::steady_clock::now();
    if(priority==ResourcePriority::Selected)entry.pressureLevel=0;
    if(!entry.descriptor&&!entry.generatedSource&&(!entry.current||std::max(entry.current->width,entry.current->height)<std::min(entry.edge,entry.sourceEdge)))impl_->requestDescriptor(entry);
    return impl_->prepared(entry);
}
PreparedTexture TextureStreamer::requestGeneratedFloat(const std::string& key,std::function<FloatImage()> generator,std::uint32_t width,std::uint32_t height,bool mipmapped,std::uint32_t edge,ResourcePriority priority) {
    if(!generator)throw std::runtime_error("Generated float texture requires a generator.");
    return requestGeneratedFloat(key,[generator=std::move(generator)](std::stop_token){return generator();},width,height,mipmapped,edge,priority);
}
PreparedTexture TextureStreamer::requestGeneratedFloat(const std::string& key,std::function<FloatImage(std::stop_token)> generator,std::uint32_t width,std::uint32_t height,bool mipmapped,std::uint32_t edge,ResourcePriority priority) {
    if(key.empty()||key.find('\0')!=std::string::npos||!generator||!width||!height||width>4096||height>4096)
        throw std::runtime_error("Generated float texture requires a content key, generator and dimensions 1-4096.");
    const auto variant="generated-float16:"+std::to_string(width)+"x"+std::to_string(height)+(mipmapped?":mips":":single");
    auto [iterator,inserted]=impl_->entries.try_emplace(key+'\0'+variant);auto& entry=iterator->second;
    if(inserted){entry.id=key;entry.variant=variant;entry.generated=true;entry.floatGenerated=true;entry.sourceEdge=width;entry.sourceHeight=height;entry.role=streaming::TextureRole::Data;entry.mipmapped=mipmapped;entry.priority=priority;}
    entry.edge=std::max(entry.wanted?entry.edge:0u,std::max(1u,edge));entry.wanted=true;entry.priority=std::min(entry.priority,priority);entry.used=std::chrono::steady_clock::now();
    if(priority==ResourcePriority::Selected)entry.pressureLevel=0;
    if(!entry.descriptor&&(!entry.current||entry.current->level>generatedRequiredLevel(entry))) {
        entry.floatGenerator=std::move(generator);impl_->requestDescriptor(entry);entry.floatGenerator={};
    }
    return impl_->prepared(entry);
}
std::optional<PreparedTexture> TextureStreamer::cachedGeneratedFloat(const std::string& key,std::uint32_t width,std::uint32_t height,bool mipmapped,std::uint32_t edge,ResourcePriority priority) {
    const auto variant="generated-float16:"+std::to_string(width)+"x"+std::to_string(height)+(mipmapped?":mips":":single");
    const auto found=impl_->entries.find(key+'\0'+variant);if(found==impl_->entries.end())return {};
    auto& entry=found->second;entry.edge=std::max(entry.wanted?entry.edge:0u,std::max(1u,edge));entry.wanted=true;
    entry.priority=std::min(entry.priority,priority);entry.used=std::chrono::steady_clock::now();if(priority==ResourcePriority::Selected)entry.pressureLevel=0;
    if(!entry.descriptor&&(!entry.current||entry.current->level>generatedRequiredLevel(entry)))return {};
    return impl_->prepared(entry);
}
ResourceSnapshot<streaming::TextureMipCache> TextureStreamer::generatedFloatSnapshot(const std::string& key,std::uint32_t width,std::uint32_t height,bool mipmapped) const {
    const auto variant="generated-float16:"+std::to_string(width)+"x"+std::to_string(height)+(mipmapped?":mips":":single");
    const auto found=impl_->entries.find(key+'\0'+variant);
    if(found==impl_->entries.end()||found->second.descriptor.state()!=ResourceState::Ready)return {};
    return found->second.descriptor.snapshot();
}
void TextureStreamer::beginFrame() { impl_->framePins.clear(); for(auto& [key,entry]:impl_->entries) { (void)key; entry.wanted=false;entry.edge=0;entry.priority=ResourcePriority::Background; } }
void TextureStreamer::advance(std::size_t bytes,double milliseconds) {
    for(auto& [key,entry]:impl_->entries)if(entry.generated&&!entry.wanted){(void)key;if(entry.floatGenerated){entry.floatCancellation.request_stop();entry.retryFloat|=entry.descriptor&&entry.descriptor.state()!=ResourceState::Ready;}entry.pending.reset();entry.descriptor={};entry.generatedSource={};}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(milliseconds));
    std::vector<Entry*> ordered; for(auto& [key,entry]:impl_->entries){(void)key;ordered.push_back(&entry);}
    std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->priority<b->priority;});
    for(auto* entry:ordered) {
        if(std::chrono::steady_clock::now()>=deadline || bytes==0) break;
        try { impl_->advanceEntry(*entry,bytes,deadline); } catch(const std::exception& error) { entry->error=impl_->error=error.what();entry->pending.reset(); }
    }
}
void TextureStreamer::invalidate(const std::string& id) { std::erase_if(impl_->entries,[&](const auto& pair){return !pair.second.generated&&pair.second.id==id;}); }
void TextureStreamer::forgetGenerated(const std::string& key) {
    for(auto i=impl_->entries.begin();i!=impl_->entries.end();) {
        auto& entry=i->second;
        if(entry.generated&&entry.id==key){entry.floatCancellation.request_stop();i=impl_->entries.erase(i);}
        else ++i;
    }
}
void TextureStreamer::reload(const std::string& id) {
    std::vector<streaming::TextureRole> refreshed;
    for(auto& [key,entry]:impl_->entries) if(!entry.generated&&entry.id==id) {
        (void)key; entry.pending.reset();entry.original={};entry.building=false;entry.buildingFingerprint=0;entry.repairNeeded=false;entry.repairAttempts=0;entry.error.clear();
        const bool first=std::find(refreshed.begin(),refreshed.end(),entry.role)==refreshed.end();
        impl_->requestDescriptor(entry,first); if(first) refreshed.push_back(entry.role);
    }
}
std::size_t TextureStreamer::residentBytes() const { std::size_t bytes=0; for(const auto& allocation:impl_->allocations) if(auto texture=allocation.lock()) bytes+=texture->bytes; return bytes; }
void TextureStreamer::trim(std::size_t targetBytes) {
    std::erase_if(impl_->allocations,[](const auto& value){return value.expired();});
    const auto now=std::chrono::steady_clock::now(); std::vector<Entry*> idle;
    for(auto& [key,entry]:impl_->entries) { (void)key; if(!entry.wanted && entry.current && entry.current.use_count()==1) idle.push_back(&entry); }
    std::sort(idle.begin(),idle.end(),[](const auto* a,const auto* b){return a->used<b->used;});
    auto remaining=residentBytes();std::size_t thumbnailBytes{};
    for(const auto& allocation:impl_->allocations) if(auto texture=allocation.lock();texture && texture->thumbnail) thumbnailBytes+=texture->bytes;
    for(auto* entry:idle) {
        // Browser images keep a bounded LRU working set. Scene-role allocations
        // still use the normal idle grace, and frame/reader leases remain pinned.
        if(remaining<=targetBytes && (entry->thumbnail ? thumbnailBytes<=impl_->manager.config().thumbnailBytes : now-entry->used<impl_->manager.config().idleGrace)) continue;
        const auto released=entry->current->bytes+(entry->pending && bgfx::isValid(entry->pending->texture->handle)?entry->pending->texture->bytes:0);
        remaining-=released;if(entry->thumbnail)thumbnailBytes-=released;
        entry->current.reset();entry->pending.reset();
    }
    remaining=residentBytes();
    for(auto& [key,entry]:impl_->entries) {
        (void)key;
        if(remaining<targetBytes*4/5) entry.pressureLevel=0;
        else if(remaining>targetBytes && entry.wanted && entry.current && entry.priority!=ResourcePriority::Selected) {
            if(entry.pending && entry.pending->texture->bytes>entry.current->bytes) entry.pending.reset();
            entry.pressureLevel=std::max(entry.pressureLevel,entry.current->level+1);
        }
    }
    std::erase_if(impl_->entries,[&](const auto& item){const auto& entry=item.second;return entry.generated&&!entry.wanted&&!entry.current&&!entry.pending&&now-entry.used>=impl_->manager.config().idleGrace;});
}
std::string TextureStreamer::diagnostic() const { return impl_->error; }
}
