////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "scene/document/project_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path stressProjectPath() {
    if (const char* overridePath = std::getenv("QUADER_STRESS_QDR");
            overridePath != nullptr && overridePath[0] != '\0') {
        return std::filesystem::path(overridePath);
    }
    return std::filesystem::path(LR"(C:\Users\Drako\Desktop\my_first_project.qdr)");
}

template <typename Step>
double timedStressStep(std::string_view label, Step&& step) {
    const auto start = std::chrono::steady_clock::now();
    step();
    const auto stop = std::chrono::steady_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(stop - start).count();
    std::cout << "[local-stress] " << label << "_ms=" << elapsedMs << '\n';
    return elapsedMs;
}

std::size_t selectedEntityCount(const quader::QuaderEditorApi& editor) {
    std::size_t count = 0;
    for (const quader::QuaderEntitySnapshot& entity : editor.query().entities()) {
        if (entity.selected) {
            ++count;
        }
    }
    return count;
}

quader::QuaderRay stressViewportForwardRay(float x = 0.0f) {
    return {
            .origin = {x, 0.0f, 10.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
}

quader::QuaderViewportCamera stressOrthographicViewportCamera() {
    return {
            .orthographic = true,
            .origin = {0.0f, 0.0f, 10.0f},
            .xAxis = {1.0f, 0.0f, 0.0f},
            .yAxis = {0.0f, 1.0f, 0.0f},
            .zAxis = {0.0f, 0.0f, 1.0f},
            .viewportSize = {100.0f, 100.0f},
            .orthographicSize = 100.0,
            .fovDegrees = 60.0,
    };
}

quader::QuaderTransformDragBeginRequest stressXMoveDragBeginRequest() {
    return {
            .tool = quader::QuaderTransformTool::Move,
            .operation = quader::QuaderTransformOperation::Transform,
            .axis = quader::QuaderTransformAxis::X,
            .screenPosition = {0.0f, 0.0f},
            .screenPivot = {0.0f, 0.0f},
            .screenTip = {10.0f, 0.0f},
            .hasWorldDirection = true,
            .worldDirection = {1.0f, 0.0f, 0.0f},
            .ray = stressViewportForwardRay(),
            .camera = stressOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.0,
            .gridSnapEnabled = false,
    };
}

quader::QuaderTransformDragUpdateRequest stressXMoveDragUpdateRequest(float screenX) {
    return {
            .screenPosition = {screenX, 0.0f},
            .ray = stressViewportForwardRay(),
            .camera = stressOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.0,
            .gridSnapEnabled = false,
            .precision = false,
    };
}

} // namespace

TEST(LocalStressTests, MyFirstProjectLargeWorkflow) {
    const std::filesystem::path projectPath = stressProjectPath();
    if (!std::filesystem::exists(projectPath)) {
        GTEST_SKIP() << "Local stress project is missing: " << projectPath.string();
    }

    quader::QdrProjectReadResult read;
    timedStressStep("qdr_read", [&] {
        read = quader::QdrDocumentReader::readProjectDocument(projectPath);
    });
    ASSERT_TRUE(read.ok) << read.message;

    quader::ProjectSessionService sessions;
    std::unique_ptr<quader::ProjectSession> session;
    timedStressStep("session_open", [&] {
        session = sessions.openSession(std::move(read.document));
    });
    ASSERT_NE(session, nullptr);

    quader::QuaderEditorApi editor(quader::QuaderApiSessionInternalAccess::fromProjectSession(*session));
    const std::vector<quader::QuaderEntitySnapshot> loadedEntities = editor.query().entities();
    std::cout << "[local-stress] entity_count=" << loadedEntities.size() << '\n';

    timedStressStep("select_all", [&] {
        const quader::QuaderApiVoidResult selected = editor.selection().selectAll();
        ASSERT_TRUE(selected.ok()) << selected.errorMessage();
    });
    const std::size_t selectedCount = selectedEntityCount(editor);
    std::cout << "[local-stress] selected_entity_count=" << selectedCount << '\n';
    ASSERT_GT(selectedCount, 0u);

    timedStressStep("move_selection", [&] {
        const quader::QuaderApiVoidResult moved = editor.modeling().moveSelection({0.001f, 0.0f, 0.0f});
        ASSERT_TRUE(moved.ok()) << moved.errorMessage();
    });

    timedStressStep("rotate_selection", [&] {
        const quader::QuaderApiVoidResult rotated = editor.modeling().rotateSelection({0.0f, 0.001f, 0.0f});
        ASSERT_TRUE(rotated.ok()) << rotated.errorMessage();
    });

    timedStressStep("scale_selection", [&] {
        const quader::QuaderApiVoidResult scaled = editor.modeling().scaleSelection({1.001f, 1.0f, 1.0f});
        ASSERT_TRUE(scaled.ok()) << scaled.errorMessage();
    });

    timedStressStep("transform_drag_move", [&] {
        quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
                editor.modeling().beginTransformDrag(stressXMoveDragBeginRequest());
        ASSERT_TRUE(begin.ok) << begin.message;
        ASSERT_TRUE(begin.value().active);
        quader::QuaderApiResult<quader::QuaderTransformDragPreview> update =
                editor.modeling().updateTransformDrag(stressXMoveDragUpdateRequest(2.0f));
        ASSERT_TRUE(update.ok) << update.message;
        quader::QuaderApiResult<quader::QuaderTransformDragPreview> commit = editor.modeling().commitTransformDrag();
        ASSERT_TRUE(commit.ok) << commit.message;
    });

    timedStressStep("duplicate_selection", [&] {
        const quader::QuaderApiVoidResult duplicated = editor.modeling().duplicateSelectedEntityHierarchy();
        ASSERT_TRUE(duplicated.ok()) << duplicated.errorMessage();
    });

    timedStressStep("invert_mesh", [&] {
        const quader::QuaderApiVoidResult inverted =
                editor.commands().execute(quader::QuaderCommand::ModelingInvertMesh);
        if (!inverted.ok()) {
            const std::string error = inverted.errorMessage();
            if (error.find("disabled") != std::string::npos) {
                std::cout << "[local-stress] invert_mesh_disabled=" << error << '\n';
                return;
            }
        }
        ASSERT_TRUE(inverted.ok()) << inverted.errorMessage();
    });

    const std::filesystem::path savePath =
            std::filesystem::temp_directory_path() / "quader_local_stress_my_first_project.qdr";
    std::error_code removeError;
    std::filesystem::remove(savePath, removeError);
    timedStressStep("save_after_transform_and_invert", [&] {
        const quader::QuaderApiVoidResult saved = editor.project().saveAs(savePath);
        ASSERT_TRUE(saved.ok()) << saved.errorMessage();
    });
    EXPECT_TRUE(std::filesystem::exists(savePath));

    quader::QdrProjectReadResult compactRead;
    timedStressStep("qdr_read_saved_compact", [&] {
        compactRead = quader::QdrDocumentReader::readProjectDocument(savePath);
    });
    ASSERT_TRUE(compactRead.ok) << compactRead.message;

    timedStressStep("delete_selection", [&] {
        const quader::QuaderApiVoidResult deleted = editor.commands().execute(quader::QuaderCommand::DeleteSelection);
        ASSERT_TRUE(deleted.ok()) << deleted.errorMessage();
    });

    std::filesystem::remove(savePath, removeError);
}
