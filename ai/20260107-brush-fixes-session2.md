# Brush Picker Fixes - Session 2

## Issues Fixed

### 1. Brush Names Showing UUIDs Instead of Proper Names

**Problem**: Brush names displayed as UUIDs in the picker instead of actual names like "Jingles", "Peppermint", etc.

**Root Cause**: `CFKeyedArchiverUID` objects don't respond to `intValue` selector. The code was checking `[nameRef respondsToSelector:@selector(intValue)]` which returned NO for UID objects.

**Solution**: Added `extractUIDValue:` helper method that parses the CFKeyedArchiverUID description string to extract the integer value:

```objc
+ (NSInteger)extractUIDValue:(id)uidRef {
    if (!uidRef) return -1;

    // First try intValue/integerValue (for NSNumber)
    if ([uidRef respondsToSelector:@selector(integerValue)]) {
        return [uidRef integerValue];
    }

    // Parse CFKeyedArchiverUID description: "<CFKeyedArchiverUID ...>{value = 83}"
    NSString* desc = [uidRef description];
    NSRange range = [desc rangeOfString:@"value = "];
    if (range.location != NSNotFound) {
        NSString* valueStr = [desc substringFromIndex:range.location + range.length];
        valueStr = [valueStr stringByTrimmingCharactersInSet:
                    [NSCharacterSet characterSetWithCharactersInString:@"}"]];
        return [valueStr integerValue];
    }
    return -1;
}
```

**Files Modified**: `brush_importer.mm` (lines 433-453)

### 2. Fizzle and Wobble Brushes Selecting "Random" Brushes

**Problem**: Selecting brushes 8 (Fizzle) or 12 (Wobble) appeared to select random brushes.

**Root Cause**: These brushes use Procreate's **bundled shape resources** (e.g., `bundledShapePath = "Infrared-Oil.jpg"`) instead of custom `Shape.png` files. Since we don't have access to Procreate's internal resources, these brushes have `shapeTextureId = -1`.

When switching from a brush WITH a texture to one WITHOUT, the renderer continued using the previous brush's texture, making it appear that a "random" brush was selected.

**Solution**:
1. Show **red X indicator** for unsupported brushes in the picker
2. **Block selection** of unsupported brushes (keep current brush active)

**Files Modified**:
- `drawing_mobile_ios.mm` - Added red X indicator for brushes with `shapeTextureId < 0`
- `brush_importer.mm` - Added check in `applyBrush:` to reject unsupported brushes

### Key Learning: Procreate Bundled Resources

Procreate brushes can use either:
1. **Custom shapes** - `Shape.png` in the brush folder (we support these)
2. **Bundled shapes** - Referenced via `bundledShapePath` in Brush.archive (we DON'T have these)

Example from Fizzle brush archive:
```
"bundledShapePath" => <CFKeyedArchiverUID>{value = 2}
// where value 2 = "Infrared-Oil.jpg" (internal Procreate resource)
```

## Commands Used

```bash
# Check if brush has Shape.png
ls -la "brush_folder/"

# Check bundledShapePath in brush archive
plutil -p "Brush.archive" | grep -i "bundled\|shape"

# Verify CFKeyedArchiverUID parsing
# Test program that extracts value from UID description
```

## Files Modified

1. **brush_importer.mm**
   - Added `extractUIDValue:` helper for CFKeyedArchiverUID parsing
   - Updated `parseBrushArchive:` to use new helper for name extraction
   - Added unsupported brush check in `applyBrush:` - returns NO for brushes without shape texture

2. **drawing_mobile_ios.mm**
   - Updated `drawBrushPicker()` to check for unsupported brushes FIRST
   - Added red X indicator (dark red circle with red cross lines) for unsupported brushes

## What's Next

- Consider adding support for Procreate's basic shape presets (circle, square, etc.)
- The bundled shapes like "Infrared-Oil.jpg" would require reverse-engineering or recreation
- Could potentially extract shape data from brush thumbnails as a fallback
