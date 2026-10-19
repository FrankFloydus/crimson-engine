#include <crimson/renderer/core/MeshInstances.hpp>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson::shared::core;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

int main() {
    try {
        std::vector<MeshInstanceRecord> accepted(100000), candidate;
        for (std::uint32_t index = 0; index < accepted.size(); ++index) {
            accepted[index].values[0] = accepted[index].values[5] = accepted[index].values[10] = accepted[index].values[15] = 1;
            accepted[index].values[12] = static_cast<float>(index);
            accepted[index].values[28] = static_cast<float>(index & 65535U);
            accepted[index].values[29] = static_cast<float>(index >> 16U);
        }
        std::vector<MeshInstanceRange> ranges;
        require(plan_mesh_instance_updates({},accepted,ranges).ok(),"Initial population rejected");
        require(ranges == std::vector<MeshInstanceRange>{{0,100000}},"Initial population must have one upload span");
        require(plan_mesh_instance_updates(accepted,accepted,ranges).ok() && ranges.empty(),"Unchanged population must upload nothing");
        candidate = accepted;
        candidate[65535].values[12] += 0.5F;
        require(plan_mesh_instance_updates(accepted,candidate,ranges).ok(),"Single edit rejected");
        require(ranges == std::vector<MeshInstanceRange>{{65535,1}},"Single edit must retain exact record range across draw boundary");
        candidate = accepted;
        for (std::uint32_t index = 0; index < 130; index += 2) candidate[index].values[13] = 1;
        require(plan_mesh_instance_updates(accepted,candidate,ranges).ok(),"Fragmented edits rejected");
        require(ranges == std::vector<MeshInstanceRange>{{0,129}},"Fragmented updates must bound upload commands");
        candidate = accepted;
        candidate.back().values[1] = std::numeric_limits<float>::quiet_NaN();
        require(!plan_mesh_instance_updates(accepted,candidate,ranges).ok() && ranges.empty(),"Invalid candidate must publish no update ranges");
        require(accepted.back().values[1] == 0,"Rejected candidate changed accepted population");
        require(plan_mesh_instance_updates(accepted,std::span<const MeshInstanceRecord>(accepted).first(90000),ranges).ok()
            && ranges.empty(),"Shrinking unchanged prefix must not reupload records");
        std::cout << "100000-record instance update contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
