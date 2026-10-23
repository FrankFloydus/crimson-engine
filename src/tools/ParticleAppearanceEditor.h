#pragma once

#include "particles/ParticleTypes.h"
#include "ColorGradientEditor.h"
#include <array>
#include <memory>

namespace crimson {

// A compound editor reports every constituent widget rather than only the
// last ImGui item. The asset document owns the resulting edit transaction.
struct ParticleAppearanceEdit {
    bool changed{},activated{},active{},deactivated{};
    void include(bool itemChanged=false);
    void merge(const ParticleAppearanceEdit&);
};
struct ParticleCurveEditorState {
    uint64_t selected{},dragging{};
    Vec2 plotMinimum{},plotMaximum{};
    float lower{},upper{},startMouseX{},startMouseY{},startAge{},startValue{};
    float contextAge{.5f},contextValue{};
    bool fitNext{true},panning{};
    void cancel() { dragging=0;panning=false; }
};
using ParticleGradientEditorState=ColorGradientEditorState;

ParticleAppearanceEdit drawParticleCurveEditor(ParticleScalarCurve&,ParticleCurveEditorState&,
    float minimum,float maximum,float resetValue,const char* valueLabel);
ParticleAppearanceEdit drawParticleGradientEditor(ParticleColorGradient&,ParticleGradientEditorState&,const Vec4& resetColor);

} // namespace crimson
