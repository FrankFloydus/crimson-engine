////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_asset_api.hpp"
#include "api/quader_history_api.hpp"
#include "api/quader_modeling_api.hpp"
#include "api/quader_query_api.hpp"
#include "api/quader_scene_api.hpp"
#include "api/quader_selection_api.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_script.hpp"

#include <memory>

namespace quader {

class QuaderObjectHandle;
class QuaderApiMutationRunner;

/**
 * Exposes Quader API operations through the public Quader API surface.
 */
class QuaderApi final {
public:
    explicit QuaderApi(QuaderApiSession session);
    ~QuaderApi();

    QuaderApi(const QuaderApi&) = delete;
    QuaderApi& operator=(const QuaderApi&) = delete;
    QuaderApi(QuaderApi&&) noexcept;
    QuaderApi& operator=(QuaderApi&&) noexcept;

    [[nodiscard]] QuaderSceneApi& scene();
    [[nodiscard]] const QuaderSceneApi& scene() const;
    [[nodiscard]] QuaderModelingApi& modeling();
    [[nodiscard]] const QuaderModelingApi& modeling() const;
    [[nodiscard]] QuaderSelectionApi& selection();
    [[nodiscard]] const QuaderSelectionApi& selection() const;
    [[nodiscard]] QuaderAssetApi& assets();
    [[nodiscard]] const QuaderAssetApi& assets() const;
    [[nodiscard]] QuaderHistoryApi& history();
    [[nodiscard]] const QuaderHistoryApi& history() const;
    [[nodiscard]] QuaderQueryApi& query();
    [[nodiscard]] const QuaderQueryApi& query() const;

    [[nodiscard]] QuaderApiSnapshot snapshot() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;
    [[nodiscard]] QuaderScript script(std::string label = {});
    [[nodiscard]] QuaderObjectHandle object(QuaderModelingObjectRef ref);

private:
    friend class QuaderObjectHandle;
    friend class QuaderScript;

    [[nodiscard]] QuaderApiSession session() const;
    [[nodiscard]] QuaderApiMutationRunner& mutations();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
