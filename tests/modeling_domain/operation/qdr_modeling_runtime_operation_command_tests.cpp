////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/operations/portable_operation_mapping.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace qec = quader_editor_core;
namespace qm = quader::modeling;

TEST(QdrModelingRuntimeOperationCommandTests,
     NativeRegistryProjectsEveryPortableDescriptorExactlyOnce) {
  const std::span<const qec::PortableOperationMapping> mappings =
      qec::portable_operation_mappings();
  const std::span<const qm::OperationDescriptor> descriptors =
      qm::operation_descriptors();
  const std::span<const qec::EditorOperationRegistryEntry> entries =
      qec::operation_registry().entries();

  ASSERT_EQ(mappings.size(), descriptors.size());
  ASSERT_EQ(entries.size(), mappings.size());

  std::set<int> native_ids;
  std::set<int> portable_kinds;
  std::size_t host_owned_count = 0;
  for (const qec::PortableOperationMapping &mapping : mappings) {
    EXPECT_TRUE(native_ids.insert(static_cast<int>(mapping.native_id)).second);
    EXPECT_TRUE(
        portable_kinds.insert(static_cast<int>(mapping.portable_kind)).second);

    const qm::OperationDescriptor *descriptor =
        qec::portable_operation_descriptor(mapping.native_id);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor,
              qm::operation_descriptor(mapping.portable_kind));
    EXPECT_FALSE(descriptor->id.empty());
    EXPECT_FALSE(descriptor->dispatch_id.empty());
    EXPECT_FALSE(descriptor->diagnostic_id.empty());

    const qec::EditorOperation *operation =
        qec::editor_operation_for_id(mapping.native_id);
    ASSERT_NE(operation, nullptr);
    EXPECT_EQ(operation->operation_id(), mapping.native_id);
    EXPECT_EQ(operation->id(), descriptor->id);
    EXPECT_EQ(operation->portable_descriptor(), descriptor);
    EXPECT_EQ(operation->supports_preview(), descriptor->supports_preview);
    EXPECT_EQ(qec::editor_operation_for_id(descriptor->id), operation);

    const std::optional<qec::EditorOperationCommandSchema> schema =
        qec::command_schema_for_operation(mapping.native_id);
    ASSERT_TRUE(schema.has_value());
    EXPECT_EQ(schema->operation_id, mapping.native_id);
    EXPECT_EQ(schema->preview_transaction_kind.has_value(),
              descriptor->supports_preview);

    if (descriptor->host_owned) {
      ++host_owned_count;
      EXPECT_EQ(mapping.native_id, qec::EditorOperationId::AssignMaterial);
      EXPECT_EQ(mapping.portable_kind, qm::OperationKind::AssignMaterialSlot);
    }
  }

  EXPECT_EQ(host_owned_count, 1u);
}

TEST(QdrModelingRuntimeOperationCommandTests,
     NativePresentationAddsLabelsWithoutCopyingPortableIdentity) {
  for (const qec::EditorOperationRegistryEntry &entry :
       qec::operation_registry().entries()) {
    ASSERT_NE(entry.operation, nullptr);
    ASSERT_NE(entry.operation->portable_descriptor(), nullptr);
    EXPECT_EQ(entry.id, entry.operation->portable_descriptor()->id);
    EXPECT_EQ(entry.class_name, std::string_view("PortableOperationDescriptor"));
    EXPECT_FALSE(entry.operation->label().empty());
  }
}

TEST(QdrModelingRuntimeOperationCommandTests,
     PreviewKindsAreAHostProjectionOfPortablePreviewCapability) {
  for (const qec::PortableOperationMapping &mapping :
       qec::portable_operation_mappings()) {
    const qm::OperationDescriptor *descriptor =
        qec::portable_operation_descriptor(mapping.native_id);
    ASSERT_NE(descriptor, nullptr);
    const std::optional<qec::PreviewTransactionKind> preview =
        qec::preview_transaction_kind_for_operation(mapping.native_id);
    EXPECT_EQ(preview.has_value(), descriptor->supports_preview);
    if (preview.has_value()) {
      EXPECT_EQ(qec::operation_for_preview_transaction_kind(*preview),
                mapping.native_id);
    }
  }
}
