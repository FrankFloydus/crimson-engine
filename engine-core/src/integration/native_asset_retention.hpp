#pragma once

#include "assets/AssetReferenceManager.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace quader { class ProjectSession; class SceneDocument; }
namespace crimson::integration {

// Native QDR is authoritative. This service indexes declared immutable payload
// references without owning scene content, history, or any replacement policy.
class NativeAssetRetention final {
public:
    explicit NativeAssetRetention(quader::ProjectSession&);
    ~NativeAssetRetention();
    NativeAssetRetention(const NativeAssetRetention&) = delete;
    NativeAssetRetention& operator=(const NativeAssetRetention&) = delete;
    void observe(quader::SceneDocument&);
    // Explicit Find References/deletion boundary only; may read saved QDR metadata.
    std::vector<AssetUsage> snapshot();
    std::uint64_t revision() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
