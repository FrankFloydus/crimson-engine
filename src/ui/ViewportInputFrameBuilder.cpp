#include "ui/ViewportInputFrameBuilder.h"

#include <algorithm>

namespace quader {
namespace {

void addPressedKey(ViewportInputFrame& input, ImGuiKey imguiKey, ViewportKey viewportKey) {
    if (ImGui::IsKeyPressed(imguiKey, false)) {
        input.pressedKeys.push_back(viewportKey);
    }
}

void addPressedKeyPair(ViewportInputFrame& input, ImGuiKey primaryKey, ImGuiKey keypadKey, ViewportKey viewportKey) {
    if (ImGui::IsKeyPressed(primaryKey, false) || ImGui::IsKeyPressed(keypadKey, false)) {
        input.pressedKeys.push_back(viewportKey);
    }
}

} // namespace

ViewportInputFrame ViewportInputFrameBuilder::build(const ViewportInputFrameBuildRequest& request) const {
    ImGuiIO& io = ImGui::GetIO();
    ViewportInputFrame input;
    input.viewportHovered = request.hovered;
    input.viewportFocused = request.focused;
    input.textInputActive = io.WantTextInput;
    input.middlePressed = (request.hovered || request.routeActiveNavigationInput) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    input.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.middleReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    input.rightPressed = request.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    input.mouseWheel = (request.hovered || request.routeActiveNavigationInput) ? io.MouseWheel : 0.0f;
    input.deltaSeconds = std::max(io.DeltaTime, 0.0f);
    input.mouseScreen = {io.MousePos.x, io.MousePos.y};
    input.mouseDelta = request.useCapturedMouseDelta ? request.capturedMouseDelta : Vec2{io.MouseDelta.x, io.MouseDelta.y};
    input.mouseLocal = {io.MousePos.x - request.itemMin.x, io.MousePos.y - request.itemMin.y};
    input.viewportSize = {request.itemSize.x, request.itemSize.y};
    input.modifiers = {
            .primary = io.KeyCtrl || io.KeySuper,
            .shift = io.KeyShift,
            .alt = io.KeyAlt,
    };

    if (ImGui::IsKeyDown(ImGuiKey_W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
        input.flyLocalDirection.x -= 1.0f;
    }

    addPressedKey(input, ImGuiKey_Q, ViewportKey::Q);
    addPressedKey(input, ImGuiKey_W, ViewportKey::W);
    addPressedKey(input, ImGuiKey_A, ViewportKey::A);
    addPressedKey(input, ImGuiKey_D, ViewportKey::D);
    addPressedKey(input, ImGuiKey_H, ViewportKey::H);
    addPressedKey(input, ImGuiKey_I, ViewportKey::I);
    addPressedKey(input, ImGuiKey_M, ViewportKey::M);
    addPressedKey(input, ImGuiKey_R, ViewportKey::R);
    addPressedKey(input, ImGuiKey_S, ViewportKey::S);
    addPressedKey(input, ImGuiKey_B, ViewportKey::B);
    addPressedKey(input, ImGuiKey_C, ViewportKey::C);
    addPressedKey(input, ImGuiKey_P, ViewportKey::P);
    addPressedKey(input, ImGuiKey_T, ViewportKey::T);
    addPressedKey(input, ImGuiKey_V, ViewportKey::V);
    addPressedKey(input, ImGuiKey_X, ViewportKey::X);
    addPressedKey(input, ImGuiKey_Y, ViewportKey::Y);
    addPressedKey(input, ImGuiKey_Z, ViewportKey::Z);
    addPressedKeyPair(input, ImGuiKey_1, ImGuiKey_Keypad1, ViewportKey::Number1);
    addPressedKeyPair(input, ImGuiKey_2, ImGuiKey_Keypad2, ViewportKey::Number2);
    addPressedKeyPair(input, ImGuiKey_3, ImGuiKey_Keypad3, ViewportKey::Number3);
    addPressedKeyPair(input, ImGuiKey_4, ImGuiKey_Keypad4, ViewportKey::Number4);
    addPressedKeyPair(input, ImGuiKey_5, ImGuiKey_Keypad5, ViewportKey::Number5);
    addPressedKey(input, ImGuiKey_Home, ViewportKey::Home);
    addPressedKey(input, ImGuiKey_F, ViewportKey::F);
    addPressedKey(input, ImGuiKey_Space, ViewportKey::Space);
    addPressedKey(input, ImGuiKey_F1, ViewportKey::F1);
    addPressedKey(input, ImGuiKey_F2, ViewportKey::F2);
    addPressedKey(input, ImGuiKey_F3, ViewportKey::F3);
    addPressedKey(input, ImGuiKey_F4, ViewportKey::F4);
    addPressedKey(input, ImGuiKey_F5, ViewportKey::F5);
    addPressedKey(input, ImGuiKey_F6, ViewportKey::F6);
    addPressedKey(input, ImGuiKey_F7, ViewportKey::F7);
    addPressedKey(input, ImGuiKey_F8, ViewportKey::F8);
    addPressedKey(input, ImGuiKey_F9, ViewportKey::F9);
    addPressedKey(input, ImGuiKey_F10, ViewportKey::F10);
    addPressedKey(input, ImGuiKey_Escape, ViewportKey::Escape);
    addPressedKey(input, ImGuiKey_Delete, ViewportKey::Delete);
    addPressedKey(input, ImGuiKey_Backspace, ViewportKey::Backspace);

    return input;
}

} // namespace quader
