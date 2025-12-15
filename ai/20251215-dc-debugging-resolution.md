# DC Mesh Debugging - Resolution Discovery

## Summary
Investigated the "sliced" appearance of Dual Contouring (DC) meshes. The issue was not a bug in the DC algorithm but rather insufficient resolution for the model complexity.

## Key Findings

### Resolution Comparison at res=128
- **DC**: 82 vertices, 156 triangles (78 quads)
- **MC**: More triangles for same surface

### Resolution Comparison at res=512
- **DC**: 1538 vertices, 3080 triangles
- **MC**: 9108 vertices, 3036 triangles

DC at higher resolution produces comparable (actually slightly more) triangles than MC, with much better vertex efficiency (fewer vertices due to quad sharing).

## Root Cause
DC generates quads at cell boundaries, not at edge intersections within cells like MC. At low resolutions:
- Cells are larger
- Fewer crossing edges detected
- Sparse surface coverage causes visible gaps

## Technical Details

### DC Algorithm Verification
- Vertex sharing works correctly (all vertices used multiple times)
- Quad winding is correct for backface culling
- No connectivity bugs found

### Files Modified During Investigation
- `vulkan/marching_cubes.hpp` - Added/removed debug output
- `vulkan/sdf_engine.hpp` - Added/removed debug output, temporarily disabled culling

### Debug Output Cleanup
Simplified DC output to just show: `DC mesh: X vertices, Y triangles`

## Recommendations
1. For detailed models, use higher resolution (256+) with DC
2. DC works best for models with sharp features (its intended use case)
3. Consider adding UI warning if resolution is low with DC enabled

## Commands Used
```bash
# Start app
make sdf

# nREPL evaluation
clj-nrepl-eval -p 5557 '(cpp/raw "sdfx::generate_mesh_preview(512);")'
clj-nrepl-eval -p 5557 '(cpp/raw "sdfx::save_viewport_screenshot(\"/path/to/file.png\");")'
```

## Next Steps
- Consider implementing adaptive resolution for DC
- The DC algorithm is working correctly and can be used for production
