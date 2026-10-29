#pragma once

#include "DecalInternal.h"
#include <span>

namespace crimson::decals::detail {

// Endpoint planes partition a contact's cells before its longitudinal cut.
// The spatial support is only a broad phase: a cell touching that support can
// extend well beyond the finite contact, so also test its open middle slab.
struct FiniteContactInterval {
    D3 first{},direction{};
    double extent{};

    bool intersects(std::span<const D3> polygon,double epsilon)const {
        bool before=true,after=true;
        for(const D3 point:polygon) {
            const double along=dot(point-first,direction);
            before&=along<=epsilon;
            after&=along>=extent-epsilon;
        }
        return !before&&!after;
    }
};

} // namespace crimson::decals::detail
