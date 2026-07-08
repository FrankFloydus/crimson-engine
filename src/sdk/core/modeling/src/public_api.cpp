////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

namespace quader::modeling {
using namespace detail;



ModelingException::ModelingException(Error error)
    : std::runtime_error(error.message), error_(std::move(error)) {}

const Error &ModelingException::error() const noexcept { return error_; }

CheckedModelingApi::CheckedModelingApi(
    std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

Result<MeshHandle> CheckedModelingApi::create_box(BoxOptions options) {
  Result<PolygonDocument> document = PolygonDocument::make_box({
      .min = options.min,
      .max = options.max,
      .material = options.material,
  });
  if (!document.ok()) {
    return Result<MeshHandle>::failure(document.error());
  }
  Result<ObjectId> object =
      context_->session.add_document(std::move(document).value(), options.name);
  if (!object.ok()) {
    return Result<MeshHandle>::failure(object.error());
  }
  return Result<MeshHandle>::success(MeshHandle(context_, object.value()));
}

Result<MeshHandle>
CheckedModelingApi::add_document(PolygonDocument document, std::string name) {
  Result<ObjectId> object =
      context_->session.add_document(std::move(document), std::move(name));
  if (!object.ok()) {
    return Result<MeshHandle>::failure(object.error());
  }
  return Result<MeshHandle>::success(MeshHandle(context_, object.value()));
}

CheckedIoApi CheckedModelingApi::io() { return CheckedIoApi(context_); }

CheckedToolsApi CheckedModelingApi::tools() { return CheckedToolsApi(context_); }

CheckedOperationsApi CheckedModelingApi::operations() {
  return CheckedOperationsApi(context_);
}

ModelingApi::ModelingApi()
    : context_(std::make_shared<ModelingApiContext>()) {}

ModelingApi::ModelingApi(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

ModelingApi ModelingApi::create(ModelingApiOptions options) {
  auto context = std::make_shared<ModelingApiContext>();
  context->options = options;
  return ModelingApi(std::move(context));
}

CheckedModelingApi ModelingApi::checked() {
  return CheckedModelingApi(context_);
}

const Error *ModelingApi::last_error() const {
  return context_->last_error ? &*context_->last_error : nullptr;
}

void ModelingApi::clear_error() { context_->last_error.reset(); }

MeshHandle ModelingApi::create_box(BoxOptions options) {
  Result<MeshHandle> result = checked().create_box(std::move(options));
  if (!result.ok()) {
    return handle_value_failure(*context_, result.error(), MeshHandle{});
  }
  context_->last_error.reset();
  return std::move(result).value();
}

MeshHandle ModelingApi::add_document(PolygonDocument document,
                                     std::string name) {
  Result<MeshHandle> result =
      checked().add_document(std::move(document), std::move(name));
  if (!result.ok()) {
    return handle_value_failure(*context_, result.error(), MeshHandle{});
  }
  context_->last_error.reset();
  return std::move(result).value();
}

BoxTool ModelingApi::activate_box_tool(BoxToolOptions options) {
  return BoxTool(context_, std::move(options));
}

KnifeTool ModelingApi::activate_knife_tool(KnifeToolOptions) {
  return KnifeTool(context_);
}

CutTool ModelingApi::activate_cut_tool(CutToolOptions options) {
  return CutTool(context_, options);
}

MeshHandle ModelingApi::mesh(ObjectId object) {
  return MeshHandle(context_, object);
}

MeshCollection ModelingApi::meshes() { return MeshCollection(context_); }

std::vector<MeshSummary> ModelingApi::mesh_summaries() const {
  std::vector<MeshSummary> result;
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    result.push_back(to_mesh_summary(summary));
  }
  return result;
}

std::vector<MeshHandle> ModelingApi::selected_meshes() const {
  std::vector<MeshHandle> result;
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    if (summary.selected) {
      result.push_back(MeshHandle(context_, summary.id));
    }
  }
  return result;
}

SelectionApi ModelingApi::selection() { return SelectionApi(context_); }
OperationsApi ModelingApi::operations() { return OperationsApi(context_); }
ToolsApi ModelingApi::tools() { return ToolsApi(context_); }
PreviewApi ModelingApi::preview() { return PreviewApi(context_); }
MeshSyncApi ModelingApi::mesh_sync() { return MeshSyncApi(context_); }
IoApi ModelingApi::io() { return IoApi(context_); }
CommandApi ModelingApi::commands() { return CommandApi(context_); }
ProfilingApi ModelingApi::profiling() { return ProfilingApi(context_); }
RenderApi ModelingApi::render() { return RenderApi(context_); }

ModelingBatch ModelingApi::batch(std::string label) {
  return ModelingBatch(context_, std::move(label));
}

OperationReceipt ModelingApi::undo() {
  Result<OperationResult> result = context_->session.undo();
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

OperationReceipt ModelingApi::redo() {
  Result<OperationResult> result = context_->session.redo();
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

bool ModelingApi::can_undo() const { return context_->session.can_undo(); }
bool ModelingApi::can_redo() const { return context_->session.can_redo(); }

RevisionStamp ModelingApi::revisions() const {
  return current_revisions(*context_);
}

} // namespace quader::modeling
