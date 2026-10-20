#include <crimson/renderer/forward/MaterialProcedural.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
using namespace crimson::material_procedural;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
// Sample resolved footprints at asymmetric positions, independently of any
// presentation hash. These are field-sensitivity probes, not coarse previews.
std::vector<float> probes(const Settings& settings,float aspect=2) {
    PreparedPattern pattern(settings,1.f/4096,aspect);
    std::vector<float> values;values.reserve(768);
    for(unsigned y=0;y<24;++y)for(unsigned x=0;x<32;++x) {
        const Vec2 uv{(float(x)+.217f)/32,(float(y)+.391f)/24};
        const auto value=pattern.sample(uv);
        require(std::isfinite(value)&&value>=0&&value<=1,"Procedural control produced invalid pixels.");
        values.push_back(value);
    }
    return values;
}
double difference(const std::vector<float>& a,const std::vector<float>& b) {
    require(a.size()==b.size(),"Mismatched procedural probe sizes.");
    double sum{};for(std::size_t i=0;i<a.size();++i)sum+=std::abs(double(a[i])-b[i]);return sum/a.size();
}
void activeControls() {
    unsigned checked{};
    for(const auto family:patterns) {
        auto baseline=default_settings(family);baseline.seed=19;baseline.disorder=.13f;
        // Rotating a circular stamp is intentionally invariant. An elongated
        // fixture exercises the enabled orientation controls meaningfully.
        if(family==Pattern::Moisture)baseline.patternSize={.6f,1.4f};
        const auto active=controls(family);const auto original=probes(baseline);
        const auto changes=[&](const char* name,const std::function<void(Settings&)>& edit) {
            auto changed=baseline;edit(changed);validate(changed);
            require(difference(original,probes(changed))>1e-6,
                std::string(pattern_name(family))+" / "+name+" did not change resolved source pixels.");
            ++checked;
        };
        if(active.seed)changes("Seed",[](auto& value){value.seed=317;});
        if(active.scale)changes("Scale or density",[](auto& value){
            value.scale=value.pattern==Pattern::Clouds1||value.pattern==Pattern::Clouds2?2.f:
                value.pattern==Pattern::DirectionalScratches?6.f:value.pattern==Pattern::GaussianNoise?16.f:2.f;
        });
        if(active.disorder)changes("Variation",[](auto& value){value.disorder=.47f;});
        if(active.detailBalance)changes("Detail balance",[](auto& value){value.detailBalance=.17f;});
        if(active.patternSize) {
            changes("Width",[](auto& value){value.patternSize[0]*=.65f;});
            changes("Height or length",[](auto& value){value.patternSize[1]*=.7f;});
        }
        if(active.angle)changes("Angle",[](auto& value){value.angle=47;});
        if(active.angleRandom)changes("Angle randomness",[](auto& value){value.angleRandom=113;});
        if(active.patternAmount)changes("Pattern amount",[](auto& value){value.patternAmount=.42f;});
        if(active.patternSizeRandom) {
            changes("Width randomness",[](auto& value){value.patternSizeRandom[0]=.6f;});
            changes("Length randomness",[](auto& value){value.patternSizeRandom[1]=.6f;});
        }
        if(active.nonSquareExpansion)changes("Keep square tiles on a rectangular source",[](auto& value){value.nonSquareExpansion=false;});
        if(active.contribution)changes("Contribution",[](auto& value){value.contribution=.7f;});
        if(active.fibersAngleSpread)changes("Angle spread",[](auto& value){value.fibersAngleSpread=43;});
        if(active.threads)changes("Threads",[](auto& value){value.threads=7;});
        std::cout<<pattern_name(family)<<": active pixel controls passed.\n";
    }
    require(checked>=50,"Procedural property inventory unexpectedly lost active controls.");
    std::cout<<checked<<" procedural properties changed resolved pixels.\n";
}
void intentionalInvariances() {
    auto circles=default_settings(Pattern::Moisture);circles.seed=19;circles.patternSize={1,1};
    const auto reference=probes(circles);circles.angle=47;
    require(difference(reference,probes(circles))<1e-5,"Rotating circular Moisture stamps changed their shape.");
    auto scratches=default_settings(Pattern::DirectionalScratches);scratches.seed=19;
    const auto square=probes(scratches,1);scratches.nonSquareExpansion=false;
    require(difference(square,probes(scratches,1))==0,"Square-cell policy changed an already square source.");
    auto parabola=default_settings(Pattern::ParabolicGradient);const auto profile=probes(parabola);
    parabola.seed=71;parabola.disorder=.43f;
    require(difference(profile,probes(parabola))==0,"Hidden random controls altered deterministic Parabolic Gradient.");
}
}
int main(){try{activeControls();intentionalInvariances();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
