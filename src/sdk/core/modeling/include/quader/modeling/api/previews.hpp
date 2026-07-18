////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_contracts.hpp>

namespace quader::modeling {

/**
 * Handle to one pending public modeling preview operation.
 */
class PreviewHandle {
public:
  PreviewHandle() = default;
  [[nodiscard]] PreviewId id() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] PreviewTransactionSummary summary() const;
  [[nodiscard]] OperationReceipt update(BridgeOptions options);
  [[nodiscard]] OperationReceipt update(EdgeBevelOptions options);
  [[nodiscard]] OperationReceipt update(ThickenOptions options);
  [[nodiscard]] OperationReceipt update(SliceQuadOptions options);
  [[nodiscard]] OperationReceipt update(MergeByDistanceOptions options);
  [[nodiscard]] OperationReceipt update(TransformOptions options);
  [[nodiscard]] ToolPreviewPayload payload(PayloadRequest request = {}) const;
  [[nodiscard]] MeshPayload mesh_payload(PayloadRequest request = {}) const;
  [[nodiscard]] SemanticOverlayPayload
  overlay_payload(PayloadRequest request = {}) const;
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class VertexSelection;
  friend class EdgeSelection;
  friend class FaceSelection;
  friend class MeshTransform;
  explicit PreviewHandle(std::shared_ptr<ModelingPreviewState> state);
  std::shared_ptr<ModelingPreviewState> state_;
};

/**
 * Facade for the active session preview transaction.
 */
class PreviewApi {
public:
  PreviewApi() = default;
  [[nodiscard]] bool active() const;
  [[nodiscard]] PreviewTransactionSummary summary() const;
  [[nodiscard]] OperationReceipt commit(std::string label = {});
  [[nodiscard]] OperationReceipt cancel();

private:
  friend class ModelingApi;
  friend class ToolsApi;
  friend class TransformTool;
  explicit PreviewApi(std::shared_ptr<ModelingApiContext> context);
  std::shared_ptr<ModelingApiContext> context_ =
      detail::make_unbound_modeling_api_context();
};

} // namespace quader::modeling
