# Plan: Implement defcomp Macro for jank

**Date**: 2025-12-08
**Status**: PLANNING

## Vybe's defcomp Overview

In vybe (Clojure), `defcomp` creates a VybeComponent that:
1. Defines a C struct layout using Java's Foreign Function Memory API
2. Registers the component with Flecs (size, alignment, name)
3. Provides a callable constructor: `(Position {:x 1.0 :y 2.0})`
4. Enables field access via map-like interface
5. Tracks field metadata (name, type, offset, getter, setter)

```clojure
;; Vybe usage:
(vp/defcomp Position
  [[:x :double]
   [:y :double]])

;; Creates instance
(Position {:x 10 :y 20})  ; => pointer-backed map
```

## jank's C++ Interop Capabilities

jank provides:
- `cpp/raw` - Embed raw C++ code
- `cpp/new Type` - Allocate new instance
- `cpp/value "StructType{...}"` - Create struct literal (compile-time only)
- `cpp/.-field` - Access struct fields
- `cpp/= target value` - Assign to field
- `cpp/box` / `cpp/unbox` - Box/unbox native values
- `cpp/type "TypeName"` - Reference C++ type
- `cpp/&` - Get address of value

## Key Challenges

### Challenge 1: Struct Definition
jank doesn't have a built-in way to define new C structs from Clojure-like syntax.
We need to use `cpp/raw` to generate struct definitions.

### Challenge 2: Dynamic Struct Instantiation
`cpp/value` requires compile-time string literals. We can't do:
```clojure
(cpp/value (str "Position{.x = " x "}"))  ; DOESN'T WORK
```

### Challenge 3: Field Access
Need to generate accessor functions for each field.

### Challenge 4: Flecs Registration
Components must be registered with Flecs using `ecs_component_init`.

## Proposed Solution

### Phase 1: Define C++ Struct via cpp/raw

The `defcomp` macro generates a C++ struct:

```clojure
(defcomp Position
  [[:x :float]
   [:y :float]])

;; Expands to:
(cpp/raw "
struct vybe_Position {
  float x;
  float y;
};
")
```

### Phase 2: Type Mapping

Map jank types to C++ types:
```clojure
(def type-map
  {:float "float"
   :double "double"
   :int "int32_t"
   :long "int64_t"
   :bool "bool"
   :pointer "void*"
   :u8 "uint8_t"
   :u16 "uint16_t"
   :u32 "uint32_t"
   :u64 "uint64_t"})
```

### Phase 3: Constructor Function

Use jank's native `cpp/=` for field setting (much simpler than generating C++ setters):

```clojure
;; Field access in jank:
(cpp/= (cpp/.-x p) (cpp/float. v))  ; Set field
(cpp/.-x (cpp/* p))                  ; Get field (dereference pointer first)

;; For Position, generate minimal C++ helpers:
(cpp/raw "
vybe_Position* vybe_Position_new() {
  return new vybe_Position{};
}
size_t vybe_Position_sizeof() { return sizeof(vybe_Position); }
size_t vybe_Position_alignof() { return alignof(vybe_Position); }
")
```

The `defcomp` macro generates a constructor that accepts a jank hash map:

```clojure
;; User writes:
(defcomp Position
  [[:x :float]
   [:y :float]])

;; Macro generates (simplified):
(cpp/raw "
struct vybe_Position { float x; float y; };
vybe_Position* vybe_Position_new() { return new vybe_Position{}; }
size_t vybe_Position_sizeof() { return sizeof(vybe_Position); }
size_t vybe_Position_alignof() { return alignof(vybe_Position); }
")

(defn Position
  "Create a new Position component."
  ([] (cpp/box (cpp/vybe_Position_new)))
  ([{:keys [x y]}]  ;; <-- destructuring generated from field names
   (let [p (cpp/vybe_Position_new)]
     (when x (cpp/= (cpp/.-x p) (cpp/float. x)))  ;; <-- generated per field
     (when y (cpp/= (cpp/.-y p) (cpp/float. y)))
     (cpp/box p))))

;; Usage - pass a normal jank hash map:
(Position {:x 10.0 :y 20.0})
(Position {:x 5.0})           ;; y defaults to 0
(Position {})                 ;; all defaults

;; Field access macros also generated:
(defmacro position-x [p] `(cpp/.-x (cpp/* (cpp/unbox (cpp/type "vybe_Position*") ~p))))
(defmacro position-y [p] `(cpp/.-y (cpp/* (cpp/unbox (cpp/type "vybe_Position*") ~p))))
```

The macro automatically:
1. Generates `:keys` destructuring from field names
2. Generates `cpp/=` setter calls for each field
3. Applies the correct type constructor (`cpp/float.`, `cpp/int.`, etc.) based on field type

### Phase 4: Component Metadata Record

Store component information for queries:

```clojure
(defrecord VybeComponent [name fields size alignment eid])

(def Position-meta
  {:name "Position"
   :fields [{:name :x :type :float :offset 0}
            {:name :y :type :float :offset 4}]
   :size (cpp/vybe_Position_sizeof)
   :alignment (cpp/vybe_Position_alignof)
   :eid nil})  ; Set after Flecs registration
```

### Phase 5: Flecs Registration using Meta API (ecs_struct)

**Key insight from Flecs docs**: Use `ecs_struct()` for runtime type generation with full reflection support!

```c
// Flecs built-in primitive types:
ecs_id(ecs_f32_t)   // float
ecs_id(ecs_f64_t)   // double
ecs_id(ecs_i32_t)   // int32_t
ecs_id(ecs_i64_t)   // int64_t
ecs_id(ecs_bool_t)  // bool
// etc.

// Register Position at runtime with ecs_struct:
ecs_entity_t Position = ecs_struct(world, {
    .entity = ecs_entity(world, { .name = "Position" }),
    .members = {
        { .name = "x", .type = ecs_id(ecs_f32_t) },
        { .name = "y", .type = ecs_id(ecs_f32_t) }
    }
});
```

**New architecture:**

1. `make-comp` - Returns a pure data descriptor (no world needed)
2. `defcomp` - Calls `make-comp`, stores descriptor in var
3. `register-comp!` - Takes world + descriptor, calls `ecs_struct`

```clojure
;; Type mapping to Flecs primitive IDs
(def flecs-type-map
  {:float  "ecs_id(ecs_f32_t)"
   :double "ecs_id(ecs_f64_t)"
   :i32    "ecs_id(ecs_i32_t)"
   :i64    "ecs_id(ecs_i64_t)"
   :bool   "ecs_id(ecs_bool_t)"
   :u8     "ecs_id(ecs_u8_t)"
   :u32    "ecs_id(ecs_u32_t)"
   :u64    "ecs_id(ecs_u64_t)"
   :string "ecs_id(ecs_string_t)"
   :entity "ecs_id(ecs_entity_t)"})
```

**C++ helper for registration** (handles dynamic member count):

```cpp
// In cpp/raw - helper to register struct with up to 8 members
ecs_entity_t vybe_register_struct(
    ecs_world_t* w,
    const char* name,
    int member_count,
    const char** member_names,
    ecs_entity_t* member_types
) {
    ecs_entity_desc_t entity_desc = {.name = name};
    ecs_entity_t e = ecs_entity_init(w, &entity_desc);

    ecs_struct_desc_t desc = {.entity = e};
    for (int i = 0; i < member_count && i < ECS_MEMBER_DESC_CACHE_SIZE; i++) {
        desc.members[i].name = member_names[i];
        desc.members[i].type = member_types[i];
    }

    return ecs_struct(w, &desc);
}
```

**jank registration function:**

```clojure
(defn register-comp!
  "Register a component descriptor with a Flecs world.
   Returns the Flecs entity ID for the component."
  [world comp]
  (let [desc (:descriptor comp)
        member-names (mapv :name (:fields desc))
        member-types (mapv #(get-flecs-type-id world (:type %)) (:fields desc))]
    ;; Call C++ helper
    (cpp/vybe_register_struct
      (world-ptr world)
      (:name desc)
      (count (:fields desc))
      member-names
      member-types)))
```

### Phase 6: Integration with with-query

Update `with-query` to accept component symbols:

```clojure
;; Current (string-based):
(vf/with-query w [_ "Position", e :vf/entity] ...)

;; Goal (component-based, like vybe):
(vf/with-query w [pos Position, e :vf/entity]
  (println "Entity" e "at" (:x pos) (:y pos)))
```

**How it works:**

1. `defcomp` stores component metadata in a var (e.g., `Position-meta`)
2. `with-query` macro detects component symbols (not strings, not keywords)
3. Builds query string from component names
4. Generates C++ helper to extract component data per entity
5. Binds component data to the symbol as a map

```clojure
;; defcomp generates metadata:
(def Position-meta
  {:name "Position"
   :struct-name "vybe_Position"
   :fields [{:name :x :type :float} {:name :y :type :float}]
   :size-fn vybe_Position_sizeof
   :alignment-fn vybe_Position_alignof
   :eid nil})  ;; Set after registration

;; with-query expansion for: (with-query w [pos Position, e :vf/entity] body)
(let [query-string "Position"  ;; Built from Position-meta
      entities (query-entities-with-components w query-string [Position])]
  (mapv (fn [[e pos]]  ;; pos is a map {:x ... :y ...}
          body)
        entities))
```

**C++ helper for component data extraction:**

```cpp
// Generated or generic helper to get component field values
jank::runtime::object_ref vybe_query_with_Position(ecs_world_t* w, ecs_query_t* q) {
  auto result = jank::runtime::make_box<jank::runtime::obj::persistent_vector>();
  ecs_iter_t it = ecs_query_iter(w, q);
  while (ecs_query_next(&it)) {
    vybe_Position* pos_arr = (vybe_Position*)ecs_field_w_size(&it, sizeof(vybe_Position), 0);
    for (int i = 0; i < it.count; i++) {
      ecs_entity_t entity = it.entities[i];
      vybe_Position* pos = &pos_arr[i];
      // Build map {:x pos->x :y pos->y}
      auto pos_map = /* create jank map with field values */;
      auto tuple = /* create [entity pos_map] */;
      result = jank::runtime::conj(result, tuple);
    }
  }
  return result;
}
```

**Multiple components:**

```clojure
(vf/with-query w [pos Position, vel Velocity, e :vf/entity]
  (println "Entity" e "pos:" pos "vel:" vel))

;; Generates query string: "Position, Velocity"
;; Returns tuples: [entity pos-map vel-map]
```

## make-comp: Internal Function

`make-comp` is a pure function that creates component descriptors without needing a world:

```clojure
(defn make-comp
  "Create a component descriptor from a name and field definitions.
   Returns a map that can later be registered with a Flecs world.

   Usage:
     (make-comp 'Position [[:x :float] [:y :float]])

   Returns:
     {:name \"Position\"
      :fields [{:name \"x\" :type :float}
               {:name \"y\" :type :float}]
      :cpp-struct-name \"vybe_Position\"}"
  [sym fields]
  (let [field-maps (mapv (fn [[fname ftype]]
                           {:name (name fname)
                            :type ftype})
                         fields)]
    {:name (str sym)
     :fields field-maps
     :cpp-struct-name (str "vybe_" sym)}))

;; Example:
(make-comp 'Position [[:x :float] [:y :float]])
;; => {:name "Position"
;;     :fields [{:name "x" :type :float} {:name "y" :type :float}]
;;     :cpp-struct-name "vybe_Position"}
```

**defcomp uses make-comp internally:**

```clojure
(defmacro defcomp
  "Define a Flecs component. Creates:
   1. Component descriptor (via make-comp)
   2. C++ struct definition
   3. Constructor function
   4. Field accessor macros"
  [sym fields]
  `(do
     ;; Store descriptor
     (def ~sym (make-comp '~sym ~fields))

     ;; Generate C++ struct (via cpp/raw)
     ~(generate-cpp-struct sym fields)

     ;; Constructor function accepts hash map
     ~(generate-constructor sym fields)))
```

## Test Plan

### Test 1: make-comp creates correct descriptor

```clojure
(deftest make-comp-test
  (testing "make-comp creates descriptor without world"
    (let [desc (make-comp 'Position [[:x :float] [:y :float]])]
      (is (= "Position" (:name desc)))
      (is (= 2 (count (:fields desc))))
      (is (= "x" (:name (first (:fields desc)))))
      (is (= :float (:type (first (:fields desc))))))))
```

### Test 2: defcomp creates usable component

```clojure
(deftest defcomp-test
  (testing "defcomp creates component with constructor"
    ;; Define component
    (defcomp TestPosition [[:x :float] [:y :float]])

    ;; Check descriptor
    (is (= "TestPosition" (:name TestPosition)))

    ;; Create instance with hash map
    (let [pos (TestPosition {:x 10.0 :y 20.0})]
      (is (some? pos)))))
```

### Test 3: Register component with Flecs world

```clojure
(deftest register-comp-test
  (testing "component can be registered with Flecs world"
    (defcomp MyPosition [[:x :float] [:y :float]])

    (let [w (vf/make-world)
          ;; Register component - returns Flecs entity ID
          comp-eid (vf/register-comp! w MyPosition)]
      (is (> comp-eid 0))

      ;; Component should be queryable by name
      (let [lookup-eid (fl/ecs_lookup (vf/world-ptr w) "MyPosition")]
        (is (= comp-eid lookup-eid)))

      (vf/destroy-world! w))))
```

### Test 4: Use component in entity and query

```clojure
(deftest component-with-entity-test
  (testing "component can be added to entity and queried"
    (defcomp GamePosition [[:x :float] [:y :float]])

    (let [w (vf/make-world)
          comp-eid (vf/register-comp! w GamePosition)
          e (vf/new-entity w)]

      ;; Add component to entity
      (vf/set-comp! w e GamePosition {:x 100.0 :y 200.0})

      ;; Query entities with component
      (let [results (vf/with-query w [pos GamePosition, e :vf/entity]
                      {:entity e :x (:x pos) :y (:y pos)})]
        (is (= 1 (count results)))
        (is (= 100.0 (:x (first results))))
        (is (= 200.0 (:y (first results)))))

      (vf/destroy-world! w))))
```

## Complete defcomp Macro Design

```clojure
(defmacro defcomp
  "Define a Flecs component type.

   Usage:
     (defcomp Position
       [[:x :float]
        [:y :float]])

   Creates:
   - A C++ struct: vybe_Position
   - Constructor function: (Position {:x 1.0 :y 2.0})
   - Field accessors: (position-x p), (position-set-x! p v)
   - Component metadata for Flecs registration"
  [sym fields]
  (let [struct-name (str "vybe_" sym)
        fields-parsed (mapv (fn [[fname ftype]] {:name fname :type ftype}) fields)
        cpp-fields (->> fields-parsed
                        (map (fn [{:keys [name type]}]
                               (str "  " (type-map type) " " (clojure.core/name name) ";")))
                        (apply str "\n"))
        ;; Generate struct definition
        struct-def (str "struct " struct-name " {\n" cpp-fields "\n};")
        ;; Generate helper functions
        helpers (generate-helpers struct-name fields-parsed)]
    `(do
       ;; Define C++ struct and helpers
       (cpp/raw ~(str struct-def "\n" helpers))

       ;; Store component metadata
       (def ~(symbol (str sym "-meta"))
         {:name ~(str sym)
          :struct-name ~struct-name
          :fields ~fields-parsed
          :size (cpp/~(symbol (str struct-name "_sizeof")))
          :alignment (cpp/~(symbol (str struct-name "_alignof")))})

       ;; Constructor function
       (defn ~sym
         ~(str "Create a new " sym " component.")
         ([] (cpp/box (cpp/~(symbol (str struct-name "_new")))))
         ([params#]
          (let [p# (cpp/~(symbol (str struct-name "_new")))]
            ~@(for [{:keys [name type]} fields-parsed]
                `(when-let [v# (~(keyword name) params#)]
                   (cpp/~(symbol (str struct-name "_set_" (clojure.core/name name)))
                         p# (~(type-constructor type) v#))))
            (cpp/box p#)))))))
```

## Implementation Steps

### Step 1: Create helper function generators
- `generate-struct-def`
- `generate-accessors`
- `generate-constructor`

### Step 2: Implement basic defcomp
- C++ struct generation
- Constructor with map params
- Field getters/setters

### Step 3: Implement Flecs registration
- `register-component!` function
- Integration with world creation

### Step 4: Update with-query
- Accept component symbols
- Build query from component names
- Extract field data from iterator

### Step 5: Testing
- Define Position component
- Create instances
- Register with Flecs
- Query entities with component

## Example Final Usage

```clojure
;; Define component
(defcomp Position
  [[:x :float]
   [:y :float]])

;; Create world and register
(let [w (vf/make-world)]
  (vf/register-component! w Position)

  ;; Create entity with component
  (let [e (vf/new-entity w)
        pos (Position {:x 10.0 :y 20.0})]
    (vf/set-component! w e pos))

  ;; Query entities
  (vf/with-query w [pos Position, e :vf/entity]
    (println "Entity" e "at" (position-x pos) (position-y pos))))
```

## Open Questions

1. **Memory Management**: Who owns the allocated struct memory? Need to handle cleanup.

2. **Map-like Interface**: Should component instances behave like maps? (More complex)

3. **Nested Components**: Support components containing other components?

4. **Arrays**: Support fixed-size arrays in components?

5. **Alignment**: How to handle struct alignment across platforms?

## Next Steps After Planning

1. Implement minimal `defcomp` with:
   - C++ struct generation via `cpp/raw`
   - Basic constructor
   - Field getters/setters

2. Test with simple Position component

3. Add Flecs registration

4. Update `with-query` to work with components
