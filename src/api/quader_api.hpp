////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_asset_api.hpp"
#include "api/quader_history_api.hpp"
#include "api/quader_layers_api.hpp"
#include "api/quader_modeling_api.hpp"
#include "api/quader_query_api.hpp"
#include "api/quader_scene_api.hpp"
#include "api/quader_selection_api.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_script.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace quader {

class QuaderEditorApi;
class QuaderObjectHandle;
class QuaderApiMutationRunner;

/**
 * Public document and modeling automation facade.
 *
 * Use this API for tests, examples, scripts, and tools that need scene, modeling, selection, asset,
 * history, or query workflows without runtime editor domains. Operations express user-document
 * intent and delegate to the owning domain APIs; this class is not an owner for document, modeling,
 * asset, history, storage, render, UI, viewport, or platform invariants.
 *
 * Use QuaderEditorApi instead when the workflow needs projects, commands, preferences, tools,
 * viewports, UI, events, or rendering. Keep public automation on this front door rather than on
 * backend objects or mutable service internals.
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
    [[nodiscard]] QuaderLayersApi &layers();
    [[nodiscard]] const QuaderLayersApi &layers() const;
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
    friend class QuaderEditorApi;
    friend class QuaderObjectHandle;
    friend class QuaderScript;

    void rebind(QuaderApiSession session,
                std::uint64_t scene_binding_identity = 1u);
    void set_dirty_changed_callback(std::function<void(bool)> callback);
    void synchronize_dirty_state();
    [[nodiscard]] std::uint64_t scene_binding_identity() const;
    [[nodiscard]] QuaderApiSession session() const;
    [[nodiscard]] QuaderApiMutationRunner& mutations();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
