<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Mesh transform baking

[`MeshTransformBakeService`](mesh_transform_bake_service.hpp)
owns Bake Transforms, Bake Location, Bake Rotation,
and Bake Scale. The public entry point is `QuaderModelingApi::bake_transforms`.
These operations are available for editable authored polygon meshes in Mesh
(Object) selection mode.

Baking incorporates the chosen object transform channels into the authored mesh
and resets those channels. Location and rotation reset to zero; scale resets to
one. Untouched channels retain their exact stored values. Ordinary Inspector and
gizmo transforms continue to modify object placement without baking geometry.

For a mesh with world matrix `W_before` and its planned matrix `W_after`, transform
the local vertices by `B = inverse(W_after) * W_before`. This preserves world
positions even when selected ancestors are baked in the same operation. With no
changed ancestor and local matrix `T * R * S`, the partial bake matrices are:

| Operation | Geometry transform |
| --- | --- |
| Location | Translation by `inverse(S) * inverse(R) * location` |
| Rotation | `inverse(S) * R * S` |
| Scale | `S` |
| Transforms | `T * R * S` |

The rotation conjugation is required when retained scale is nonuniform. A bake
must cancel retained placement before rounding the relative matrix to floats;
otherwise large scene coordinates can leak a translation into baked geometry.
The bake matrix may contain shear because mesh vertices can represent it. Compensating an
unselected child must instead fit the scene's stored TRS representation; preflight
the entire operation before publishing changes.

Bake Location and Bake Transforms reset the actual origin to parent-space zero.
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
