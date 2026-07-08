////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

#include <string_view>

namespace quader_poly {
namespace {

/**
 * Implements the Assign Material Slot Operation modeling operation for the polygon document and mesh editing core.
 */
class AssignMaterialSlotOperation final : public PolyOperation {
public:
	explicit AssignMaterialSlotOperation(std::uint32_t material_slot)
		: material_slot_(material_slot)
	{
	}

	[[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::AssignSelectedFaceMaterialSlot).id; }
	[[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::AssignSelectedFaceMaterialSlot).label; }
	[[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
	std::uint32_t material_slot_ = 0;
};

OperationResult assign_material_slot_to_selection(
		Document& document,
		const Selection& selection,
		std::uint32_t material_slot)
{
	QDR_PROFILE_SCOPE("qdr_document.assign_selected_face_material_slot");
        if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
          return {false,
                  "Select one or more faces before assigning a material slot."};
        }

        bool found = false;
	bool changed = false;
	for (ElementId face_id : selection.faces) {
		Face* face = find_face(document, face_id);
		if (face == nullptr) {
			continue;
		}
		found = true;
		if (face->material_slot == material_slot) {
			continue;
		}
		face->material_slot = material_slot;
		changed = true;
	}

	if (!found) {
		return { false, "Selected faces were not found." };
	}
	if (!changed) {
		return { false, "Selected faces already use that material slot." };
	}
	return { true, {} };
}

OperationResult AssignMaterialSlotOperation::apply(Document& document, Selection& selection) const
{
	return assign_material_slot_to_selection(document, selection, material_slot_);
}

} // namespace

OperationResult assign_selected_face_material_slot(Document& document, const Selection& selection, std::uint32_t material_slot)
{
	Selection selection_copy = selection;
	return AssignMaterialSlotOperation { material_slot }.apply(document, selection_copy);
}

OperationResult assign_face_material_slot(Document& document, ElementId face_id, std::uint32_t material_slot)
{
	QDR_PROFILE_SCOPE("qdr_document.assign_face_material_slot");
	Face* face = find_face(document, face_id);
	if (face == nullptr) {
		return { false, "Material target face was not found." };
	}
	if (face->material_slot == material_slot) {
		return { false, "Face already uses that material slot." };
	}
	face->material_slot = material_slot;
	return { true, {} };
}

} // namespace quader_poly
