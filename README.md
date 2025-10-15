# ![r3shape-labs](https://github.com/user-attachments/assets/ac634f13-e084-4387-aded-4679eb048cac)  <br> libECX

**libECX** an ECS implementation designed as a **minimal, cache-aware, fully deterministic entity–component runtime** implemented in **pure C99**.


## Architectural Model

The runtime consists of **five primary layers**:

### 1. Entity Layer

The fundamental unit of identity.

* 64-bit packed handles (`id | gen | salt | alive`) ensure lifetime validation.
* Fixed-size dense arrays store entity masks, generations, and free lists.
* Entity validity is O(1) to check.
* Binding is mask-based, ensuring fast component set filtering.

### 2. Component Layer

A SoA (Structure of Arrays) storage system for all component data.

* Each component type owns a **contiguous, arena-allocated field buffer**.
* Fields are described via `ECXFieldDesc` (`hash`, `stride`), with hash-based lookup.
* Supports flexible field counts (e.g. position[x,y,z], color[r,g,b], etc).
* Field offsets, strides, and hashes are precomputed — no runtime reflection.
* Field data is accessed directly via offset arithmetic or helper methods (`getField`, `setField`).

### 3. Query

The dynamic querying system that drives runtime iteration.

* **Queries** represent filter definitions (`all`, `any`, `none` bitmasks).
* All component data is stored in a unified **Arena allocator**.
* Each query maps to a config via the `query.config` table.
* When components are bound/unbound, configs are incrementally updated in O(1).
* `iter(config, sys, user)` provides high-performance iteration without reflection.

### 4. Config Layer
* **Configs** represent *cached entity sets* that satisfy those filters.
* Each internal subsystem (entity, component, query, config) maintains its own pools and free lists.

### 5. Composition Layer
* **Compositions** represent *the joined component dataf* that a configuration is composed of.

## libECX 1.1.0 Benchmark

Performance measured on **15,000,000 entities**:

| Operation                                      | Time (ms) | Throughput                |
| ---------------------------------------------- | --------- | ------------------------- |
| **Entity spawn + bind**                        | 48-49     | ~20.7M ent/sec            |
| **Query Configuration**                        | 100–113   | >5M ent/sec               |
| **Query Configuration (Compose + Decompose)**  | 130-190   | >5M ent/sec               |
| **Mutation pass (pos+vel)**                    | 480-505   | >29M ent/sec (0.5M-0.7M ent/frame @60fps) |

**Highlights:**

* **High-throughput ECS:** Millions of entities spawned, queried, and iterated at extreme speeds.
* **Sparse queries supported:** Efficient handling of partial active component sets.
* **Cache-friendly SoA design:** Minimal overhead for hot/cold passes.
* **Real-world mutations:** Read/write operations on vectors remain performant.

## Runtime Flow

A typical runtime pass:

```c
ECXComponent pos = newComponent((ECXComponentDesc){
    .mask = (1 << 0), .max = 100,
    .fields = 3,
    .fieldv = (ECXFieldDesc[]){
        { .hash="x", .stride=sizeof(f32) },
        { .hash="y", .stride=sizeof(f32) },
        { .hash="z", .stride=sizeof(f32) }
    }
});

ECXEntity e1 = newEntity();
bind(e1, pos);

setField(0, &(f32){123.4f}, e1, pos);

f32 val;
getField(0, &val, e1, pos);

ECXQuery query = query((ECXQueryDesc){ .all = (1 << 0) });
iter(query, sys, NULL);
```

## Key Properties

| Feature                 | Description                                                                |
| ----------------------- | -------------------------------------------------------------------------- |
| **Performance**         | O(1) entity/component access. No heap churn.                               |
| **Cache Coherence**     | SoA layout; field-major for SIMD/SoA traversal.                            |
| **Incremental Updates** | Configs and queries update immediately upon bind/unbind.                   |
| **Lifetime Safety**     | Generational handles prevent UAF and stale references.                     |
| **Reflection-Free**     | No strings or RTTI at runtime. All hashes precomputed.                     |
| **Arena-Based**         | All component fields allocated contiguously in arena memory.               |
| **Portable**            | Pure ISO C99. No platform dependencies.                                    |
| **Integration-Ready**   | Designed for direct integration into `UFCORE.dll` as Uniform’s ECS kernel. |

## Systems and Iteration

ECX adopts a **stateless system model**:
Systems are just function pointers with a consistent signature:

```c
typedef void (*ECXSystem)(ECXEntity e, void* user);
```

The runtime uses this lightweight iteration API:

```c
void iter(ECXConfig config, ECXSystem sys, void* user);
```

This allows for maximum control — systems are pure functions,
and queries/configs act as live, incrementally updated views of entity membership.

## Example Output

```
[r3kit::SUCCESS] position component: 256
[r3kit::SUCCESS] e1, e2, e3: 4294969765, 8589937061, 12884904357
[r3kit::SUCCESS] e1 bound to pos: 4294969765, 256
[r3kit::SUCCESS] e3 bound to pos: 12884904357, 256
[r3kit::SUCCESS] e1 field 0 (x field) got: 1234.43
[r3kit::SUCCESS] e3 field 0 (x field) got: 420.69
```

## Future Work (v2.0 Roadmap)

* **Reactive systems:** automatic config invalidation and rebuild on structural change.
* **Dependency graphs:** automatic system ordering and dependency management.
* **Thread-safe configs:** atomic bind/unbind for multi-threaded iteration.
* **Per-component slabs:** replace linear arena with fragment-reclaiming slab allocators.
* **Dynamic archetypes:** cached query graphs for faster filter reuse.
