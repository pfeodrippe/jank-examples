# Brush Picker Selection Bug - Deep Investigation

## Problem Statement
When selecting certain brushes (positions 5 and 11) in the picker, the wrong brush is consistently selected.

## Root Cause Found: Incorrect Brush Ordering

### The Discovery
Procreate brushsets contain a `brushset.plist` file that defines the **correct brush order**:

```
brushset.plist -> "brushes" array:
  0: 53698C8E-C3D1-4276-B6FD-4BF5B5DBA19D
  1: 3DADE17C-E443-4FA4-8D6B-8C8AF12B7287
  2: C10D8A74-61DB-4B23-89D9-233BC321F405
  3: 46412476-4930-464E-A6CC-3A34D7876B06
  4: 71261DD8-142B-45D3-AF46-F15714E3F0D4
  5: 497781B3-A47F-4708-8ADC-B141EEFD7299
  6: D5980049-6D0A-40AB-9362-9345EA266CB9
  7: CD7EE973-D0A4-4014-8425-D0504B1D4976
  8: BDD65EDD-8A73-45DC-B63D-A460BDB8A721
  9: 7249FD62-8BD6-46DC-8F05-90CB78647223
  10: 466DDD98-2148-45D6-8F54-05A88E7A92D3
  11: 25F2BA53-A33D-4741-B4AC-DFFC1E626847
  12: 9A838558-8DBC-41B6-9CE2-E3E4C81EC6A8
  13: 45DD0DAF-35A3-4F2E-8A12-BBB2AC336322
```

### Current (Wrong) Behavior
Our code sorts directories **alphabetically by UUID**, which gives:
```
  0: 25F2BA53-A33D-4741-B4AC-DFFC1E626847  ← WRONG! Intended: position 11
  1: 3DADE17C-E443-4FA4-8D6B-8C8AF12B7287  ← OK (intended: 1)
  2: 45DD0DAF-35A3-4F2E-8A12-BBB2AC336322  ← WRONG! Intended: position 13
  3: 46412476-4930-464E-A6CC-3A34D7876B06  ← OK (intended: 3)
  4: 466DDD98-2148-45D6-8F54-05A88E7A92D3  ← WRONG! Intended: position 10
  5: 497781B3-A47F-4708-8ADC-B141EEFD7299  ← OK (intended: 5)
  6: 53698C8E-C3D1-4276-B6FD-4BF5B5DBA19D  ← WRONG! Intended: position 0
  7: 71261DD8-142B-45D3-AF46-F15714E3F0D4  ← WRONG! Intended: position 4
  ... etc
```

### Why This Causes "Wrong Brush" Selection
1. The brush picker DISPLAYS thumbnails in our (wrong) alphabetical order
2. The hit-testing correctly maps tap → array index
3. But our array order ≠ Procreate's intended order
4. So tapping what visually LOOKS like brush 5 gives you a different brush than expected

### The Fix
Parse `brushset.plist` and use its `brushes` array order:

```objc
+ (NSArray<NSNumber*>*)importBrushSetFromURL:(NSURL*)url {
    // ... extract to extractedDir ...

    // Parse brushset.plist to get the correct order
    NSURL* plistURL = [extractedDir URLByAppendingPathComponent:@"brushset.plist"];
    NSData* plistData = [NSData dataWithContentsOfURL:plistURL];
    NSDictionary* plist = [NSPropertyListSerialization propertyListWithData:plistData ...];
    NSArray* brushOrder = plist[@"brushes"];  // Array of UUID strings

    // Import brushes in the ORDER specified by brushset.plist
    for (NSString* uuid in brushOrder) {
        NSURL* brushDir = [extractedDir URLByAppendingPathComponent:uuid];
        if ([fm fileExistsAtPath:brushDir.path]) {
            int32_t brushId = [self importBrushFromExtractedDirectory:brushDir];
            if (brushId > 0) {
                [brushIds addObject:@(brushId)];
            }
        }
    }

    return brushIds;
}
```

## Additional Discovery: Brushset Metadata

The `brushset.plist` also contains:
- `name`: "12 Christmas 2025" - the brushset display name
- `iconName`: "custom-tree" - the icon to show for the brushset

## Commands Used
```bash
# List brushset contents
unzip -l brushes.brushset

# Extract and view the plist
unzip -p brushes.brushset brushset.plist | plutil -p -

# Compare sorted UUIDs vs plist order
unzip -l brushes.brushset | grep "Brush.archive$" | awk '{print $NF}' | sed 's|/Brush.archive||' | sort
```

## Files to Modify
1. **brush_importer.mm** - `importBrushSetFromURL:` method
   - Parse `brushset.plist`
   - Import brushes in the order specified by `brushes` array
   - Remove or keep alphabetical sort as fallback (if no plist)

## What's Next
1. ~~Implement the fix to use brushset.plist order~~ DONE
2. ~~Add brush names to picker (like Procreate)~~ DONE
3. ~~Test all brushes to verify correct ordering~~ DONE - Verified via logs

## Implementation: Brush Order Fix
Modified `importBrushSetFromURL:` in brush_importer.mm to:
1. Parse `brushset.plist` if it exists
2. Read the `brushes` array which contains UUIDs in correct order
3. Import brushes in that order
4. Fall back to alphabetical sort if no plist found

## Implementation: Brush Names in Picker
Added text rendering using CoreText to display brush names:

1. **Text Rendering Helper Functions** (drawing_mobile_ios.mm):
   - `renderTextToTexture()` - Creates RGBA texture from text using CoreText
   - `getBrushNameTexture()` - Caches text textures by brushId for performance
   - Added `g_brushNameTextures` and `g_brushNameSizes` caches

2. **Modified `drawBrushPicker()`** to:
   - Render brush name below each thumbnail
   - Add semi-transparent black background for readability
   - Center text horizontally within item

## Files Modified

1. **brush_importer.mm**
   - `importBrushSetFromURL:` - Now parses `brushset.plist` for correct brush order
   - Falls back to alphabetical sort if plist not found

2. **drawing_mobile_ios.mm**
   - Added `#import <CoreText/CoreText.h>`
   - Added text rendering functions and caches
   - Modified `drawBrushPicker()` to show brush names

## Build Notes
- Only .mm files were modified - no need for full `make` rebuild
- Xcode handles incremental builds for Objective-C++ files
