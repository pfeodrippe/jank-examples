# Mesh Preview Export Resolution Fix - Dec 14, 2025

## Summary
Fixed the mesh export to use the user-set resolution instead of hardcoded values.

## What Was Done

### Problem
The export buttons were using hardcoded resolution values (64 and 128) instead of the current resolution set by the user in the UI.

### Fix
Changed `src/vybe/sdf/ui.jank` export section to:
1. Use single "Export OBJ" button instead of two hardcoded buttons
2. Get current resolution from `sdfx/get_mesh_preview_resolution`
3. Pass that resolution to `sdfx/export_scene_mesh_gpu`

### Code Change
```clojure
;; Before: Two hardcoded buttons
(when (imgui/Button "Export OBJ (64)")
  (sdfx/export_scene_mesh_gpu "exported_scene.obj" (cpp/int. 64)))
(when (imgui/Button "Export OBJ (128)")
  (sdfx/export_scene_mesh_gpu "exported_scene.obj" (cpp/int. 128)))

;; After: Single button using current resolution
(let [res (sdfx/get_mesh_preview_resolution)]
  (when (imgui/Button "Export OBJ")
    (let [result (sdfx/export_scene_mesh_gpu "exported_scene.obj" (cpp/int. res))]
      (if (cpp/.-success result)
        (println "Exported at" res "res:" (cpp/.-vertices result) "verts,"
                 (cpp/.-triangles result) "tris")
        (println "Export failed:" (cpp/.-message result))))))
```

## Testing
Verified with `make sdf`:
- Export at 256 resolution: 2040 vertices, 680 triangles
- Export at 512 resolution: 9144 vertices, 3048 triangles

The export now correctly uses whatever resolution is set in the UI.

## Files Modified
- `src/vybe/sdf/ui.jank` - Export section now uses dynamic resolution

## Current State
- Mesh preview working with wireframe overlay
- Resolution controls (+ / - buttons) with adaptive step sizes
- Export uses current resolution setting
- `make sdf-standalone` produces working app bundle
