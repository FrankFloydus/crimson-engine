////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_script.hpp"

#include "api/quader_api.hpp"
#include "api/quader_api_mutation_runner.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace quader {
namespace {

void merge_script_step_status(QuaderScriptResult &result,
                              const QuaderApiStatus &status) {
  if (status.changed) {
    result.changed = true;
  }
  if (status.mutation_kind != QuaderMutationKind::None) {
    result.mutation_kind = status.mutation_kind;
  }
  result.revisions = status.revisions;
}

std::string script_transaction_label(std::string_view label) {
  return label.empty() ? std::string("Script") : std::string(label);
}

QuaderApiStatus script_history_step_status(const QuaderApi &api) {
  QuaderApiStatus status;
  status.ok = false;
  status.changed = false;
  status.message = "History operations are not supported inside QuaderScript.";
  status.revisions = api.revisions();
  return status;
}

} // namespace

QuaderScript::QuaderScript(QuaderApi& api, std::string label)
    : api_(api)
    , label_(std::move(label)) {}

QuaderScript &QuaderScript::create_box(std::string alias, QuaderBounds bounds,
                                       std::string name) {
  const std::string step_label = "createBox(" + alias + ")";
  return add_step(step_label,
                  [this, alias = std::move(alias), bounds,
                   name = std::move(name)](QuaderScriptResult &result) mutable {
                    const QuaderApiResult<QuaderModelingObjectRef> created =
                        api_.modeling().create_box(bounds, std::move(name));
                    if (created.ok) {
                      result.objects[alias] = created.value;
                    }
                    return quader_api_status_from_result(created);
                  });
}

QuaderScript &QuaderScript::create_mesh(std::string alias,
                                        QuaderPolygonDocument document,
                                        QuaderCreateMeshArgs args) {
  auto stored_document = std::make_shared<std::optional<QuaderPolygonDocument>>(
      std::move(document));
  const std::string step_label = "createMesh(" + alias + ")";
  return add_step(
      step_label, [this, alias = std::move(alias), stored_document,
                   args = std::move(args)](QuaderScriptResult &result) mutable {
        if (!stored_document->has_value()) {
          QuaderApiStatus status;
          status.ok = false;
          status.changed = false;
          status.message = "Script mesh document was already consumed.";
          status.revisions = api_.revisions();
          return status;
        }

        const QuaderApiResult<QuaderModelingObjectRef> created =
            api_.modeling().create_mesh(std::move(**stored_document), args);
        stored_document->reset();
        if (created.ok) {
          result.objects[alias] = created.value;
        }
        return quader_api_status_from_result(created);
      });
}

QuaderScript& QuaderScript::select(std::string alias) {
  const std::string step_label = "select(" + alias + ")";
  return add_step(step_label,
                  [this, alias = std::move(alias)](QuaderScriptResult &result) {
                    const auto found = result.objects.find(alias);
                    if (found == result.objects.end()) {
                      return missing_alias_status(alias);
                    }
                    return quader_api_status_from_result(
                        api_.selection().select_object(found->second));
                  });
}

QuaderScript &QuaderScript::select_vertex(std::string alias,
                                          QuaderElementId vertex) {
  const std::string step_label = "selectVertex(" + alias + ")";
  return add_step(step_label, [this, alias = std::move(alias),
                               vertex](QuaderScriptResult &result) {
    const auto found = result.objects.find(alias);
    if (found == result.objects.end()) {
      return missing_alias_status(alias);
    }
    return quader_api_status_from_result(
        api_.selection().select_vertex(found->second, vertex));
  });
}

QuaderScript &QuaderScript::select_edge(std::string alias, QuaderElementId a,
                                        QuaderElementId b) {
  const std::string step_label = "selectEdge(" + alias + ")";
  return add_step(step_label, [this, alias = std::move(alias), a,
                               b](QuaderScriptResult &result) {
    const auto found = result.objects.find(alias);
    if (found == result.objects.end()) {
      return missing_alias_status(alias);
    }
    return quader_api_status_from_result(
        api_.selection().select_edge(found->second, a, b));
  });
}

QuaderScript &QuaderScript::select_face(std::string alias,
                                        QuaderElementId face) {
  const std::string step_label = "selectFace(" + alias + ")";
  return add_step(step_label, [this, alias = std::move(alias),
                               face](QuaderScriptResult &result) {
    const auto found = result.objects.find(alias);
    if (found == result.objects.end()) {
      return missing_alias_status(alias);
    }
    return quader_api_status_from_result(
        api_.selection().select_face(found->second, face));
  });
}

QuaderScript &QuaderScript::clear_selection() {
  return add_step("clearSelection", [this](QuaderScriptResult &) {
    return quader_api_status_from_result(api_.selection().clear());
  });
}

QuaderScript &QuaderScript::set_selection_mode(QuaderSelectionMode mode) {
  return add_step("setSelectionMode", [this, mode](QuaderScriptResult &) {
    return quader_api_status_from_result(api_.selection().set_mode(mode));
  });
}

QuaderScript &QuaderScript::move_selection(QVec3 delta) {
  return add_step("moveSelection", [this, delta](QuaderScriptResult &) {
    return quader_api_status_from_result(api_.modeling().move_selection(delta));
  });
}

QuaderScript &QuaderScript::rotate_selection(QVec3 euler_radians, QVec3 pivot) {
  return add_step("rotateSelection",
                  [this, euler_radians, pivot](QuaderScriptResult &) {
                    return quader_api_status_from_result(
                        api_.modeling().rotate_selection(euler_radians, pivot));
                  });
}

QuaderScript &QuaderScript::scale_selection(QVec3 scale, QVec3 pivot) {
  return add_step("scaleSelection", [this, scale, pivot](QuaderScriptResult &) {
    return quader_api_status_from_result(
        api_.modeling().scale_selection(scale, pivot));
  });
}

QuaderScript &QuaderScript::execute_tool(std::string tool_id) {
  return add_step("executeTool(" + tool_id + ")",
                  [this, tool_id = std::move(tool_id)](QuaderScriptResult &) {
                    return quader_api_status_from_result(
                        api_.modeling().execute_tool(tool_id));
                  });
}

QuaderScript &QuaderScript::assign_material_slot(std::uint32_t material_slot) {
  return add_step(
      "assignMaterialSlot", [this, material_slot](QuaderScriptResult &) {
        return quader_api_status_from_result(
            api_.modeling().assign_material_slot_to_selection(material_slot));
      });
}

QuaderScript& QuaderScript::undo() {
  return add_step("undo", [this](QuaderScriptResult &) {
    return script_history_step_status(api_);
  });
}

QuaderScript& QuaderScript::redo() {
  return add_step("redo", [this](QuaderScriptResult &) {
    return script_history_step_status(api_);
  });
}

QuaderScriptResult QuaderScript::run() {
    QuaderScriptResult result;
    QuaderApiMutationRunner& mutations = api_.mutations();
    ModelingSessionMemento before = mutations.capture_modeling_memento();
    EditorTransaction transaction =
        mutations.begin_transaction(script_transaction_label(label_));
    if (!transaction.active()) {
        result.ok = false;
        result.changed = false;
        result.message = "Cannot run script while another editor transaction is active.";
        result.revisions = api_.revisions();
        result.final_snapshot = api_.snapshot();
        return result;
    }

    for (const Step& step : steps_) {
      QuaderApiStatus status =
          quader_api_status_from_result(step.execute(result));
      result.steps.push_back({
          .label = step.label,
          .status = status,
      });

      if (!status.ok) {
        result.ok = false;
        result.message = status.message;
        merge_script_step_status(result, status);
        break;
      }
      merge_script_step_status(result, status);
    }

    if (!result.ok) {
        const CommandResult canceled = transaction.cancel();
        const QuaderApiVoidResult restored =
            mutations.restore_modeling_memento(before, "Rollback Script");
        result.objects.clear();
        result.changed = false;
        result.mutation_kind = QuaderMutationKind::None;
        result.revisions = restored.revisions;
        if (!canceled.ok || !restored.ok) {
            if (!result.message.empty()) {
                result.message += "; ";
            }
            result.message += !canceled.ok ? canceled.message : restored.message;
        }
        result.final_snapshot = api_.snapshot();
        return result;
    }

    if (result.changed) {
      const ModelingSessionMemento after = mutations.capture_modeling_memento();
      const CommandResult recorded_modeling = transaction.record_executed(
          std::make_unique<RecordedModelingMementoCommand>(
              QuaderApiSessionInternalAccess::project_session(api_.session())
                  .modeling(),
              script_transaction_label(label_), before, after));
      if (!recorded_modeling.ok) {
        result.ok = false;
        result.changed = false;
        result.mutation_kind = QuaderMutationKind::None;
        result.message = recorded_modeling.message;
        result.revisions = api_.revisions();
        result.final_snapshot = api_.snapshot();
        return result;
      }
    }

    const CommandResult committed = transaction.commit();
    if (!committed.ok) {
        result.ok = false;
        result.changed = false;
        result.mutation_kind = QuaderMutationKind::None;
        result.message = committed.message;
        result.revisions = api_.revisions();
        result.final_snapshot = api_.snapshot();
        return result;
    }

    if (result.changed || committed.changed) {
      const QuaderApiVoidResult recorded = mutations.run_document_command(
          committed, script_transaction_label(label_));
      copy_quader_api_status(result, recorded);
    } else {
        result.revisions = api_.revisions();
    }

    result.final_snapshot = api_.snapshot();
    return result;
}

QuaderScript &QuaderScript::add_step(std::string label, StepFunction execute) {
  steps_.push_back({
      .label = std::move(label),
      .execute = std::move(execute),
  });
  return *this;
}

QuaderApiStatus
QuaderScript::missing_alias_status(std::string_view alias) const {
  QuaderApiStatus status;
  status.ok = false;
  status.changed = false;
  status.message = "Unknown script object alias: " + std::string(alias);
  status.revisions = api_.revisions();
  return status;
}

} // namespace quader
