# iOS Incremental Build Optimization

## When to use incremental xcodebuild vs full make

### Only .mm/.m/.cpp files modified → Use xcodebuild directly
```bash
# Fast incremental build
xcodebuild -project DrawingMobile-JIT-Sim.xcodeproj -scheme DrawingMobile-JIT-Sim \
  -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPad Pro 13-inch (M4)' build

# Install and launch
xcrun simctl terminate 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim 2>/dev/null
xcrun simctl install 'iPad Pro 13-inch (M4)' ~/Library/Developer/Xcode/DerivedData/DrawingMobile-JIT-Sim-*/Build/Products/Debug-iphonesimulator/DrawingMobile-JIT-Sim.app
xcrun simctl launch 'iPad Pro 13-inch (M4)' com.vybe.DrawingMobile-JIT-Sim
```

### Metal shaders (.metal) or project config modified → Use full make
```bash
cd /Users/pfeodrippe/dev/something && make drawing-ios-jit-sim-run
```

### jank/clojure source modified → Use full make
The make command syncs jank sources to the bundle.

## Summary
- .mm/.m/.cpp/.h changes only → xcodebuild (fast, ~10-20 seconds)
- .metal/.yml/Makefile/jank changes → full make (slow, 2-3 minutes)
