# Jank → Swift Direct Interoperability Research

## The Core Question
**Can jank generate Swift components directly, without any bridge (Objective-C or otherwise)?**

## TL;DR Answer

**Yes and no.** There's no way to generate *pure Swift code* from jank, but there are **three viable architectures** that achieve seamless jank→Swift integration:

1. **Swift 5.9+ C++ Interop** - Direct bidirectional C++↔Swift calls
2. **@_cdecl C ABI** - Swift exposes C functions callable from jank's C++
3. **Data-Driven UI** - jank sends state/data, Swift renders (most promising!)

---

## Architecture 1: Swift 5.9+ C++ Interoperability

Swift 5.9 introduced **native C++ interoperability** without Objective-C bridges.

### How It Works

```
┌─────────────┐      direct call      ┌─────────────┐
│   jank      │ ──────────────────→   │   Swift     │
│   (C++)     │ ←──────────────────   │   Code      │
└─────────────┘      direct call      └─────────────┘
```

### Enabling C++ Interop

In Xcode Build Settings or Swift flags:
```
-cxx-interoperability-mode=default
```

In Swift Package Manager:
```swift
.target(
    name: "MyTarget",
    swiftSettings: [.interoperabilityMode(.Cxx)]
)
```

### What Works

| C++ Feature | Swift Availability |
|-------------|-------------------|
| Classes/structs (with copy ctor) | ✅ Imported as value types |
| Methods | ✅ Callable directly |
| Free functions | ✅ Callable |
| Enums | ✅ Imported |
| Data members | ✅ Accessible |
| Template specializations | ✅ Via type alias |
| C++ → Swift calls | ✅ Via generated header |

### What Doesn't Work

| C++ Feature | Status |
|-------------|--------|
| Non-specialized templates | ❌ Not available |
| Virtual functions | ❌ Partial |
| R-value references (T&&) | ❌ Not supported |
| `std::function`, `std::variant` | ❌ Not yet |
| Catching C++ exceptions | ❌ Terminates program |
| C++20 modules | ❌ Not supported |

### Jank Integration Possibility

jank generates C++ code. That C++ code could:
1. Include Swift-generated C++ headers
2. Call Swift APIs directly
3. Pass jank data structures to Swift

**Challenge**: jank uses its own object model (boxed values, persistent data structures). Swift would need wrappers to understand `jank::runtime::object_ptr`.

### Resources
- [Swift C++ Interop Guide](https://www.swift.org/documentation/cxx-interop/)
- [Supported Features Status](https://www.swift.org/documentation/cxx-interop/status/)
- [WWDC23: Mix Swift and C++](https://developer.apple.com/videos/play/wwdc2023/10172/)

---

## Architecture 2: @_cdecl C ABI Functions

Swift can expose functions with C linkage using `@_cdecl`:

### Swift Side
```swift
@_cdecl("swift_create_button")
public func createButton(title: UnsafePointer<CChar>) -> OpaquePointer {
    let swiftTitle = String(cString: title)
    let button = UIButton()
    button.setTitle(swiftTitle, for: .normal)
    return Unmanaged.passRetained(button).toOpaque()
}

@_cdecl("swift_show_view")
public func showView(_ ptr: OpaquePointer) {
    let view = Unmanaged<UIView>.fromOpaque(UnsafeRawPointer(ptr)).takeUnretainedValue()
    // Add to view hierarchy
}
```

### jank/C++ Side
```cpp
extern "C" void* swift_create_button(const char* title);
extern "C" void swift_show_view(void* view);

// In jank code:
void* button = swift_create_button("Click Me");
swift_show_view(button);
```

### Pros
- Simple, stable ABI
- Works today
- No complex type mapping needed

### Cons
- `@_cdecl` is underscore-prefixed (unofficial)
- Manual memory management with `Unmanaged`
- Only functions, not types

### Proposal SE-0495
There's an active proposal to formalize `@cdecl`:
[SE-0495: @cdecl attribute](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0495-cdecl.md)

### Resources
- [GitHub Gist: Swift to C++ via @_cdecl](https://gist.github.com/HiImJulien/c79f07a8a619431b88ea33cca51de787)
- [Swift Forums: Formalizing @cdecl](https://forums.swift.org/t/formalizing-cdecl/40677)

---

## Architecture 3: Data-Driven UI (Most Promising!)

**This is the architecture that best fits jank's paradigm.**

### The Key Insight

> "If Swift components were data-driven, then we could do it from jank somehow (as it will have hot reload and all because of nREPL!)"

SwiftUI is **already** declarative and data-driven:
- UI is a function of state: `UI = f(state)`
- State changes → automatic re-render
- This is exactly like React!

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        iOS App                               │
│  ┌──────────────┐        ┌──────────────────────────────┐  │
│  │   jank       │  data  │         SwiftUI              │  │
│  │   Runtime    │ ─────→ │    (renders from data)       │  │
│  │              │        │                              │  │
│  │  - Lisp DSL  │ ←───── │    (events/callbacks)        │  │
│  │  - nREPL     │        │                              │  │
│  │  - Hot reload│        └──────────────────────────────┘  │
│  └──────────────┘                                          │
└─────────────────────────────────────────────────────────────┘
```

### How It Would Work

1. **jank defines UI declaratively** (like Hiccup/Reagent):
```clojure
(def my-view
  {:type :vstack
   :children
   [{:type :text :value "Hello from jank!"}
    {:type :button
     :title "Click me"
     :on-click #(println "Clicked!")}]})
```

2. **Swift interprets the data and renders**:
```swift
struct DynamicView: View {
    let spec: [String: Any]

    var body: some View {
        switch spec["type"] as? String {
        case "vstack":
            VStack { /* render children */ }
        case "text":
            Text(spec["value"] as! String)
        case "button":
            Button(spec["title"] as! String) {
                // callback to jank
            }
        // ...
        }
    }
}
```

3. **State lives in jank** (atoms):
```clojure
(def app-state (atom {:count 0}))

(defn increment! []
  (swap! app-state update :count inc))

;; Changes trigger re-render in Swift
```

### Server-Driven UI Pattern

This pattern is called **Server-Driven UI (SDUI)** and is widely used:
- Used by Airbnb, Spotify, Yelp for dynamic UIs
- Updates UI without app store releases
- Perfect for hot-reload development

### Existing Libraries

| Library | Description |
|---------|-------------|
| [DrivenUI](https://cocoapods.org/pods/DrivenUI) | iOS SDK for SDUI with JSON |
| [SwiftUI-Server-Driver-UI](https://github.com/AnupAmmanavar/SwiftUI-Server-Driver-UI) | Reference implementation |

### Why This Is Perfect for jank

1. **Hot reload via nREPL** - Change UI definition, see it instantly
2. **Lisp DSL** - Beautiful, composable UI definitions
3. **State in jank** - Atoms, watchers, reactive programming
4. **Swift does rendering** - Native performance, native look
5. **Minimal bridge** - Just data (maps/vectors → JSON → Swift)

### Implementation Path

1. Create a component registry in Swift (Text, Button, VStack, etc.)
2. Define a protocol for jank→Swift communication (JSON or C++ structs)
3. Pass UI specs from jank to Swift
4. Handle callbacks from Swift back to jank (via function IDs)

### Resources
- [Server-Driven UI with SwiftUI](https://medium.com/@pubudumihiranga/server-driven-ui-with-swiftui-a9ed31fb843b)
- [SwiftUI Server Driven Architecture](https://betterprogramming.pub/build-a-server-driven-ui-using-ui-components-in-swiftui-466ecca97290)
- [Server-Driven UI Design](https://medium.com/@kalidoss.shanmugam/server-driven-ui-design-with-swiftui-53097ffa765c)

---

## React Native's Approach (Reference)

React Native solved this same problem:

### Old Architecture (Bridge)
```
JavaScript → JSON → Bridge → Native
```
Slow, async, lots of serialization.

### New Architecture (JSI + Fabric)
```
JavaScript → JSI (C++) → Shadow Tree (C++) → Native UI
```
- Direct memory access
- Synchronous calls
- C++ Shadow Tree for layout
- No JSON serialization

### Jank Advantage

jank is *already* C++! No need for JSI-style bridging:
```
jank (C++) → Data Structures → Swift UI Layer
```

### Resources
- [React Native New Architecture Deep Dive](https://medium.com/@DhruvHarsora/deep-dive-into-react-natives-new-architecture-jsi-turbomodules-fabric-yoga-234bbdf853b4)
- [Fabric Renderer](https://blog.codeminer42.com/react-native-architecture-from-bridge-to-fabric/)

---

## Comparison Table

| Approach | Complexity | Hot Reload | Native Feel | Type Safety |
|----------|------------|------------|-------------|-------------|
| Swift 5.9+ C++ Interop | Medium | Partial | ✅ | ✅ |
| @_cdecl Functions | Low | Partial | ✅ | ❌ |
| **Data-Driven UI** | Medium | ✅ Full | ✅ | Partial |
| Objective-C Bridge | High | Partial | ✅ | ❌ |

---

## Recommended Implementation

### Phase 1: Proof of Concept
1. Create a minimal SwiftUI component registry
2. Define JSON schema for UI specs
3. jank generates UI spec, Swift renders

### Phase 2: Native Integration
1. Use C++ structs instead of JSON (faster)
2. Implement callback system for events
3. Add state synchronization (jank atoms → SwiftUI @State)

### Phase 3: Full Integration
1. Hiccup-like DSL in jank for UI definition
2. Macro system for compile-time UI optimization
3. Direct SwiftUI component wrapping via C++ interop

---

## Conclusion

**Direct Swift code generation from jank is not practical**, but **data-driven UI is the superior approach**:

1. jank excels at: data transformation, state management, hot reload
2. Swift excels at: native UI rendering, platform integration
3. Best of both: jank manages state/logic, Swift renders UI from data

This is essentially what React Native, Flutter (via Dart), and other cross-platform frameworks do - and it works beautifully.

The key insight is that **you don't need to generate Swift code** if you can **describe UI as data** that Swift interprets. This is more flexible, enables hot reload, and leverages each language's strengths.

---

## Next Steps

1. Prototype a simple `UIComponentSpec` struct in C++
2. Create matching Swift `UIBuilder` that renders from spec
3. Test round-trip: jank → C++ struct → Swift render → callback → jank
4. Document and iterate

---

## Sources

- [Swift C++ Interoperability](https://www.swift.org/documentation/cxx-interop/)
- [Swift C++ Interop Status](https://www.swift.org/documentation/cxx-interop/status/)
- [Project Build Setup](https://www.swift.org/documentation/cxx-interop/project-build-setup/)
- [GitHub: Getting Started with C++ Interop](https://github.com/swiftlang/swift/blob/main/docs/CppInteroperability/GettingStartedWithC++Interop.md)
- [SE-0495: @cdecl Proposal](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0495-cdecl.md)
- [@_cdecl Gist Example](https://gist.github.com/HiImJulien/c79f07a8a619431b88ea33cca51de787)
- [Server-Driven UI with SwiftUI](https://medium.com/@pubudumihiranga/server-driven-ui-with-swiftui-a9ed31fb843b)
- [SwiftUI Server Driven UI](https://betterprogramming.pub/build-a-server-driven-ui-using-ui-components-in-swiftui-466ecca97290)
- [React Native New Architecture](https://medium.com/@DhruvHarsora/deep-dive-into-react-natives-new-architecture-jsi-turbomodules-fabric-yoga-234bbdf853b4)
- [DrivenUI SDK](https://cocoapods.org/pods/DrivenUI)
