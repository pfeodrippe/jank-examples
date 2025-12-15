# GPU DC Cubes + Chunked Processing - 2024-12-15

## Features Implemented

### 1. GPU Fill With Cubes Mode
- Created `dc_cubes.comp` shader for voxel cube generation
- Generates 8 vertices + 36 indices per active cell on GPU
- Supports voxelSize parameter for scaling

### 2. Chunked GPU Processing
- Enables GPU DC at any resolution by processing in 512³ chunks
- Automatically chunks large grids, merges results on CPU
- Falls back gracefully to CPU if GPU fails

## Performance Results

| Resolution | Chunks | GPU Chunked | CPU (16 threads) |
|------------|--------|-------------|------------------|
| 256 | 1 | ~200 ms | ~1000 ms |
| 512 | 1 | ~150 ms | ~2400 ms |
| 1024 | 27 (3x3x3) | ~10000 ms | ~10000 ms |

**Key Finding**: GPU excels at single-chunk processing (16x speedup at 512). At 1024+, chunking overhead reduces advantage.

## Files Modified

| File | Changes |
|------|---------|
| `vulkan_kim/dc_cubes.comp` | NEW - GPU cubes shader |
| `vulkan_kim/dc_cubes.spv` | NEW - Compiled shader |
| `vulkan/sdf_engine.hpp` | Added cubes pipeline, chunked processing |
| `src/vybe/sdf/ui.jank` | GPU DC checkbox always visible |

## Technical Details

### Cubes Shader (`dc_cubes.comp`)
- Uses `dc_mark_active` to identify surface cells
- Each active cell generates a cube with atomic vertex/index allocation
- Bindings: distances, activeMask, vertices, indices, vertexCount, indexCount, params

### Chunked Processing
- `process_dc_chunk()` - processes a single chunk with GPU
- `generate_mesh_dc_gpu_chunked()` - orchestrates chunk processing
- Chunk overlap of 1 cell ensures seamless boundaries
- Results merged by offsetting indices

### Buffer Sizes (512³ chunk)
- ~134M cells max
- Vertex buffer: ~2GB
- Index buffer: ~6GB

## Usage

1. Enable "Dual Contouring" checkbox
2. Enable "GPU DC (experimental)" checkbox
3. Works with or without "Fill With Cubes"

## Future Optimizations

1. **Parallel chunk processing** - Use multiple command buffers
2. **Dynamic chunk sizing** - Based on available VRAM
3. **Sparse representation** - Only allocate for active cells

## Commands

```bash
# Compile cubes shader
cd vulkan_kim
glslangValidator -V dc_cubes.comp -o dc_cubes.spv

# Build
make sdf
```
