# .vybed File Format - Vybe Drawing Save Format

## Overview

Custom file format for saving Vybe drawing animations. Designed to be:
- **Compact**: Binary stroke data with optional compression
- **Debuggable**: JSON metadata header for human inspection
- **Future-proof**: Version field for format evolution
- **Gallery-friendly**: Embedded thumbnail for preview

## Research Sources

- [Sketch File Format](https://developer.sketch.com/file-format/) - Hybrid JSON + binary in ZIP
- [iOS Storage Best Practices](https://developer.apple.com/videos/play/tech-talks/204/) - Documents directory for user files
- [Custom Binary Formats](https://code.tutsplus.com/create-custom-binary-file-formats-for-your-games-data--gamedev-206t) - Version headers, magic numbers
- [iCloud Documents Guide](https://fatbobman.com/en/posts/in-depth-guide-to-icloud-documents/) - Cloud sync considerations

## File Structure

```
┌─────────────────────────────────────────┐
│ MAGIC NUMBER (8 bytes)                  │  "VYBED001"
├─────────────────────────────────────────┤
│ HEADER SIZE (4 bytes, uint32 LE)        │  Size of JSON header
├─────────────────────────────────────────┤
│ JSON HEADER (variable)                  │  Metadata, settings, structure
├─────────────────────────────────────────┤
│ THUMBNAIL SIZE (4 bytes, uint32 LE)     │  Size of PNG thumbnail
├─────────────────────────────────────────┤
│ THUMBNAIL DATA (variable)               │  PNG image for gallery preview
├─────────────────────────────────────────┤
│ STROKE DATA (variable)                  │  Binary stroke/point data
└─────────────────────────────────────────┘
```

## Version History

| Version | Magic      | Description |
|---------|------------|-------------|
| 1       | VYBED001   | Initial format |

## JSON Header Schema

```json
{
  "version": 1,
  "name": "My Animation",
  "created": "2026-01-11T10:30:00Z",
  "modified": "2026-01-11T12:45:00Z",
  "app_version": "1.0.0",

  "canvas": {
    "width": 1024,
    "height": 768,
    "background": [0.1, 0.1, 0.12, 1.0]
  },

  "playback": {
    "global_speed": 1.0,
    "is_playing": false
  },

  "onion_skin": {
    "mode": "both",
    "before": 2,
    "after": 1,
    "opacity": 0.3
  },

  "threads": [
    {
      "id": 0,
      "name": "Thread 1",
      "color": [1.0, 0.3, 0.3, 1.0],
      "line_width": 3.0,
      "opacity": 1.0,
      "fps": 8.0,
      "play_mode": "forward",
      "visible": true,
      "locked": false,
      "frame_count": 24,
      "stroke_data_offset": 0,
      "stroke_data_size": 12456
    },
    {
      "id": 1,
      "name": "Thread 2",
      "color": [0.3, 0.7, 1.0, 1.0],
      "line_width": 2.0,
      "opacity": 0.8,
      "fps": 12.0,
      "play_mode": "pingpong",
      "visible": true,
      "locked": false,
      "frame_count": 12,
      "stroke_data_offset": 12456,
      "stroke_data_size": 8234
    }
  ]
}
```

## Binary Stroke Data Format

Each thread's stroke data is stored as a contiguous binary block:

```
THREAD STROKE DATA:
┌─────────────────────────────────────────┐
│ For each frame:                         │
│   STROKE_COUNT (uint16)                 │
│   For each stroke:                      │
│     POINT_COUNT (uint16)                │
│     COLOR (4 x float32 = 16 bytes)      │  RGBA
│     WIDTH (float32 = 4 bytes)           │
│     FILL_MODE (uint8)                   │  0=line, 1=fill
│     BRUSH_TYPE (uint8)                  │  Reserved for future
│     For each point:                     │
│       X (float32)                       │
│       Y (float32)                       │
│       PRESSURE (float32)                │
│       TIMESTAMP (float32)               │  16 bytes per point
└─────────────────────────────────────────┘
```

### Size Estimation

For a typical animation:
- 5 threads × 24 frames × 10 strokes × 100 points = 120,000 points
- 120,000 × 16 bytes = 1.92 MB raw
- With compression (~3x): ~640 KB

## iOS Storage Strategy

### Primary: Documents Directory
```cpp
// Get Documents directory
NSArray *paths = NSSearchPathForDirectoriesInDomains(
    NSDocumentDirectory, NSUserDomainMask, YES);
NSString *documentsPath = [paths firstObject];
NSString *vybedPath = [documentsPath stringByAppendingPathComponent:@"Drawings"];
```

**Why Documents:**
- User-visible in Files app
- Backed up to iCloud automatically
- Persists across app updates
- Can enable "Open in Place" for file sharing

### Future: iCloud Container
```cpp
// Get iCloud container URL (async)
NSURL *containerURL = [[NSFileManager defaultManager]
    URLForUbiquityContainerIdentifier:nil];
NSURL *documentsURL = [containerURL URLByAppendingPathComponent:@"Documents"];
```

## C++ Implementation

### File: `src/vybe/app/drawing/native/vybed_format.hpp`

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace vybe::drawing {

constexpr char VYBED_MAGIC[8] = {'V','Y','B','E','D','0','0','1'};
constexpr uint32_t VYBED_VERSION = 1;

// Save weave to .vybed file
bool save_vybed(const std::string& path, const Weave& weave,
                const uint8_t* thumbnail_png, size_t thumbnail_size);

// Load weave from .vybed file
bool load_vybed(const std::string& path, Weave& weave,
                std::vector<uint8_t>& thumbnail_out);

// Get thumbnail only (for gallery preview)
bool load_vybed_thumbnail(const std::string& path,
                          std::vector<uint8_t>& thumbnail_out);

// List all .vybed files in directory
std::vector<std::string> list_vybed_files(const std::string& directory);

} // namespace vybe::drawing
```

### Serialization Helpers

```cpp
// Write little-endian uint32
inline void write_u32(std::ostream& os, uint32_t value) {
    os.write(reinterpret_cast<const char*>(&value), 4);
}

// Write float32
inline void write_f32(std::ostream& os, float value) {
    os.write(reinterpret_cast<const char*>(&value), 4);
}

// Read little-endian uint32
inline uint32_t read_u32(std::istream& is) {
    uint32_t value;
    is.read(reinterpret_cast<char*>(&value), 4);
    return value;
}
```

## Gallery Screen Design

```
┌─────────────────────────────────────────────────────────┐
│  VYBE DRAWINGS                              [+ New]     │
├─────────────────────────────────────────────────────────┤
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │
│  │ thumb1  │  │ thumb2  │  │ thumb3  │  │ thumb4  │    │
│  │         │  │         │  │         │  │         │    │
│  └─────────┘  └─────────┘  └─────────┘  └─────────┘    │
│  "Walk Cycle" "Bouncing"   "Fire FX"   "Character"     │
│  Jan 11      Jan 10        Jan 8       Jan 5           │
│                                                         │
│  ┌─────────┐  ┌─────────┐                              │
│  │ thumb5  │  │ thumb6  │                              │
│  │         │  │         │                              │
│  └─────────┘  └─────────┘                              │
│  "Test"      "Untitled"                                │
│  Jan 3       Jan 1                                     │
└─────────────────────────────────────────────────────────┘
```

## Implementation Steps

### Phase 1: Core Serialization
1. Create `vybed_format.hpp` and `vybed_format.cpp`
2. Implement `save_vybed()` - JSON header + binary strokes
3. Implement `load_vybed()` - parse and reconstruct Weave
4. Add thumbnail generation from current frame

### Phase 2: iOS Integration
1. Create `VybedFileManager` Objective-C++ class
2. Implement save to Documents directory
3. Implement file listing for gallery
4. Add "Save" button to top bar

### Phase 3: Gallery Screen
1. Create gallery UI with imgui
2. Load thumbnails on demand
3. Implement tap to open
4. Add delete/rename options

### Phase 4: Polish
1. Auto-save on app background
2. iCloud sync (optional)
3. Export to GIF/video

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `src/vybe/app/drawing/native/vybed_format.hpp` | CREATE | Format definitions and API |
| `src/vybe/app/drawing/native/vybed_format.cpp` | CREATE | Serialization implementation |
| `src/vybe/app/drawing/native/file_manager_ios.h` | CREATE | iOS file operations |
| `src/vybe/app/drawing/native/file_manager_ios.mm` | CREATE | Objective-C++ implementation |
| `src/vybe/app/drawing/native/gallery_screen.hpp` | CREATE | Gallery UI component |
| `DrawingMobile/drawing_mobile_ios.mm` | MODIFY | Add save/load buttons |
| `Makefile` | MODIFY | Add new source files |

## Testing Plan

1. Create simple drawing with 2 threads, 3 frames each
2. Save to .vybed
3. Hex dump file to verify structure
4. Load into fresh app instance
5. Verify all strokes, colors, settings preserved
6. Test gallery thumbnail loading
7. Test with large file (1000+ strokes)

## Risk Assessment

**Low Risk:**
- Binary format is straightforward
- iOS file APIs are well-documented

**Medium Risk:**
- Thumbnail generation timing (need to capture from Metal texture)
- Large file performance

**Mitigation:**
- Use background thread for save/load
- Progressive thumbnail loading in gallery
