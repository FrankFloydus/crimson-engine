<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Quader Feature Ideas

My take: **Quader’s strongest direction is fast modeling of textured environments and props**, with geometry, materials, and modular assembly working closely together.

I checked the existing toolset, so these are missing workflows or substantial extensions. **★ marks ideas I’d emphasize to distinguish Quader.** The order is thematic, not implementation priority.

## Modeling

1. **Editable operation stack**  
   Revisit bevel width, thickness, primitive dimensions, or array count after committing. Start with a small, dependable set of operations. Inspiration: [Blender’s modifiers](https://docs.blender.org/manual/en/latest/modeling/modifiers/introduction.html).

2. **Editable Boolean cutters**  
   Subtract, combine, and intersect meshes while keeping the cutting objects adjustable. Move a doorway cutter later and the opening updates.

3. **Smart extrusion**  
   Extrude through a wall to create an opening, or into another surface to join it, rebuilding the affected geometry automatically. Inspiration: [3ds Max Smart Extrude](https://help.autodesk.com/cloudhelp/2023/ENU/3DSMax-Modeling/files/GUID-BC21F430-1E97-4237-A228-93C8744227C2.htm).

4. **Live symmetry**  
   Extend the existing Mirror tool into ongoing symmetrical editing, with centerline welding, clipping, and an adjustable symmetry plane.

5. **Editable curves and sweeps**  
   Draw a path and give it a cross-section to create pipes, cables, rails, and architectural trim. Editing the path updates the mesh. Inspiration: [Houdini Sweep](https://www.sidefx.com/docs/houdini/nodes/sop/sweep.html).

6. **Persistent dimensions and constraints**  
   Keep a doorway exactly 90 cm wide, two faces parallel, or several gaps equal while surrounding geometry changes. Adapt the precision of [CAD constraints](https://www.shapr3d.com/product/cad-constraints) to polygon modeling.

7. **Surface retopology tools**  
   Draw clean polygons over an imported reference mesh, with surface snapping and relaxation. Useful for simplifying scans and rebuilding awkward assets. Inspiration: [Maya Quad Draw](https://help.autodesk.com/cloudhelp/2025/ENU/Maya-Modeling/files/GUID-20DEA0B6-C090-49EA-98AE-172F1C382A05.htm).

## Materials and UVs

8. **A complete UV workspace**  
   Build on UV Peel with synchronized 2D/3D selection, seam editing, island stitching, straightening, and packing.

9. **Texel-density controls**  
   Set a project target such as 512 pixels per meter, visualize inconsistent surfaces, and normalize selected objects. Inspiration: [RizomUV’s density and packing controls](https://www.rizomuv.com/feature/rizomuv-2022-release-features/).

10. **★ Trim-sheet workflow**  
    Mark named strips on a texture, then assign them to selected faces or edge bands with controlled repetition and endpoint alignment. Inspiration: [Houdini’s trim tools](https://www.sidefx.com/docs/houdini/nodes/sop/labs--trim_texture.html).

11. **★ Texture continuation across many faces**  
    Extend our Alt-click transfer into a brush stroke or connected-face operation. Continue a pattern around a structure and preview where conflicting alignment requires a seam.

12. **★ Texture anchors that survive resizing**  
    Pin a brick course to the floor or a decorative stripe to an edge. Geometry edits preserve the chosen alignment and texture scale, with warnings when constraints conflict.

13. **Decal placement tools**  
    Drag signs, labels, cracks, and stains directly onto surfaces. Provide corner snapping, layering, projection depth, and an option to bake the result for export.

14. **Layered materials with surface masks**  
    Blend materials using height, slope, painted masks, or baked curvature—for example, dirt near the ground and wear along edges. Inspiration: [Substance Painter’s generators](https://experienceleague.adobe.com/en/docs/substance-3d-painter/using/effects/generators/generator).

15. **Integrated texture baking**  
    Bake normals, ambient occlusion, curvature, and material IDs, with cage previews and batch processing. This would turn Quader’s existing baking scaffolding into an artist-facing workflow.

## Modular assets and environment assembly

16. **Reusable kit and prefab library**  
    Save a selection with its geometry, materials, pivots, and thumbnail. Drag it into other projects and explicitly choose whether to accept later asset updates.

17. **★ Connector-based assembly**  
    Give modules named attachment points such as “wall end” or “door opening.” Compatible parts snap into position and orientation automatically. Build on the attachment concept of [Unreal’s mesh sockets](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-sockets-with-static-meshes-in-unreal-engine).

18. **★ Modules that resize intelligently**  
    Make a window wider without thickening its frame, or extend a fence by adding segments. Preserve trim dimensions and texture density. Inspiration: [SketchUp’s dynamic components](https://help.sketchup.com/en/sketchup/creating-common-types-dynamic-components).

19. **Parametric architectural assets**  
    Stairs, arches, doors, railings, and wall panels with a few exposed controls and viewport handles. Inspiration: [Houdini digital assets](https://www.sidefx.com/docs/houdini/assets/intro.html), presented through focused tool panels.

20. **Distribution along curves and surfaces**  
    Extend existing arrays with path placement and surface scattering. Add spacing, orientation, exclusion zones, and repeatable random seeds for fences, bolts, rubble, or vegetation.

21. **★ Replace blockouts with finished modules**  
    Tag simple geometry as a wall, doorway, or pillar, then replace it with a compatible detailed asset while retaining dimensions and connections. Preview mismatches before applying.

## Everyday workflow

22. **Saved selections and selection rules**  
    Save “all upward faces above this height” or “all exterior edges” as reusable selections that can be reevaluated after edits.

23. **A modeling recipe recorder**  
    Record a sequence such as inset → extrude → bevel, expose a few parameters, and save it as a reusable tool. This would make repetitive workflows accessible without programming.

24. **Searchable command launcher**  
    Search actions by name or intent, see their shortcuts, and learn why an action is unavailable. Contextual previews would make unfamiliar tools easier to discover.

25. **Visual checkpoints and design alternatives**  
    Add autosave, named checkpoints, and thumbnail comparisons. Let users restore one object or material from an earlier checkpoint while keeping the rest of the scene.

## Getting work into production

26. **Complete scene import and export**  
    Start with glTF/GLB support that carries hierarchy, transforms, PBR materials, and textures. The current selected-mesh OBJ export is a significant limitation for exchanging finished work.

27. **Live connection to a game engine**  
    Send only changed objects and preserve explicitly supported engine-side overrides. Start with one engine and clear conflict handling. Inspiration: [Datasmith Direct Link](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-datasmith-direct-link-in-unreal-engine).

28. **Collision and LOD workbench**  
    Generate, edit, preview, and export actual collision meshes and levels of detail. Show silhouette changes and triangle counts before accepting simplification.

29. **Production-readiness inspector**  
    List missing textures, invalid geometry, problematic UVs, excessive material splits, and export incompatibilities. Clicking an issue selects it; fixes have previews. Inspiration: [TrenchBroom’s issue browser](https://trenchbroom.github.io/manual/latest/index.html).

30. **Walkthrough mode with clearance checks**  
    Explore a level using a configurable player capsule, eye height, and step height. Highlight narrow passages, low ceilings, and inaccessible stairs—extending the existing fly camera into a practical scale-checking tool.

## What I would prioritize

**First:** complete scene interchange **#26**, texel density **#9**, trim sheets **#10**, multi-face continuation **#11**, and the reusable asset library **#16**.

**The strongest longer-term combination:** **#12 + #17 + #18 + #21**. Imagine stretching a corridor: brick scale stays correct, trim extends properly, doors keep their proportions, and detailed modules still fit. **That coherent workflow could become Quader’s recognizable advantage.**
