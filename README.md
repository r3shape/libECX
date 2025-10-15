# ![r3shape-labs](https://github.com/user-attachments/assets/ac634f13-e084-4387-aded-4679eb048cac)  <br> libECX

**libECX** an ECS implementation designed as a **minimal, cache-aware, fully deterministic entity–component runtime** implemented in **pure C99**.

## libECX 1.1.0 Benchmark

Performance measured on **15,000,000 entities**

| Operation                                     | Time (ms) | Throughput                                           |
| --------------------------------------------- | --------- | ---------------------------------------------------- |
| **Entity Spawn + Bind (pos + vel)**           | 401.8     | **37.3 M ent/sec**                                   |
| **Query Configuration (Compose + Decompose)** | 76.7      | **13.0 M ent/sec**                                   |
| **Move System — Single Frame (pos += vel)**   | 134.6     | **111.4 M ent/sec** (~ **1.9 M ent/frame @ 60 fps**) |

### Highlights

* **`ECXComposition`:** unified component-field composition with full arena locality.
* **Pointer-based iteration:** eliminated composition copy overhead for massive speedup.
* **Peak throughput:** > 110 M entities/sec on single-threaded CPU workloads.
* **Fully deterministic:** every run produces identical state evolution and timings.
* **Memory-local design:** all component and query data share a single arena allocator.


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

## Systems and Iteration

libECX adopts a **stateless system model**:
Systems are just function pointers with a consistent signature:

```c
typedef none (*ECXSystem)(u32 index, ptr user, ECXComposition* comp);
```

The runtime uses this lightweight iteration API:

```c
none iter(ECXQuery query, ECXSystem sys, ptr user);
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
