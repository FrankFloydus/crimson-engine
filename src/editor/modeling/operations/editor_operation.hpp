////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/session.hpp>

#include <string_view>
#include <variant>

namespace quader_editor_core {

using EditorOperationSettings = std::variant<
		BridgeOperationSettings,
		EdgeBevelOperationSettings,
		ThickenOperationSettings,
		SliceQuadOperationSettings,
		MergeByDistanceOperationSettings,
		BevelVerticesOperationSettings,
		DuplicateMeshesOperationSettings,
		RecalculateFaceNormalsOperationSettings,
		AssignMaterialOperationSettings,
		SelectionOperationSettings>;

/**
 * Implements the Editor Operation modeling operation for the modeling runtime domain.
 */
class EditorOperation {
public:
	virtual ~EditorOperation();

	[[nodiscard]] virtual EditorOperationId operation_id() const = 0;
	[[nodiscard]] virtual std::string_view id() const = 0;
	[[nodiscard]] virtual std::string_view label() const = 0;
	[[nodiscard]] virtual bool supports_preview() const = 0;
	[[nodiscard]] virtual CommandResult execute(EditorSession& session, const EditorOperationSettings& settings) const = 0;
	[[nodiscard]] virtual CommandResult begin_preview(EditorSession&, const EditorOperationSettings&) const;
	[[nodiscard]] virtual CommandResult update_preview(EditorSession&, const EditorOperationSettings&) const;
};

} // namespace quader_editor_core
