# Plan: Fast Dual Contouring Mesh Generation

## Executive Summary

Our current DC implementation is **single-threaded** with O(n^3) complexity for a resolution of n. The MC (Marching Cubes) implementation is already multithreaded. This plan outlines multiple optimization strategies ranging from quick wins to advanced GPU-based approaches.

**Current Performance Baseline:**
- DC at res=512: ~1538 vertices, 3080 triangles (single-threaded)
- MC at res=512: ~9108 vertices, 3036 triangles (multithreaded)

## Research Summary

### Sources Consulted

**Academic Papers:**
- [Occupancy-Based Dual Contouring (ODC)](https://occupancy-based-dual-contouring.github.io/) - SIGGRAPH Asia 2024
- [Neural Dual Contouring](https://arxiv.org/abs/2202.01999) - Learning-based approach
- [Massively Parallel Rendering](https://www.mattkeeter.com/research/mpr/) - GPU SDF rendering

**Open Source Implementations:**
- [libfive](https://github.com/libfive/libfive) - Manifold DC with worker pools (we have this as submodule!)
- [Twinklebear/vulkan-marching-cubes](https://github.com/Twinklebear/vulkan-marching-cubes) - GPU MC in Vulkan
- [KAIST ODC Implementation](https://github.com/KAIST-Visual-AI-Group/ODC) - PyTorch GPU-parallel DC
- [WebGPU Marching Cubes](https://www.willusher.io/graphics/2024/04/22/webgpu-marching-cubes/) - Near-native performance

**Key Techniques:**
- [fogleman/sdf](https://github.com/fogleman/sdf) - Batch processing, multithread workers
- [IsoMesh](https://github.com/EmmetOT/IsoMesh) - Unity GPU compute shader DC
- Interval arithmetic for empty cell skipping

---

## Optimization Strategies (Ordered by Implementation Effort)

### Strategy 1: CPU Multithreading (Quick Win)

**Effort: LOW | Impact: 4-16x speedup**

Our MC implementation already does this. Apply the same pattern to DC.

**Current DC Problem:**
```cpp
// Current: Single-threaded triple nested loop
for (int z = 0; z < res - 1; z++) {
    for (int y = 0; y < res - 1; y++) {
        for (int x = 0; x < res - 1; x++) {
            // Process cell...
        }
    }
}
```

**Solution: Z-slice parallelization**
```cpp
inline Mesh generateMeshDC_Parallel(
    const std::vector<float>& distances,
    int res, Vec3 bounds_min, Vec3 bounds_max, float isolevel = 0.0f
) {
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    numThreads = std::min(numThreads, (unsigned int)(res - 1));

    // Per-thread data structures
    struct ThreadData {
        std::vector<Vec3> cellVertices;
        std::vector<bool> cellHasVertex;
        std::vector<int> cellToVertex;
        std::vector<Vec3> vertices;
        std::vector<uint32_t> indices;
    };
    std::vector<ThreadData> threadData(numThreads);

    // Phase 1: Parallel cell vertex generation
    std::vector<std::thread> threads;
    int zPerThread = (res - 1 + numThreads - 1) / numThreads;

    for (unsigned int t = 0; t < numThreads; t++) {
        int zStart = t * zPerThread;
        int zEnd = std::min(zStart + zPerThread, res - 1);

        threads.emplace_back([&, t, zStart, zEnd]() {
            ThreadData& td = threadData[t];
            // Process cells in Z range [zStart, zEnd)
            // ... (cell vertex computation)
        });
    }

    for (auto& t : threads) t.join();

    // Phase 2: Merge and generate quads (needs careful handling)
    // Edge quads cross Z boundaries, so handle those specially
    // ...
}
```

**Key Changes:**
1. Split cell processing by Z-slices (same as MC)
2. Each thread builds local vertex list
3. Merge phase combines results
4. Edge processing needs care at Z-boundaries

**Files to Modify:**
- `vulkan/marching_cubes.hpp`: Add `generateMeshDC_Parallel()`

---

### Strategy 2: Early Cell Rejection (Quick Win)

**Effort: LOW | Impact: 2-10x speedup (depending on model)**

Skip cells that are clearly inside or outside the surface.

**Current Problem:**
We check all 12 edges for every cell, even when all corners have the same sign.

**Solution: Add early-out check**
```cpp
// Quick reject: if all corners same sign, skip
int insideCount = 0;
for (int i = 0; i < 8; i++) {
    if (v[i] < isolevel) insideCount++;
}
if (insideCount == 0 || insideCount == 8) continue;  // Early out

// Only then check edges...
```

**Additional Optimization - Distance Threshold:**
```cpp
// If minimum distance to surface is large, skip cell entirely
float minDist = std::abs(v[0]);
float maxDist = std::abs(v[0]);
for (int i = 1; i < 8; i++) {
    minDist = std::min(minDist, std::abs(v[i]));
    maxDist = std::max(maxDist, std::abs(v[i]));
}
// If cell is far from surface, skip
float cellDiagonal = std::sqrt(cell_size.x*cell_size.x +
                               cell_size.y*cell_size.y +
                               cell_size.z*cell_size.z);
if (minDist > cellDiagonal) continue;  // Cell entirely inside/outside
```

---

### Strategy 3: Use libfive's DC Implementation

**Effort: MEDIUM | Impact: Significant (battle-tested, optimized)**

We already have libfive as a submodule! It has:
- `WorkerPool` with lock-free task stack
- Octree-based adaptive subdivision
- Manifold Dual Contouring
- Per-thread evaluators

**libfive Architecture:**
```
vendor/libfive/libfive/src/render/brep/
├── dc/
│   ├── dc_worker_pool3.cpp   <- Parallel worker pool
│   ├── dc_mesher.cpp         <- Mesh generation
│   ├── dc_tree3.cpp          <- Octree structure
│   └── dc_contourer.cpp      <- Contouring logic
├── worker_pool.inl           <- Generic parallel framework
└── mesh.cpp                  <- Output mesh handling
```

**Integration Approach:**
```cpp
#include <libfive/libfive.h>
#include <libfive/render/brep/mesh.hpp>
#include <libfive/render/brep/settings.hpp>

// Option A: Use libfive's tree for SDF
libfive::Tree sdf = /* build from primitives */;
libfive::BRepSettings settings;
settings.workers = std::thread::hardware_concurrency();
settings.min_feature = cell_size;
auto mesh = libfive::Mesh::render(sdf, region, settings);

// Option B: Wrap our SDF evaluator as libfive Oracle
class CustomSDF : public libfive::Oracle {
    // Implement evaluate(), evalIntervals(), etc.
};
```

**Pros:**
- Already in our codebase
- Well-tested, handles edge cases
- Adaptive resolution (octree)
- Manifold output (watertight meshes)

**Cons:**
- Need to bridge our SDF evaluation to libfive's API
- May require building libfive library
- Different mesh format (need conversion)

**Files to Create/Modify:**
- `vulkan/libfive_bridge.hpp`: Bridge between our SDF and libfive
- `CMakeLists.txt`: Link against libfive

---

### Strategy 4: Vulkan Compute Shader DC

**Effort: HIGH | Impact: 10-100x speedup**

Move DC to GPU using Vulkan compute shaders.

**Architecture (based on WebGPU MC research):**
```
┌─────────────────────────────────────────────────────────────┐
│                    GPU DC Pipeline                           │
├─────────────────────────────────────────────────────────────┤
│  Pass 1: Mark Active Cells                                   │
│  - Input: SDF volume (3D texture or SSBO)                   │
│  - Output: Active cell mask buffer                           │
│  - Each thread: Check 8 corners, write 1 if crossing        │
├─────────────────────────────────────────────────────────────┤
│  Pass 2: Exclusive Scan (Stream Compaction)                 │
│  - Input: Active cell mask                                   │
│  - Output: Cell write offsets                                │
│  - Parallel prefix sum for output indices                   │
├─────────────────────────────────────────────────────────────┤
│  Pass 3: Compact Active Cells                                │
│  - Input: Mask + offsets                                     │
│  - Output: Compact list of active cell indices               │
├─────────────────────────────────────────────────────────────┤
│  Pass 4: Generate Cell Vertices (QEF Solve)                 │
│  - Input: Active cell list, SDF volume                      │
│  - Output: Vertex buffer (one per active cell)              │
│  - Each thread: Solve QEF for one cell                      │
├─────────────────────────────────────────────────────────────┤
│  Pass 5: Count Quads per Edge                               │
│  - Input: Active cells                                       │
│  - Output: Quad counts per edge direction (X/Y/Z)           │
├─────────────────────────────────────────────────────────────┤
│  Pass 6: Generate Quad Indices                              │
│  - Input: Active cells, vertex indices, edge quad counts    │
│  - Output: Index buffer                                      │
└─────────────────────────────────────────────────────────────┘
```

**Key Compute Shaders:**

**1. Mark Active Cells (`mark_active.comp`):**
```glsl
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(binding = 0) uniform sampler3D sdfVolume;
layout(binding = 1) buffer ActiveMask { uint activeMask[]; };

uniform int resolution;
uniform float isolevel;

void main() {
    ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (any(greaterThanEqual(cell, ivec3(resolution - 1)))) return;

    // Sample 8 corners
    float v[8];
    v[0] = texelFetch(sdfVolume, cell + ivec3(0,0,0), 0).r;
    v[1] = texelFetch(sdfVolume, cell + ivec3(1,0,0), 0).r;
    // ... (remaining corners)

    // Check for sign change
    int inside = 0;
    for (int i = 0; i < 8; i++) {
        if (v[i] < isolevel) inside++;
    }

    uint cellIdx = cell.x + cell.y * (resolution-1) +
                   cell.z * (resolution-1) * (resolution-1);
    activeMask[cellIdx] = (inside > 0 && inside < 8) ? 1 : 0;
}
```

**2. QEF Solve (`qef_solve.comp`):**
```glsl
#version 450

layout(local_size_x = 256) in;

layout(binding = 0) uniform sampler3D sdfVolume;
layout(binding = 1) buffer ActiveCells { uint activeCellIndices[]; };
layout(binding = 2) buffer Vertices { vec4 vertices[]; };

// QEF accumulation (symmetric 4x4 matrix stored as 10 floats)
struct QEF {
    float ata[6];  // Upper triangle of A^T * A
    vec3 atb;      // A^T * b
    vec3 massPoint;
    int numPoints;
};

void addToQEF(inout QEF q, vec3 pos, vec3 normal) {
    q.ata[0] += normal.x * normal.x;
    q.ata[1] += normal.x * normal.y;
    q.ata[2] += normal.x * normal.z;
    q.ata[3] += normal.y * normal.y;
    q.ata[4] += normal.y * normal.z;
    q.ata[5] += normal.z * normal.z;

    float d = dot(pos, normal);
    q.atb += normal * d;
    q.massPoint += pos;
    q.numPoints++;
}

vec3 solveQEF(QEF q, vec3 cellMin, vec3 cellMax) {
    // Regularized least squares solve
    // Add small value to diagonal for numerical stability
    // Clamp result to cell bounds
    // ...
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= numActiveCells) return;

    uint cellIdx = activeCellIndices[idx];
    // Convert back to 3D coordinates
    // Process edges, accumulate QEF
    // Solve and write vertex
    // ...
}
```

**Performance Reference (WebGPU MC on RTX 3080 at 256^3):**
- WebGPU: 31-37.5ms
- Vulkan: 27.45-33.43ms

**Files to Create:**
- `vulkan/shaders/dc_mark_active.comp`
- `vulkan/shaders/dc_scan.comp`
- `vulkan/shaders/dc_compact.comp`
- `vulkan/shaders/dc_qef_solve.comp`
- `vulkan/shaders/dc_generate_quads.comp`
- `vulkan/dc_compute.hpp`: Pipeline setup and dispatch

---

### Strategy 5: ODC-Style Binary Search (No Gradients)

**Effort: MEDIUM-HIGH | Impact: Better quality + GPU-friendly**

Based on [Occupancy-Based Dual Contouring (SIGGRAPH Asia 2024)](https://arxiv.org/abs/2409.13418).

**Key Innovation:** Replace gradient-based normal computation with 2D point search.

**Algorithm:**
1. **1D Point Search**: Binary search along edges to find surface crossings (15 iterations)
2. **2D Point Search**: Find auxiliary points on grid faces for normal computation
3. **3D Point Solve**: QEF without gradients using 1D + 2D points
4. **Polygonization**: Standard manifold DC quad generation

**Why This Matters:**
- No gradient computation needed (expensive for complex SDFs)
- Binary search is GPU-parallel
- Better sharp feature detection

**Implementation Sketch:**
```cpp
// 1D point search: binary search along edge
Vec3 findEdgeCrossing(Vec3 p0, Vec3 p1,
                      std::function<float(Vec3)> sdf,
                      int iterations = 15) {
    for (int i = 0; i < iterations; i++) {
        Vec3 mid = (p0 + p1) * 0.5f;
        float d = sdf(mid);
        if ((d < 0) == (sdf(p0) < 0)) {
            p0 = mid;
        } else {
            p1 = mid;
        }
    }
    return (p0 + p1) * 0.5f;
}

// 2D point search: find point on face for normal
Vec3 find2DPoint(Vec3 center, Vec3 axis1, Vec3 axis2,
                 std::function<float(Vec3)> sdf,
                 int iterations = 11) {
    // Line-binary search in 2D
    // Returns point where surface crosses face plane
}

// Compute normal from 3 points (1D + 2x 2D)
Vec3 computeNormalFrom3Points(Vec3 p1d, Vec3 p2d_a, Vec3 p2d_b) {
    Vec3 v1 = p2d_a - p1d;
    Vec3 v2 = p2d_b - p1d;
    return normalize(cross(v1, v2));
}
```

---

### Strategy 6: Adaptive Octree DC (libfive-style)

**Effort: HIGH | Impact: Massive for sparse models**

Instead of uniform grid, use adaptive octree subdivision.

**Benefits:**
- Skip large empty/full regions entirely
- Higher detail only where needed
- Memory efficient for sparse surfaces

**Implementation:**
```cpp
struct OctreeNode {
    Vec3 min, max;
    int level;
    std::array<std::unique_ptr<OctreeNode>, 8> children;
    bool isLeaf;
    bool hasVertex;
    Vec3 vertex;
};

void buildOctree(OctreeNode* node,
                 const std::function<float(Vec3)>& sdf,
                 int maxLevel) {
    // Sample corners
    float corners[8] = /* sample 8 corners */;

    // Check if homogeneous (all same sign)
    bool allInside = true, allOutside = true;
    for (int i = 0; i < 8; i++) {
        if (corners[i] < 0) allOutside = false;
        else allInside = false;
    }

    if (allInside || allOutside) {
        node->isLeaf = true;
        node->hasVertex = false;
        return;  // Skip this subtree!
    }

    if (node->level >= maxLevel) {
        node->isLeaf = true;
        node->hasVertex = true;
        node->vertex = computeDCVertex(node, sdf);
        return;
    }

    // Subdivide
    for (int i = 0; i < 8; i++) {
        node->children[i] = std::make_unique<OctreeNode>(/*...*/);
        buildOctree(node->children[i].get(), sdf, maxLevel);
    }
}
```

---

## Comparison Matrix

| Strategy | Effort | Speedup | Quality | Memory | GPU Required |
|----------|--------|---------|---------|--------|--------------|
| 1. CPU Multithreading | Low | 4-16x | Same | Same | No |
| 2. Early Cell Rejection | Low | 2-10x | Same | Same | No |
| 3. libfive Integration | Medium | 10-50x | Better | Lower | No |
| 4. Vulkan Compute | High | 50-100x | Same | Higher | Yes |
| 5. ODC Binary Search | Medium | 20-50x | Better | Same | Optional |
| 6. Adaptive Octree | High | 10-1000x* | Better | Much Lower | No |

*Depends heavily on model sparsity

---

## Recommended Implementation Order

### Phase 1: Quick Wins (1-2 days)
1. **Strategy 1**: Add multithreading to DC (copy pattern from MC)
2. **Strategy 2**: Add early cell rejection

Expected: **8-20x speedup** with minimal code changes

### Phase 2: Quality Improvements (3-5 days)
3. **Strategy 3**: Evaluate libfive integration
   - Build libfive library
   - Create bridge to our SDF evaluator
   - Compare output quality

### Phase 3: GPU Acceleration (1-2 weeks)
4. **Strategy 4**: Vulkan compute shader implementation
   - Start with mark_active + scan
   - Add QEF solve
   - Add quad generation

### Phase 4: Advanced (Future)
5. **Strategy 5**: ODC binary search (if gradient computation is bottleneck)
6. **Strategy 6**: Adaptive octree (if memory/sparse models are priority)

---

## Files to Create/Modify

| File | Changes |
|------|---------|
| `vulkan/marching_cubes.hpp` | Add `generateMeshDC_Parallel()`, early rejection |
| `vulkan/dc_compute.hpp` | NEW: Vulkan compute pipeline for DC |
| `vulkan/shaders/dc_*.comp` | NEW: Compute shaders (5-6 files) |
| `vulkan/libfive_bridge.hpp` | NEW: Bridge to libfive (if using Strategy 3) |
| `CMakeLists.txt` | libfive linkage (if using Strategy 3) |

---

## Testing Plan

1. **Correctness**: Compare output meshes at same resolution
2. **Performance**: Benchmark at res 64, 128, 256, 512
3. **Quality**: Visual comparison, vertex/triangle counts
4. **Memory**: Peak memory usage during generation

**Test Commands:**
```bash
# Start app
make sdf

# Generate at different resolutions
clj-nrepl-eval -p 5557 '(cpp/raw "sdfx::generate_mesh_preview(128);")'
clj-nrepl-eval -p 5557 '(cpp/raw "sdfx::generate_mesh_preview(256);")'
clj-nrepl-eval -p 5557 '(cpp/raw "sdfx::generate_mesh_preview(512);")'
```

---

## GPU Profiling for Vulkan

### Overview

GPU profiling is essential for optimizing compute shader performance. Vulkan provides built-in timestamp queries, and several external tools offer deeper analysis.

### Tool Comparison

| Tool | Vendor | Best For | Compute Support | Ease of Use |
|------|--------|----------|-----------------|-------------|
| **Vulkan Timestamp Queries** | Khronos | In-app profiling | Excellent | Medium |
| **Tracy Profiler** | Open Source | CPU/GPU correlation | Good | Easy |
| **RenderDoc** | Open Source | Frame debugging | Limited | Easy |
| **NSight Graphics** | NVIDIA | Deep shader analysis | Excellent | Medium |
| **Radeon GPU Profiler (RGP)** | AMD | AMD GPU optimization | Excellent | Medium |

---

### Method 1: Vulkan Timestamp Queries (Built-in)

**Best for:** In-app profiling, always-on performance monitoring

**Requirements:**
- Check `timestampPeriod > 0` in physical device limits
- Check `timestampComputeAndGraphics == VK_TRUE` for compute support
- Vulkan 1.2+ (or `VK_EXT_host_query_reset` for 1.0/1.1)

**Implementation:**

```cpp
// 1. Create query pool
VkQueryPoolCreateInfo queryPoolInfo{};
queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
queryPoolInfo.queryCount = 16;  // Number of timestamps needed

VkQueryPool queryPool;
vkCreateQueryPool(device, &queryPoolInfo, nullptr, &queryPool);

// 2. Reset queries before use (at start of command buffer)
vkCmdResetQueryPool(commandBuffer, queryPool, 0, 16);

// 3. Record timestamps around compute dispatch
vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    queryPool, 0);  // Start timestamp

vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);

vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    queryPool, 1);  // End timestamp

// 4. Read results after submission completes
uint64_t timestamps[2];
vkGetQueryPoolResults(device, queryPool, 0, 2,
                      sizeof(timestamps), timestamps, sizeof(uint64_t),
                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

// 5. Convert to milliseconds
VkPhysicalDeviceProperties props;
vkGetPhysicalDeviceProperties(physicalDevice, &props);
float timestampPeriod = props.limits.timestampPeriod;  // nanoseconds per tick

double elapsedMs = (timestamps[1] - timestamps[0]) * timestampPeriod / 1e6;
std::cout << "Compute dispatch: " << elapsedMs << " ms" << std::endl;
```

**Multi-pass Profiling Helper:**
```cpp
class VulkanGPUProfiler {
public:
    struct TimingResult {
        const char* name;
        double milliseconds;
    };

    void beginFrame(VkCommandBuffer cmd) {
        vkCmdResetQueryPool(cmd, queryPool, 0, maxQueries);
        currentQuery = 0;
    }

    void beginZone(VkCommandBuffer cmd, const char* name) {
        if (currentQuery >= maxQueries - 1) return;
        zoneNames[currentQuery / 2] = name;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            queryPool, currentQuery++);
    }

    void endZone(VkCommandBuffer cmd) {
        if (currentQuery >= maxQueries) return;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            queryPool, currentQuery++);
    }

    std::vector<TimingResult> getResults() {
        std::vector<uint64_t> timestamps(currentQuery);
        vkGetQueryPoolResults(device, queryPool, 0, currentQuery,
                              timestamps.size() * sizeof(uint64_t),
                              timestamps.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

        std::vector<TimingResult> results;
        for (size_t i = 0; i < currentQuery; i += 2) {
            double ms = (timestamps[i+1] - timestamps[i]) * timestampPeriod / 1e6;
            results.push_back({zoneNames[i/2], ms});
        }
        return results;
    }

private:
    VkDevice device;
    VkQueryPool queryPool;
    float timestampPeriod;
    uint32_t currentQuery = 0;
    static constexpr uint32_t maxQueries = 64;
    const char* zoneNames[32];
};
```

**Usage in DC Pipeline:**
```cpp
void generateMeshDC_GPU(VulkanGPUProfiler& profiler, VkCommandBuffer cmd) {
    profiler.beginFrame(cmd);

    profiler.beginZone(cmd, "Mark Active Cells");
    vkCmdDispatch(cmd, ...);  // mark_active.comp
    profiler.endZone(cmd);

    profiler.beginZone(cmd, "Exclusive Scan");
    vkCmdDispatch(cmd, ...);  // scan.comp
    profiler.endZone(cmd);

    profiler.beginZone(cmd, "Compact Cells");
    vkCmdDispatch(cmd, ...);  // compact.comp
    profiler.endZone(cmd);

    profiler.beginZone(cmd, "QEF Solve");
    vkCmdDispatch(cmd, ...);  // qef_solve.comp
    profiler.endZone(cmd);

    profiler.beginZone(cmd, "Generate Quads");
    vkCmdDispatch(cmd, ...);  // generate_quads.comp
    profiler.endZone(cmd);
}

// After frame completion:
for (auto& result : profiler.getResults()) {
    std::cout << result.name << ": " << result.milliseconds << " ms\n";
}
```

---

### Method 2: Tracy Profiler Integration

**Best for:** CPU/GPU correlation, real-time monitoring, production profiling

**Website:** https://github.com/wolfpld/tracy

**Setup:**
1. Add Tracy to project (header-only or library)
2. Define `TRACY_ENABLE` for profiling builds
3. Initialize Vulkan context for GPU zones

**Integration:**
```cpp
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

// Create Tracy Vulkan context (once at init)
TracyVkCtx tracyCtx = TracyVkContext(physicalDevice, device, queue, commandBuffer);

// In command buffer recording:
void recordDCCompute(VkCommandBuffer cmd) {
    {
        TracyVkZone(tracyCtx, cmd, "DC Mark Active");
        vkCmdDispatch(cmd, ...);
    }
    {
        TracyVkZone(tracyCtx, cmd, "DC Scan");
        vkCmdDispatch(cmd, ...);
    }
    {
        TracyVkZone(tracyCtx, cmd, "DC QEF Solve");
        vkCmdDispatch(cmd, ...);
    }

    // Collect GPU data (call once per frame, outside render pass)
    TracyVkCollect(tracyCtx, cmd);
}

// CPU zones for comparison
void generateMesh() {
    ZoneScoped;  // CPU zone

    {
        ZoneScopedN("Upload SDF Data");
        // ... upload code
    }

    recordDCCompute(cmd);

    {
        ZoneScopedN("Download Mesh");
        // ... download code
    }
}
```

**Tracy Features:**
- Real-time visualization in Tracy Profiler GUI
- CPU/GPU timeline correlation
- Frame time graphs
- Memory allocation tracking
- Lock contention analysis

---

### Method 3: NVIDIA NSight Graphics

**Best for:** Deep shader analysis on NVIDIA GPUs

**Download:** https://developer.nvidia.com/nsight-graphics

**Key Features:**
- **GPU Trace Profiler**: Throughput and utilization analysis
- **Shader Profiler**: Identify shader stalls and bottlenecks
- **Range Profiler**: Profile specific frame sections
- Cache hit/miss analysis
- Warp occupancy visualization

**Usage:**
1. Launch application through NSight Graphics
2. Capture frame with F11 (or configured hotkey)
3. Analyze in "GPU Trace" view for compute workloads

**Compute Shader Metrics:**
- SM Occupancy (target: >50%)
- Memory throughput (L1/L2 cache hit rates)
- Warp stall reasons (memory dependency, execution dependency)
- Register usage per thread

---

### Method 4: AMD Radeon GPU Profiler (RGP)

**Best for:** Deep analysis on AMD GPUs

**Download:** https://gpuopen.com/rgp/

**Key Features:**
- **Wavefront Occupancy**: Visualize GPU utilization over time
- **Event Timing**: Per-dispatch timing breakdown
- **Pipeline State**: Analyze shader compilation
- **Cache Counters**: L0/L1/L2 cache hit rates (RDNA2+)
- **Barrier Analysis**: Identify synchronization bottlenecks

**Setup:**
1. Enable RGP in AMD driver settings
2. Press Ctrl+Shift+C to capture profile
3. Open `.rgp` file in Radeon GPU Profiler

**Compute-Specific Analysis:**
- Look for "CS" (Compute Shader) events in wavefront view
- Check "Thread Group" dimensions vs occupancy
- Analyze LDS (Local Data Share) usage
- Review barrier wait times between dispatches

**Command Line Capture (Linux):**
```bash
# Set environment variable for Vulkan layer
export VK_INSTANCE_LAYERS=VK_LAYER_AMD_switchable_graphics

# Or use radeon_gpu_profiler CLI
rgp --capture-vulkan ./your_app
```

---

### Method 5: RenderDoc (Limited Compute Support)

**Best for:** Debugging, resource inspection, basic timing

**Download:** https://renderdoc.org/

**Limitations:** RenderDoc is primarily a graphics debugger. Compute shader support exists but is less comprehensive than dedicated profilers.

**Useful For:**
- Buffer/texture content inspection
- Shader source viewing
- Basic event timing
- API call validation

---

### Profiling Best Practices

1. **Use Multiple Pools**: Avoid GPU stalls by using N query pools (one per frame-in-flight)
   ```cpp
   VkQueryPool queryPools[MAX_FRAMES_IN_FLIGHT];
   // Use queryPools[currentFrame % MAX_FRAMES_IN_FLIGHT]
   ```

2. **Profile Release Builds**: Debug builds have significant overhead

3. **Warm Up**: Run several iterations before measuring to fill caches

4. **Measure Variance**: Single measurements can be noisy
   ```cpp
   std::vector<double> timings;
   for (int i = 0; i < 100; i++) {
       timings.push_back(measureDispatch());
   }
   double mean = std::accumulate(timings.begin(), timings.end(), 0.0) / timings.size();
   double variance = /* compute */;
   ```

5. **Memory Barriers**: Ensure barriers don't dominate timing
   ```cpp
   // Profile barrier cost separately
   profiler.beginZone(cmd, "Memory Barrier");
   vkCmdPipelineBarrier(cmd, ...);
   profiler.endZone(cmd);
   ```

6. **Workgroup Size Tuning**: Profile different local_size configurations
   ```cpp
   // Test: 64, 128, 256, 512 threads per workgroup
   for (int size : {64, 128, 256, 512}) {
       // Recompile shader or use specialization constants
       double time = measureWithWorkgroupSize(size);
       std::cout << "Workgroup " << size << ": " << time << " ms\n";
   }
   ```

---

### Profiling Checklist for DC GPU Implementation

- [ ] Add timestamp queries around each compute pass
- [ ] Integrate Tracy for development builds
- [ ] Profile at multiple resolutions (128, 256, 512)
- [ ] Measure memory transfer overhead (upload SDF, download mesh)
- [ ] Compare CPU vs GPU total time
- [ ] Identify bottleneck pass (mark? scan? QEF? quads?)
- [ ] Test workgroup size variations
- [ ] Check GPU occupancy with vendor tool

---

### Expected Timing Breakdown (Target)

For res=256 on mid-range GPU:

| Pass | Expected Time | Notes |
|------|--------------|-------|
| Upload SDF | 1-5 ms | Depends on transfer method |
| Mark Active | <1 ms | Simple per-cell check |
| Exclusive Scan | 1-2 ms | O(n) parallel scan |
| Compact | <1 ms | Scatter to compact buffer |
| QEF Solve | 2-5 ms | Most compute-intensive |
| Generate Quads | 1-2 ms | Index buffer generation |
| Download Mesh | 1-5 ms | Variable size output |
| **Total** | **7-20 ms** | Target: <16ms for 60fps |

---

## References

### Mesh Generation
- [Occupancy-Based Dual Contouring](https://occupancy-based-dual-contouring.github.io/) - SIGGRAPH Asia 2024
- [libfive GitHub](https://github.com/libfive/libfive) - Manifold DC implementation
- [Vulkan Marching Cubes](https://github.com/Twinklebear/vulkan-marching-cubes) - GPU MC reference
- [WebGPU Marching Cubes](https://www.willusher.io/graphics/2024/04/22/webgpu-marching-cubes/) - Performance analysis
- [MPR GPU Rendering](https://github.com/mkeeter/mpr/) - GPU SDF evaluation
- [fogleman/sdf](https://github.com/fogleman/sdf) - Batch processing reference
- [IsoMesh](https://github.com/EmmetOT/IsoMesh) - Unity GPU DC

### GPU Profiling
- [Vulkan Timestamp Queries](https://docs.vulkan.org/samples/latest/samples/api/timestamp_queries/README.html) - Official Khronos documentation
- [How to use Vulkan Timestamp Queries](https://nikitablack.github.io/post/how_to_use_vulkan_timestamp_queries/) - Detailed tutorial
- [Tracy Profiler](https://github.com/wolfpld/tracy) - Real-time CPU/GPU profiler
- [NVIDIA NSight Graphics](https://developer.nvidia.com/nsight-graphics) - NVIDIA GPU profiler
- [AMD Radeon GPU Profiler](https://gpuopen.com/rgp/) - AMD GPU profiler
- [GPU Execution Timing Basics](https://pavelsmejkal.net/Posts/GPUTimingBasics) - Vk/DX12 timing guide
- [VulkanProfiler](https://github.com/lstalmir/VulkanProfiler) - Real-time GPU profiling layer

---

## Status
- [x] Research completed
- [x] Phase 1: Quick wins (CPU multithreading + early rejection) - **IMPLEMENTED 2024-12-15**
- [ ] Phase 2: libfive evaluation
- [ ] Phase 3: Vulkan compute implementation
- [ ] Phase 4: Advanced optimizations

### Implementation Notes (Phase 1)

**Completed:** CPU Multithreading + Early Cell Rejection

Changes to `vulkan/marching_cubes.hpp`:
- `generateMeshDC()` now uses `std::thread::hardware_concurrency()` threads
- **Phase 1 (Cell vertices)**: Parallelized by Z-slices, each thread writes to disjoint regions
- **Phase 2 (Vertex mapping)**: Sequential (fast O(n) scan, maintains contiguous vertex IDs)
- **Phase 3 (Quad generation)**: Parallelized with thread-local index buffers, merged at end
- **Early rejection**: Added corner sign check before edge crossing check
- Changed `std::vector<bool>` to `std::vector<uint8_t>` for thread safety

Output now shows thread count: `DC mesh: 346 vertices, 752 triangles (threads: 16)`
