<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Scene transforms

## Editable channels and parenting compensation

`TransformComponent` retains editable position, rotation and scale (`TRS`) and
an identity-default affine `parenting_offset` (`O`). The complete local matrix is
`O * TRS`; the world matrix is `parent_world * O * TRS`. Use
`scene_local_transform_matrix` for placement and
`scene_compensated_parent_matrix` for converting world edits into the editable
channel frame. Ordinary channel edits retain the stored offset.

Keep-world parenting changes the offset while retaining channels and custom
pivots. Offset compensation can remain on a root entity and can include shear
or reflection. Gizmo rotation and scale still reject requested channel edits
that cannot be represented as TRS in the compensated frame. Translation bases
are captured per entity: siblings need not have the same compensation.

Pivot rebasing and baking compensate descendants through their offsets rather
than decomposing inherited affine transforms. All before/after comparisons and
history snapshots include the offset.

## Mesh transform baking

[`MeshTransformBakeService`](mesh_transform_bake_service.hpp)
owns Bake Transforms, Bake Location, Bake Rotation,
and Bake Scale. The public entry point is `QuaderModelingApi::bake_transforms`.
These operations are available for editable authored polygon meshes in Mesh
(Object) selection mode.

Baking incorporates the chosen object transform channels into the authored mesh
and resets those channels. Location and rotation reset to zero; scale resets to
one. Untouched channels and parenting compensation retain their exact stored
values. Ordinary Inspector and
gizmo transforms continue to modify object placement without baking geometry.

For a mesh with world matrix `W_before` and its planned matrix `W_after`, transform
the local vertices by `B = inverse(W_after) * W_before`. This preserves world
positions even when selected ancestors are baked in the same operation. With no
changed ancestor, the common parent/compensation prefix cancels, leaving these
partial bake matrices for the editable `T * R * S` channels:

| Operation | Geometry transform |
| --- | --- |
| Location | Translation by `inverse(S) * inverse(R) * location` |
| Rotation | `inverse(S) * R * S` |
| Scale | `S` |
| Transforms | `T * R * S` |

The rotation conjugation is required when retained scale is nonuniform. A bake
must cancel retained placement before rounding the relative matrix to floats;
otherwise large scene coordinates can leak a translation into baked geometry.
The bake matrix may contain shear because mesh vertices can represent it.
An unselected child's offset absorbs the parent's change without decomposing the
child's transform or changing its editable channels. Preflight the entire
operation before publishing changes.

Bake Location and Bake Transforms reset location to zero in the compensated
channel frame; the retained parenting offset can keep that origin away from the
parent's origin.
Bake Rotation and Bake Scale preserve location. Baking must also preserve texture
appearance and corner correspondence, correct reflected winding, and isolate shared
mesh assets so unselected sharers remain unchanged. Geometry, mesh bindings, texture
mapping, and scene transforms form one undoable publication.

The behavior was checked against the local Blender source at
`C:/Users/Drako/Desktop/_QUADER_DEV/resources/_SOURCE_CODE/blender-main`, primarily
`source/blender/editors/object/object_transform.cc`: `apply_objects_internal`
contains rotation/scale conjugation and channel resets; `ignore_parent_tx` handles
child compensation. Quader uses its existing SDK projection and native history
owners for implementation.

Observable regression contracts live in
[mesh_transform_bake_tests.cpp](../../../tests/mesh_transform_bake_tests.cpp).
