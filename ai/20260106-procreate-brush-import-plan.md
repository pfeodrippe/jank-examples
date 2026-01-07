# Procreate Brush Import Implementation Plan

**Date:** 2026-01-06

## Research Summary

### .brush File Format
Procreate `.brush` files are **ZIP archives** containing:

```
brush-name.brush/
├── QuickLook/
│   └── Thumbnail.png    # Preview image (optional)
├── Shape.png            # Brush tip shape (optional)
├── Grain.png            # Texture/grain pattern (inverted: white=paint)
└── Brush.archive        # Binary plist with brush settings
```

### .brushset File Format
A `.brushset` is also a ZIP containing multiple brush folders:
```
brushset-name.brushset/
├── brush1/
│   ├── Brush.archive
│   ├── Shape.png
│   └── Grain.png
├── brush2/
│   └── ...
└── brushset.plist       # Brush order and metadata
```

### Brush.archive (bplist) Key Settings
Based on research, the binary plist contains:
- **Spacing**: How often brush stamps along path (0-100%)
- **Size jitter**: Random size variation
- **Opacity jitter**: Random opacity variation
- **Position jitter**: Scatter/lateral/linear offsets
- **Rotation jitter**: Random rotation per stamp
- **Blend mode**: How texture combines with stroke
- **Pressure curves**: Size/opacity response to pressure
- **Speed dynamics**: Size/opacity response to speed

## Implementation Phases

### Phase 1: ZIP Extraction (Day 1)
**Goal:** Extract files from .brush archives

**Tasks:**
1. Add zip library support (minizip or iOS built-in)
2. Create `BrushImporter` class with methods:
   - `importBrush(path)` - extract single .brush file
   - `importBrushSet(path)` - extract .brushset folder
3. Extract to temporary directory
4. Return struct with paths to extracted files

**Code Location:** `DrawingMobile/brush_importer.mm`

### Phase 2: PNG Texture Loading (Day 1)
**Goal:** Load Shape.png and Grain.png as Metal textures

**Tasks:**
1. Load PNG files using UIImage/CGImage
2. Handle grayscale conversion (textures should be grayscale)
3. Handle image inversion if needed (Procreate uses white=paint)
4. Create Metal textures via existing `metal_stamp_load_texture_from_data()`

**Integration:** Extend existing texture loading in `metal_renderer.mm`

### Phase 3: Binary Plist Parsing (Day 2)
**Goal:** Read brush settings from Brush.archive

**Tasks:**
1. Use NSPropertyListSerialization to parse bplist
2. Create `ProcreateeBrushSettings` struct with fields:
   ```cpp
   struct ProcreateBrushSettings {
       float spacing;          // 0.0-1.0
       float sizeJitter;       // 0.0-1.0
       float opacityJitter;    // 0.0-1.0
       float scatterAmount;    // 0.0-1.0
       float rotationJitter;   // degrees
       float sizePressure;     // -1.0 to 1.0
       float opacityPressure;  // -1.0 to 1.0
       // ... more as needed
   };
   ```
3. Map Procreate keys to our struct (requires reverse-engineering key names)

**Code Location:** `DrawingMobile/brush_settings_parser.mm`

### Phase 4: Settings Mapping (Day 2)
**Goal:** Convert Procreate settings to our Metal brush parameters

**Mapping Table:**
| Procreate Setting | Our Metal Parameter |
|-------------------|---------------------|
| Spacing | `brush_.spacing` |
| Size Jitter | `brush_.size_jitter` |
| Opacity Jitter | `brush_.opacity_jitter` |
| Scatter | `brush_.scatter` |
| Rotation Jitter | `brush_.rotation_jitter` |
| Size Pressure | `brush_.size_pressure` |
| Opacity Pressure | `brush_.opacity_pressure` |
| Grain Scale | `brush_.grain_scale` |

### Phase 5: Brush Picker UI (Day 3)
**Goal:** UI to browse and select imported brushes

**Tasks:**
1. Create brush library storage (documents directory)
2. Add "Import Brush" button that opens file picker
3. Display brush thumbnails in scrollable list
4. Tap to select brush and apply settings
5. Store imported brushes for persistence

**UI Design:**
```
[Import] [Library ▼]
┌─────────────────────┐
│ ○ Pencil            │
│ ○ Crayon           │
│ ● Custom Brush 1   │  ← Selected
│ ○ Custom Brush 2   │
└─────────────────────┘
```

### Phase 6: File Import Flow (Day 3)
**Goal:** Allow importing .brush files from Files app or sharing

**Tasks:**
1. Register app as handler for .brush/.brushset files
2. Implement `UIDocumentPickerViewController` for file selection
3. Copy imported brushes to app's documents directory
4. Update brush library after import

## API Design

### BrushImporter
```objc
@interface BrushImporter : NSObject

// Import single brush file, returns brush ID or -1 on failure
+ (int32_t)importBrushFromPath:(NSString*)path;

// Import brushset, returns array of brush IDs
+ (NSArray<NSNumber*>*)importBrushSetFromPath:(NSString*)path;

// Get list of imported brushes
+ (NSArray<NSDictionary*>*)getImportedBrushes;

// Apply brush settings by ID
+ (BOOL)applyBrush:(int32_t)brushId;

// Delete imported brush
+ (BOOL)deleteBrush:(int32_t)brushId;

@end
```

### Brush Data Structure
```cpp
struct ImportedBrush {
    int32_t id;
    std::string name;
    std::string thumbnailPath;
    int32_t shapeTextureId;   // -1 if none
    int32_t grainTextureId;   // -1 if none
    ProcreateBrushSettings settings;
};
```

## Files to Create/Modify

### New Files
- `DrawingMobile/brush_importer.h` - Import API header
- `DrawingMobile/brush_importer.mm` - ZIP extraction, plist parsing
- `DrawingMobile/brush_library.h` - Brush storage/management
- `DrawingMobile/brush_library.mm` - Persistence, listing

### Modified Files
- `DrawingMobile/drawing_mobile_ios.mm` - Add brush picker UI
- `DrawingMobile/project-jit-sim.yml` - Add zip library if needed
- `src/vybe/app/drawing/native/metal_renderer.mm` - Apply imported settings

## Dependencies

1. **ZIP handling**: Use iOS's built-in `NSFileManager` with `unzipItem` or add minizip
2. **Property List**: Use `NSPropertyListSerialization` (built-in)
3. **File Picker**: Use `UIDocumentPickerViewController` (built-in)

## Testing Plan

1. **Unit test ZIP extraction** with sample .brush file
2. **Test PNG loading** - verify grayscale conversion
3. **Test plist parsing** - dump all keys from real Procreate brush
4. **Integration test** - import brush, draw, verify texture applied
5. **UI test** - full import flow from Files app

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Unknown plist key names | Dump real Procreate brush, reverse-engineer keys |
| Different Procreate versions | Test with brushes from multiple versions |
| Large texture files | Resize to max 2048x2048, compress |
| Complex blend modes | Start with basic modes, add more later |

## Sources

- [Krita Artists - Procreate brush import prototype](https://krita-artists.org/t/procreate-brush-import-plugin-prototype/104894)
- [GitHub - geologic-patterns brush creator](https://github.com/davenquinn/geologic-patterns/blob/master/procreate-brushes/create-brushes)
- [Procreate Handbook - Brush Studio Settings](https://help.procreate.com/procreate/handbook/brushes/brush-studio-settings)
- [Procreate Help - Importing brushes](https://help.procreate.com/articles/daaqbd-importing-your-brushes)
