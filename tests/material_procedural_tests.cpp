#include <crimson/renderer/forward/MaterialProcedural.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cmath>
#include <fstream>
#include <future>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using crimson::Vec2;
using namespace crimson::material_procedural;
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected{};try{function();}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,message);
}
std::vector<float> grid(const Settings& settings,unsigned width=32,unsigned height=32,
                        float footprint=1.f/512) {
    PreparedPattern field(settings,footprint,float(width)/height);
    std::vector<float> result(std::size_t(width)*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        result[std::size_t(y)*width+x]=field.sample({(float(x)+.5f)/width,(float(y)+.5f)/height});
    return result;
}
double difference(const std::vector<float>& a,const std::vector<float>& b) {
    double sum{};for(std::size_t i=0;i<a.size();++i)sum+=std::abs(a[i]-b[i]);return sum/a.size();
}
double mean(const std::vector<float>& values) {
    double sum{};for(float value:values)sum+=value;return sum/values.size();
}
void identities_and_defaults() {
    check(patterns.size()==12&&static_cast<unsigned>(Pattern::DirectionalScratches)==12,
          "Procedural catalog lost the excluded legacy identity gap.");
    for(const auto pattern:patterns) {
        check(is_supported(pattern)&&pattern_name(pattern)[0],"Missing supported family name.");
        validate(default_settings(pattern));
    }
    check(!is_supported(static_cast<Pattern>(11)),"Rejected legacy scuff source is exposed.");
    rejects([]{default_settings(static_cast<Pattern>(11));},"Invalid family default was accepted.");
    const auto scratches=default_settings(Pattern::DirectionalScratches);
    check(scratches.scale==4&&scratches.patternSize==Vec2{.5f,20},"Scratch family defaults are incorrect.");
    check(default_settings(Pattern::GaussianNoise).scale==32,"Gaussian family default is not fine scale.");
    check(default_settings(Pattern::MultiscaleSpots).detailBalance==.5f,"Spots spectral default is incorrect.");
    const auto parabola=controls(Pattern::ParabolicGradient);
    check(!parabola.seed&&!parabola.disorder&&parabola.scale&&parabola.angle,
          "Parabolic gradient exposes meaningless random controls.");
    const auto fibers=controls(Pattern::MessyFibers);
    check(fibers.threads&&fibers.fibersAngleSpread&&!fibers.patternSize,
          "Fiber controls do not match actual algorithm inputs.");
    const auto streaks=controls(Pattern::GaussianStreaks);
    check(streaks.angle&&!streaks.patternSize,"Streaks expose unsupported width or length.");
    auto invalid=scratches;invalid.scale=4.5f;
    rejects([&]{validate(invalid);},"Directional scatter accepted fractional tile count.");
    invalid=scratches;invalid.patternSize={.001f,64};validate(invalid);
    invalid.patternSize[0]=0;rejects([&]{validate(invalid);},"Zero scratch width was accepted.");
    invalid=default_settings(Pattern::GaussianNoise);invalid.scale=64;validate(invalid);
    invalid.scale=64.01f;rejects([&]{validate(invalid);},"Gaussian scale upper bound was lost.");
    invalid=default_settings(Pattern::Clouds2);invalid.disorder=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validate(invalid);},"Nonfinite procedural setting was accepted.");
}
void deterministic_reference_samples() {
    // Independent pre-port Texflow CPU reference values. Gaussian is sampled
    // at scale 1 here intentionally; its artist preset has a different scale.
    constexpr std::array<Pattern,10> families{Pattern::Clouds1,Pattern::Clouds2,Pattern::FineDirt,
        Pattern::Moisture,Pattern::Creased,Pattern::MessyFibers,Pattern::WhiteNoise,
        Pattern::GaussianNoise,Pattern::ParabolicGradient,Pattern::GaussianStreaks};
    constexpr std::array<std::array<float,3>,10> expected{{
        {{.5157456994f,.7465609312f,.6366863251f}},
        {{.736209631f,.6090163589f,.7944098711f}},
        {{.1653838456f,.1974271238f,.2609547377f}},
        {{.4215226769f,.5277463198f,.2367203534f}},
        {{.5004060268f,.5102818012f,.3665061891f}},
        {{.6597262621f,.6343282461f,.5419559479f}},
        {{.7839866877f,.3247640729f,.7541790009f}},
        {{.3194197118f,.3185790777f,.3191826642f}},
        {{.432828784f,.9779552221f,.4274272919f}},
        {{.01065003127f,.07691831887f,.008448719978f}}
    }};
    constexpr std::array<Vec2,3> points{{{.017f,.079f},{.215f,.417f},{.691f,.923f}}};
    constexpr std::array footprints{0.f,1.f/512,0.f};
    for(unsigned p=0;p<families.size();++p) {
        auto settings=default_settings(families[p]);settings.seed=19;settings.scale=1;
        for(unsigned i=0;i<points.size();++i)
            check(std::abs(sample(points[i],settings,footprints[i])-expected[p][i])<.000002f,
                  "Port changed an independently recorded procedural reference sample.");
    }
}
void periodicity_and_sampling() {
    for(const auto pattern:patterns) {
        auto settings=default_settings(pattern);settings.seed=19;
        if(pattern!=Pattern::DirectionalScratches)settings.scale=1.375f;
        PreparedPattern field(settings,1.f/512,2);
        double seed_delta{},disorder_delta{};
        auto reseeded=settings;reseeded.seed=317;PreparedPattern other(reseeded,1.f/512,2);
        auto redistributed=settings;redistributed.disorder=.35f;PreparedPattern varied(redistributed,1.f/512,2);
        for(unsigned i=0;i<32;++i) {
            const Vec2 uv{(float(i)+.173f)/32,float((i*13)%31)/31+.0053f};
            const float value=field.sample(uv);
            check(std::isfinite(value)&&value>=0&&value<=1,"Procedural output is nonfinite or unbounded.");
            check(value==field.sample(uv),"Same source settings are nondeterministic.");
            check(std::abs(value-field.sample({uv[0]+1,uv[1]-2}))<.004f,
                  "Source is not periodic across positive and negative tile coordinates.");
            seed_delta+=std::abs(value-other.sample(uv));
            disorder_delta+=std::abs(value-varied.sample(uv));
        }
        if(pattern==Pattern::ParabolicGradient)
            check(seed_delta==0&&disorder_delta==0,"Parabola depends on hidden random controls.");
        else check(seed_delta>.0001&&disorder_delta>.0001,"Active source randomness does not alter pixels.");
    }
    auto source=default_settings(Pattern::WhiteNoise);
    check(std::abs(sample({.137f,.531f},source,1)-.5f)<.0005f,
          "Unresolved white noise enlarged its grain instead of approaching the mean.");
    source=default_settings(Pattern::MultiscaleSpots);source.detailBalance=1;
    check(std::abs(sample({.317f,.571f},source,1.f/32)-.5f)<.001f,
          "Unresolved Spots bands lost their mean.");
    rejects([&]{sample({std::numeric_limits<float>::infinity(),0},source);},"Invalid UV accepted.");
    rejects([&]{PreparedPattern(source,-.1f);},"Negative footprint accepted.");
    rejects([&]{PreparedPattern(source,0,0);},"Invalid image aspect accepted.");
    check(std::isfinite(sample({1e20f,-1e20f},source)),"Huge finite UV caused unsafe integer conversion.");
}
void meaningful_controls() {
    auto moisture=default_settings(Pattern::Moisture);moisture.contribution=0;
    for(float value:grid(moisture))check(value==.5f,"Zero Moisture contribution must be neutral gray.");
    moisture.contribution=.8f;const auto low=grid(moisture);
    moisture.contribution=1.6f;const auto high=grid(moisture);
    double low_energy{},high_energy{};
    for(unsigned i=0;i<low.size();++i) {
        low_energy+=(low[i]-.5)*(low[i]-.5);high_energy+=(high[i]-.5)*(high[i]-.5);
    }
    check(high_energy>low_energy*1.5,"Moisture contribution does not strengthen actual relief.");
    moisture.patternSize={.35f,1.8f};const auto aligned=grid(moisture);moisture.angleRandom=180;
    check(difference(aligned,grid(moisture))>.005,"Moisture angle randomness has no effect.");
    auto fibers=default_settings(Pattern::MessyFibers);fibers.fibersAngleSpread=0;fibers.threads=3;
    const auto coarse=grid(fibers);fibers.threads=32;
    check(difference(coarse,grid(fibers))>.01,"Fiber threads do not alter internal pattern structure.");
    const auto narrow=grid(fibers);fibers.fibersAngleSpread=180;
    check(difference(narrow,grid(fibers))>.01,"Fiber angle spread has no effect.");
    auto spots=default_settings(Pattern::MultiscaleSpots);spots.seed=19;
    spots.detailBalance=0;const auto broad=grid(spots,96,96);
    spots.detailBalance=1;const auto fine=grid(spots,96,96);
    check(difference(broad,fine)>.02,"Spots balance does not shift spectral structure.");
    check(std::abs(mean(broad)-.5)<.05&&std::abs(mean(fine)-.5)<.05,
          "Spots balance changes opacity rather than preserving the mean.");
    auto gradient=default_settings(Pattern::ParabolicGradient);
    check(std::abs(sample({.1f,0},gradient)-.2f)<1e-6f&&sample({.1f,.5f},gradient)==1,
          "Parabolic field disagrees with its analytic profile.");
    gradient.angle=90;
    check(std::abs(sample({0,.1f},gradient)-.2f)<1e-6f,"Parabolic angle does not rotate its profile.");
}
void scratch_coverage() {
    auto settings=default_settings(Pattern::DirectionalScratches);
    const auto baseline=grid(settings,64,64,1.f/128);
    settings.patternAmount=0;
    for(float value:grid(settings,16,16,1.f/128))check(value==0,"Zero scratch amount did not remove marks.");
    settings.patternAmount=.25f;const auto sparse=grid(settings,64,64,1.f/128);
    for(unsigned i=0;i<baseline.size();++i)
        check(sparse[i]<=baseline[i]+1e-6f,"Scratch amount reseeds or fades instead of selecting a stable subset.");
    settings.patternAmount=1;settings.patternSize[0]=1;
    const auto wider=grid(settings,64,64,1.f/128);
    for(unsigned i=0;i<baseline.size();++i)
        check(wider[i]+1e-6f>=baseline[i],"Width does not broaden the same scratch support.");
    settings.patternSize[0]=.1f;const auto thin=grid(settings,64,64,1.f/128);
    check(mean(thin)>.03&&mean(thin)<mean(baseline),
          "Subpixel scratches disappear or acquire an unrelated constant gray background.");
    PreparedPattern square(settings,1.f/128,1),rect(settings,1.f/128,2);
    double delta{};
    for(unsigned i=0;i<32;++i) {
        const Vec2 uv{float(i)/32+.001f,float((i*7)%29)/29};
        delta+=std::abs(square.sample(uv)-rect.sample(uv));
    }
    check(delta>.001,"Keep Square Tiles ignores the actual non-square image aspect.");
    settings.nonSquareExpansion=false;
    PreparedPattern no_expand_square(settings,1.f/128,1),no_expand_rect(settings,1.f/128,2);
    check(no_expand_square.sample({.137f,.571f})==no_expand_rect.sample({.137f,.571f}),
          "Disabled non-square expansion still depends on image aspect.");
}
void prepared_reference_parity() {
    for(const auto pattern:patterns)for(bool varied:{false,true}) {
        auto settings=default_settings(pattern);
        if(varied) {
            settings.seed=177;settings.disorder=.381f;settings.angle=47;settings.angleRandom=97;
            settings.scale=pattern==Pattern::DirectionalScratches?3.f:2.375f;
            settings.detailBalance=.17f;settings.patternSize=pattern==Pattern::DirectionalScratches?Vec2{.07f,7}:Vec2{.3f,2.5f};
            settings.patternSizeRandom={.7f,.4f};settings.patternAmount=.63f;
            settings.contribution=2;settings.fibersAngleSpread=360;settings.threads=31;
        }
        const float footprint=varied?1.f/96:1.f/128,aspect=varied?2.75f:1.f;
        for(const std::size_t budget:{std::size_t(0),std::size_t(8192),std::size_t(64u*1024u*1024u)}) {
            PreparedPattern prepared(settings,footprint,aspect,budget);
            check(prepared.memory_bytes()<=budget,"Prepared stamp storage exceeded its explicit memory cap.");
            std::uint32_t state=0x79271u;
            for(unsigned i=0;i<64;++i) {
                state=state*1664525u+1013904223u;const float u=float(state>>8)/16777216.f;
                state=state*1664525u+1013904223u;const float v=float(state>>8)/16777216.f;
                const Vec2 uv{u+1,v-2};
                const float reference=sample(uv,settings,footprint,aspect),actual=prepared.sample(uv);
                if(std::abs(actual-reference)>3e-6f)
                    throw std::runtime_error(std::string("Prepared/reference mismatch: ")+pattern_name(pattern)+
                        " varied="+std::to_string(varied)+" budget="+std::to_string(budget)+
                        " uv="+std::to_string(u)+","+std::to_string(v)+
                        " reference="+std::to_string(reference)+" actual="+std::to_string(actual));
            }
        }
    }
    // Conservative bucket construction must retain the original bounded
    // neighborhood even when very large, rotated ingredient support overlaps.
    auto wide=default_settings(Pattern::Moisture);wide.patternSize={16,16};wide.angleRandom=360;wide.disorder=1;
    PreparedPattern extreme(wide,1.f/32,1,4u*1024u*1024u);
    for(const Vec2 uv:{Vec2{.001f,.999f},Vec2{.317f,.571f},Vec2{.999f,.001f}})
        check(std::abs(extreme.sample(uv)-sample(uv,wide,1.f/32))<3e-6f,"Large-support prepared field changed the bounded source algorithm.");
}
void prepared_cancellation_and_concurrency() {
    auto settings=default_settings(Pattern::MultiscaleSpots);settings.scale=32;
    std::stop_source canceled;canceled.request_stop();
    bool rejected{};try{PreparedPattern field(settings,1.f/4096,1,64u*1024u*1024u,canceled.get_token());}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected,"Prepared source ignored cancellation before construction.");
    std::stop_source stop;std::promise<void> entered;auto started=entered.get_future();
    std::atomic<bool> done{},interrupted{};
    std::jthread worker([&] {
        entered.set_value();
        try{PreparedPattern field(settings,0,1,64u*1024u*1024u,stop.get_token());}
        catch(const std::runtime_error&){interrupted=true;}
        done=true;
    });
    started.wait();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool finishedBeforeRequest=done.load();stop.request_stop();worker.join();
    check(finishedBeforeRequest||interrupted.load(),"Stamp preparation ignored a live job cancellation.");
    settings=default_settings(Pattern::Clouds2);
    PreparedPattern shared(settings,1.f/128,1);
    constexpr std::array<Vec2,4> points{{{.137f,.271f},{.731f,.331f},{-.117f,1.581f},{1.0f,.5f}}};
    std::array<float,4> first{},second{};
    std::jthread a([&]{for(unsigned i=0;i<points.size();++i)first[i]=shared.sample(points[i]);});
    std::jthread b([&]{for(unsigned i=0;i<points.size();++i)second[i]=sample(points[i],settings,1.f/128);});
    a.join();b.join();
    for(unsigned i=0;i<points.size();++i) {
        check(std::abs(first[i]-second[i])<3e-6f,"Concurrent prepared sampling/oracle calls shared mutable context.");
        check(sample(points[i],settings,1.f/128)==second[i],"Prepared sampling leaked its thread-local cache into the oracle.");
    }
}
void batch_reference_parity() {
    constexpr unsigned width=31,height=23;
    for(const auto pattern:patterns)for(bool varied:{false,true}) {
        auto settings=default_settings(pattern);
        if(varied) {
            settings.seed=91;settings.scale=pattern==Pattern::DirectionalScratches?3.f:1.375f;
            settings.disorder=.391f;settings.angle=67;settings.angleRandom=173;
            settings.patternSize=pattern==Pattern::DirectionalScratches?Vec2{.09f,11}:Vec2{.7f,2.1f};
            settings.patternSizeRandom={.8f,.5f};settings.detailBalance=.73f;
            settings.contribution=.8f;settings.fibersAngleSpread=91;settings.threads=27;
        }
        const float footprint=1.f/128,aspect=float(width)/height;
        for(const auto budget:{std::size_t(0),std::size_t(64u*1024u*1024u)}) {
            PreparedPattern prepared(settings,footprint,aspect,budget);
            std::vector<float> output(std::size_t(width)*height);
            check(prepared.render_scratch_bytes(4096,4096)<2u*1024u*1024u,
                "Batch procedural rendering exceeded its bounded row scratch policy.");
            prepared.render(width,height,output);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const Vec2 uv{(float(x)+.5f)/width,(float(y)+.5f)/height};
                const float reference=sample(uv,settings,footprint,aspect),actual=output[std::size_t(y)*width+x];
                if(std::abs(actual-reference)>3e-6f)
                    throw std::runtime_error(std::string("Batch/reference mismatch: ")+pattern_name(pattern)+
                        " varied="+std::to_string(varied)+" budget="+std::to_string(budget)+
                        " pixel="+std::to_string(x)+","+std::to_string(y)+
                        " reference="+std::to_string(reference)+" actual="+std::to_string(actual));
            }
        }
    }
    auto settings=default_settings(Pattern::DirectionalScratches);settings.patternAmount=0;
    PreparedPattern empty(settings,1.f/128);std::vector<float> black(37*19,1);
    empty.render(37,19,black);
    check(std::all_of(black.begin(),black.end(),[](float value){return value==0;}),"Zero scratch amount failed in the compiled raster recipe.");
    std::stop_source canceled;canceled.request_stop();bool interrupted{};
    try{empty.render(37,19,black,canceled.get_token());}catch(const std::runtime_error&){interrupted=true;}
    check(interrupted,"Compiled raster ignored cancellation before its first tile.");
    rejects([&]{empty.render(0,19,black);},"Batch render accepted zero dimensions.");
    rejects([&]{empty.render(37,20,black);},"Batch render accepted a mismatched output span.");
}
void clouds1_resolved_controls() {
    constexpr unsigned extent=64;
    auto settings=default_settings(Pattern::Clouds1);
    const auto render=[&](const Settings& value) {
        PreparedPattern prepared(value,1.f/1024,1);
        std::vector<float> image(extent*extent);prepared.render(extent,extent,image);return image;
    };
    const auto first=render(settings);
    settings.disorder=.35f;const auto varied=render(settings);
    check(difference(first,varied)>.01,"Resolved Clouds 1 variation did not change its distribution.");
    settings.disorder=1;const auto repeated=render(settings);
    check(difference(first,repeated)<.0001,"Clouds 1 variation lost the original full-cycle endpoint behavior.");
    settings.disorder=0;settings.scale=2;const auto smaller=render(settings);
    check(difference(first,smaller)>.01,"Clouds 1 Scale 1 and 2 do not produce distinct feature scales.");
    // The original periodic phase is retained, without a claim of Adobe
    // algorithm or pixel parity. Intermediate controls must still be visible.
    for(const Vec2 uv:{Vec2{.137f,.271f},Vec2{.317f,.571f},Vec2{.731f,.331f}}) {
        settings.scale=1;settings.disorder=.5f;
        const float middle=sample(uv,settings,1.f/1024);
        PreparedPattern prepared(settings,1.f/1024);
        check(std::abs(middle-prepared.sample(uv))<3e-6f,"Clouds 1 prepared variation differs from its original oracle.");
    }
}
void benchmark(const char* destination,unsigned budget_ms) {
    using Clock=std::chrono::steady_clock;
    std::vector<std::string> records;
    const auto publish=[&] {
        std::ofstream output(destination,std::ios::trunc);
        if(!output)throw std::runtime_error("Could not write procedural benchmark JSON.");
        output<<"{\"measurement\":\"CPU scalar source generation; no GPU or host image cache\","
              <<"\"storage_scope\":\"allocated float scalar plane only; excludes host RGBA intermediates and upload mips\","
              <<"\"budget_ms_per_case\":"<<budget_ms<<",\"results\":[";
        for(std::size_t i=0;i<records.size();++i)output<<(i?",":"")<<records[i];
        output<<"]}\n";
    };
    publish();
    for(unsigned extent:{1024u,2048u,4096u})for(const auto pattern:patterns) {
        const auto start=Clock::now();const auto settings=default_settings(pattern);
        std::mutex deadlineMutex;std::condition_variable deadlineCv;bool finished{};
        std::stop_source stop;std::jthread deadline;
        if(budget_ms)deadline=std::jthread([&] {
            std::unique_lock lock(deadlineMutex);
            if(!deadlineCv.wait_for(lock,std::chrono::milliseconds(budget_ms),[&]{return finished;}))stop.request_stop();
        });
        std::optional<PreparedPattern> field;std::vector<float> pixels;
        std::size_t preparedBytes{},scratchBytes{};double checksum{};bool completed{};
        try {
            field.emplace(settings,1.f/extent,1,64u*1024u*1024u,stop.get_token());
            preparedBytes=field->memory_bytes();scratchBytes=field->render_scratch_bytes(extent,extent);
            pixels.resize(std::size_t(extent)*extent);
            field->render(extent,extent,pixels,stop.get_token());completed=true;
            for(float value:pixels)checksum+=value;
        } catch(const std::runtime_error&) {
            if(!stop.stop_requested()) {
                {std::lock_guard lock(deadlineMutex);finished=true;}deadlineCv.notify_one();throw;
            }
        }
        {std::lock_guard lock(deadlineMutex);finished=true;}deadlineCv.notify_one();
        if(deadline.joinable())deadline.join();
        const double seconds=std::chrono::duration<double>(Clock::now()-start).count();
        std::ostringstream record;
        record<<"{\"pattern\":"<<static_cast<unsigned>(pattern)<<",\"name\":\""<<pattern_name(pattern)
              <<"\",\"width\":"<<extent<<",\"height\":"<<extent<<",\"completed\":"<<(completed?"true":"false")
              <<",\"rows_completed\":"<<(completed?std::to_string(extent):"null")
              <<",\"pixels_processed\":"<<(completed?std::to_string(std::size_t(extent)*extent):"null")
              <<",\"total_pixels\":"<<std::size_t(extent)*extent<<",\"allocated_scalar_bytes\":"<<pixels.capacity()*sizeof(float)
              <<",\"prepared_bytes\":"<<preparedBytes<<",\"render_scratch_bytes\":"<<scratchBytes
              <<",\"seconds\":"<<seconds<<",\"checksum\":"<<checksum<<'}';
        records.push_back(record.str());publish();
        std::cout<<pattern_name(pattern)<<' '<<extent<<" seconds="<<seconds
                 <<(completed?" complete":" budget exhausted; incomplete")<<std::endl;
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc>=3&&std::string(argv[1])=="--benchmark") {
            unsigned budget{};
            if(argc==5&&(std::string(argv[3])=="--budget-ms"||std::string(argv[3])=="--benchmark-budget-ms"))
                budget=static_cast<unsigned>(std::stoul(argv[4]));
            else if(argc!=3)throw std::invalid_argument("Usage: material_procedural_tests --benchmark <json> [--budget-ms <per-case-ms>]");
            benchmark(argv[2],budget);return 0;
        }
        identities_and_defaults();deterministic_reference_samples();periodicity_and_sampling();
        meaningful_controls();scratch_coverage();prepared_reference_parity();prepared_cancellation_and_concurrency();batch_reference_parity();clouds1_resolved_controls();
        std::cout<<"Material procedural checks passed.\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
