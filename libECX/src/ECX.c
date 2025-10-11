/*
    ECX Proof of concept using r3kit ds/arr API for better funtionality and saftey
    @zafflins - 9/3/2025
*/

#include <r3kit/include/mem/alloc.h>
#include <r3kit/include/mem/arena.h>
#include <r3kit/include/io/log.h>
#include <libECX/include/ECX.h>

static struct ECX {
    u8 init;
    Arena_Allocator arena;  // all component field arrays are stored here

    // entity ID layout: 32bits | 16bits | 15bits | 1bit
    //                      ID      GEN     SALT    ALIVE
    // entity internal arrays alloced statically
    struct {
        u32 next;          // next entity handle
        u32 count;         // alive entity count
        Array gen;         // array of entity generations
        Array free;        // array of free entity handles
        Array mask;        // array of entity component masks
    } entity;

    // component internal arrays alloced statically
    struct {
        u8 next;                // array of next component handle
        u8 count;               // number of components
        Array free;             // array of free component handles

        Array gen;              // array of component generations
        Array mask;             // array of component masks
        Array hash;             // array of component hashes
        Array field;            // array of component field buffers | [position=[x1, x2, x3 | y1, y2, y3 | z1, z2, z3], color=[r1,r2,r3 | g1,g2,g3 | b1,b2,b3] ...]
        Array fieldMax;         // array of component field maximums
        Array fieldSize;        // array of component field sizes
        Array fieldHash;        // array of component field hashes
        Array fieldCount;       // array of component field counts
        Array fieldStride;      // array of component field strides
        Array fieldOffset;      // array of component field offsets
    } component;

    // query internal arrays will grow dynamically up to max (alloced at min)
    struct {
        u16 next;               // array of next query handle
        u16 count;              // number of query handles
        Array free;             // array of free query handles
        
        Array all;              // array of config `all` masks
        Array any;              // array of config `any` masks
        Array none;             // array of config `none` masks
        Array seen;             // number of times a query was computed
        Array config;           // config IDs offset by query     | [ position+color+velocity(0, 1, 2, 3, 4, 5) | position+color(3, 1, 5) ]
        Array cached;           // boolean flag indicating query caching
        Array localToGlobal;    // ltg[local] = global (query)
        Array globalToLocal;    // gtl[global] = local (query)
    } query;

    // config internal arrays will grow dynamically up to max (alloced at min)
    struct {
        u16 next;               // array of nect config handle
        u16 count;              // number of config handles
        Array free;             // array of free config handles

        Array signature;        // array of config signatures (XOR hash of component masks)  | [ position+color+velocity(0, 1, 2, 3, 4, 5) | position+color(3, 1, 5) ]
        Array entitySet;        // array of entity IDs
        Array entityCount;      // array of entity counts
        Array componentSet;     // array of component IDs
        Array componentCount;   // array of component counts
        Array localToGlobal;    // ltg[local] = global (config)
        Array globalToLocal;    // gtl[global] = local (config)
    } config;
} ECX = {NULL};

// and here we have batshit ~@zafflins 9/15/25
static inline ECXEntity _packEntity(ECXEntity e, u16 g, u16 s, u8 a) {
    return ((e & 0xFFFFFFFF)   << 32)  |
           ((g & 0xFFFF)       << 16)  |
           ((s & 0x7FFF)       << 1)   |
           ((a & 0x1)          << 0)   ;
} static inline u32 _entityID(ECXEntity e) {
    return ((e >> 32) & 0xFFFFFFFF);
} static inline u16 _entityGEN(ECXEntity e) {
    return ((e >> 16) & 0xFFFF);
} static inline u16 _entitySALT(ECXEntity e) {
    return ((e >> 1) & 0x7FFF);
} static inline u8 _entityALIVE(ECXEntity e) {
    return ((e >> 0) & 0x1);
} static inline ECXComponent _packComponent(ECXComponent c, u8 g) {
    return ((c & 0xFF) << 8) | ((g & 0xFF) << 0);
} static inline u16 _componentID(ECXComponent c) {
    return ((c >> 8) & 0xFF);
} static inline u16 _componentGEN(ECXComponent c) {
    return ((c >> 0) & 0xFF);
}

// fnv1a
static inline u32 _hashv1(char* v) {
	if (v) { u32 o = 2166136261u;
        do { o ^= (char)*v++; o *= 16777619u; }
        while (*v); return o;
    } else return I32_MAX;
}

// TODO: components store a dense array of bound entity handles
// allowing bit-mask queries to be a simple filter before pushing
// the bound entity handles to the iterable -- systems then have O(1) lookups
// into any component (getFieldArray[entity]).
ECXEntity newEntity(none) {
    u32 id = 0;
    u8 freed = 0;
    if (r3_arr_count(&ECX.entity.free)) {
        if (!r3_arr_pop(&id, &ECX.entity.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newEntity` -- entity internal array pop failed\n");
            return 0;
        }
        freed = 1;
    } else { id = ++ECX.entity.next; }
    
    u16 gen = 0;
    if (!r3_arr_read(id - 1, &gen, &ECX.entity.gen)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newEntity` -- entity internal array read failed\n");
        if (freed) {
            if (!r3_arr_push(&id, &ECX.entity.free)) {
                r3_log_stdout(ERROR_LOG, "[ECX] Failed `newEntity` -- entity internal array push failed\n");
                return 0;
            }
        } else { --ECX.entity.next; }
        return 0;
    }
    
    ECX.entity.count++;
    return _packEntity(id, gen, 1234, 1);
}

u8 delEntity(ECXEntity entity) {
    u32 id = _entityID(entity);
    u16 gen = _entityGEN(entity);
    if (!id || id > ECX.entity.next || gen != ((u16*)ECX.entity.gen.data)[id - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `delEntity` -- invalid entity: (id)%d (gen)%d\n", id, gen);
        return 1;
    }
    
    gen++; // bump generation
    if (!r3_arr_push(&id, &ECX.entity.free)             ||
        !r3_arr_write(id - 1, &gen, &ECX.entity.gen)    ||
        !r3_arr_write(id - 1, &(u64){0}, &ECX.entity.mask)) {   // clear entity-component mask
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `delEntity` -- entity internal array push/write failed\n");
        return 0;
    }

    ECX.entity.count--;
    return 1;
}


ECXComponent newComponent(ECXComponentDesc comp) {
    // validate descriptor
    // support "tag" components with no fields by requiring field descs if a field count is passed
    if (!comp.max || comp.max > ECX_ENTITY_MAX  ||
        (comp.fields && !comp.fieldv)           ||
        !comp.mask) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- invalid component descriptor\n");
        return 0;
    }

    u8 id = 0;
    u8 freed = 0;
    if (r3_arr_count(&ECX.component.free)) {
        if (!r3_arr_pop(&id, &ECX.component.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array pop failed\n");
            return 0;
        } freed = 1;
    } else { id = ++ECX.component.next; }
    
    u8 result = 1;
    // allocate and assign field arrays from internal arena
    if (!r3_arr_assign(id - 1, r3_arena_alloc(sizeof(u32) * comp.fields, &ECX.arena), &ECX.component.fieldHash)   ||
        !r3_arr_assign(id - 1, r3_arena_alloc(sizeof(u16) * comp.fields, &ECX.arena), &ECX.component.fieldStride) ||
        !r3_arr_assign(id - 1, r3_arena_alloc(sizeof(u16) * comp.fields, &ECX.arena), &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array assignment failed\n");
        result = 0;
    }

    u32* fieldHashes;
    u16* fieldStrides;
    u16* fieldOffsets;
    if (!r3_arr_read(id - 1, &fieldHashes, &ECX.component.fieldHash)     ||
        !r3_arr_read(id - 1, &fieldStrides, &ECX.component.fieldStride)  ||
        !r3_arr_read(id - 1, &fieldOffsets, &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array read failed\n");
        result = 0;
    }
    
    if (!r3_arr_write(id - 1, &comp.mask, &ECX.component.mask)        ||
        !r3_arr_write(id - 1, &comp.hash, &ECX.component.hash)        ||
        !r3_arr_write(id - 1, &comp.max, &ECX.component.fieldMax)     ||
        !r3_arr_write(id - 1, &comp.fields, &ECX.component.fieldCount)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array write failed\n");
        result = 0;
    }
    
    u64 offset = 0;
    FOR(u8, field, 0, comp.fields, 1) {
        fieldHashes[field] = _hashv1(comp.fieldv[field].hash);
        fieldOffsets[field] = offset;
        fieldStrides[field] = comp.fieldv[field].stride;
        
        // accumulate field offsets
        offset += comp.fieldv[field].stride * comp.max;
    }
    
    // allocate and assign field buffer from internal arena -- or fallback from accumulated fails
    if (!r3_arr_write(id - 1, &offset, &ECX.component.fieldSize)                         ||
        !r3_arr_assign(id - 1, r3_arena_alloc(offset, &ECX.arena), &ECX.component.field) || !result) {
        // component fallback -- read out field data for dealloc and return handle
        r3_log_stdout(ERROR_LOG, "[ECX] `newComponent` fallback\n");
        ptr fieldHash = 0;
        ptr fieldStride = 0;
        ptr fieldOffset = 0;
        if (!(r3_arr_read(id - 1, &fieldHash, &ECX.component.fieldHash) && r3_arr_read(id - 1, &fieldStride, &ECX.component.fieldStride) && r3_arr_read(id - 1, &fieldOffset, &ECX.component.fieldOffset))   ||
            !r3_arena_dealloc(sizeof(u32) * comp.fields, fieldHash, &ECX.arena)         ||
            !r3_arena_dealloc(sizeof(u16) * comp.fields, fieldStride, &ECX.arena)       ||
            !r3_arena_dealloc(sizeof(u16) * comp.fields, fieldOffset, &ECX.arena)       ||
            !r3_arr_push(&id, &ECX.component.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` fallback -- component internal arena dealloc/array push failed\n");
        }
        return 0;
    }

    u8 gen = 0;
    if (!r3_arr_read(id - 1, &gen, &ECX.component.gen)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array read failed\n");
        if (freed) {
            if (!r3_arr_push(&id, &ECX.component.free)) {
                r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array push failed\n");
                return 0;
            }
        } else { --ECX.component.next; }
        return 0;
    }
    
    ECX.component.count++;
    return _packComponent(id, gen);
}

u8 delComponent(ECXComponent comp) {
    u8 id = _componentID(comp);
    u8 gen = _componentGEN(comp);
    if (!id || id > ECX.component.next || gen != ((u8*)ECX.component.gen.data)[id - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `delComponent` -- invalid component: (id)%d (gen)%d\n", id, gen);
        return 1;
    }
    
    // read out field data for dealloc
    u8 fieldCount = 0;
    u64 fieldSize = 0;
    ptr field = 0;
    ptr fieldHash = 0;
    ptr fieldStride = 0;
    ptr fieldOffset = 0;
    if (!(r3_arr_read(id - 1, &fieldSize, &ECX.component.fieldSize) && r3_arr_read(id - 1, &fieldCount, &ECX.component.fieldCount))
    ||  !(r3_arr_read(id - 1, &field, &ECX.component.field) && r3_arr_read(id - 1, &fieldHash, &ECX.component.fieldHash))
    ||  !(r3_arr_read(id - 1, &fieldStride, &ECX.component.fieldStride) && r3_arr_read(id - 1, &fieldOffset, &ECX.component.fieldOffset))
    ||  (!fieldSize && fieldCount) || (!field || !fieldHash || !fieldStride || !fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during `delComponent` -- component internal array read failed\n");
        return 0;
    }
    
    if (!r3_arena_dealloc(fieldSize, field, &ECX.arena)                         ||
        !r3_arr_write(id - 1, &(u8){0}, &ECX.component.mask)                    ||
        !r3_arr_write(id - 1, &(u8){0}, &ECX.component.hash)                    ||
        !r3_arr_write(id - 1, &(u8){0}, &ECX.component.fieldMax)                ||
        !r3_arr_write(id - 1, &(u8){0}, &ECX.component.fieldSize)               ||
        !r3_arr_write(id - 1, &(u8){0}, &ECX.component.fieldCount)              ||
        !r3_arena_dealloc(sizeof(u32) * fieldCount, fieldHash, &ECX.arena)      ||
        !r3_arena_dealloc(sizeof(u16) * fieldCount, fieldStride, &ECX.arena)    ||
        !r3_arena_dealloc(sizeof(u16) * fieldCount, fieldOffset, &ECX.arena)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during `delComponent` -- component internal array write failed\n");
        return 0;
    }

    // update now invalid configs
    FOR_I(0, ECX.config.count, 1) {
        u8* compSet = ((u8**)ECX.config.componentSet.data)[i];
        u8 compCount = ((u8*)ECX.config.componentCount.data)[i];
        u8 removed = 0;

        // swap-remove component
        FOR_J(0, compCount, 1) {
            if (compSet[j] == id) {
                compSet[j] = compSet[--compCount];
                ((u8*)ECX.config.componentCount.data)[i] = compCount;
                removed = 1;
                break;
            }
        }

        // update signature
        if (removed) {
            u64 newSig = 0;
            FOR_K(0, compCount, 1) {
                newSig ^= ((u64*)ECX.component.mask.data)[compSet[k] - 1];
            }
            ((u64*)ECX.config.signature.data)[i] = newSig;
        }
    }

    gen++; // bump generation
    if (!r3_arr_push(&id, &ECX.component.free) ||
        !r3_arr_write(id - 1, &gen, &ECX.component.gen)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during `delComponent` -- component internal array push failed\n");
        return 0;
    }

    ECX.component.count--;
    return 1;
}


ECXConfig query(ECXQueryDesc desc) {
    u64 all  = desc.all;
    u64 any  = desc.any;
    u64 none = desc.none;

    // Compute signature (used for reuse / caching)
    u64 signature = (all ^ (any << 21) ^ (none << 42));

    // probe exisiting queries
    for (u16 i = 0; i < ECX.query.count; ++i) {
        u64 qAll  = ((u64*)ECX.query.all.data)[i];
        u64 qAny  = ((u64*)ECX.query.any.data)[i];
        u64 qNone = ((u64*)ECX.query.none.data)[i];
        if (qAll == all && qAny == any && qNone == none) {
            // already exists → mark seen
            ((u32*)ECX.query.seen.data)[i]++;
            return ((u16*)ECX.query.config.data)[i];
        }
    }

    // alloc new query
    u16 qid = ++ECX.query.next;
    ++ECX.query.count;

    r3_arr_write(qid - 1, &all,  &ECX.query.all);
    r3_arr_write(qid - 1, &any,  &ECX.query.any);
    r3_arr_write(qid - 1, &none, &ECX.query.none);
    r3_arr_write(qid - 1, &(u32){1}, &ECX.query.seen);
    r3_arr_write(qid - 1, &(u8){0},  &ECX.query.cached);

    // alloc new config
    u16 cid = ++ECX.config.next;
    ++ECX.config.count;

    r3_arr_write(cid - 1, &signature, &ECX.config.signature); // all mask as the "signature" component set

    // alloc + compute entity set
    u32* entitySet = r3_mem_alloc(ECX.entity.count * sizeof(u32), 8);
    u32 entityCount = 0;

    u64* entityMasks = (u64*)ECX.entity.mask.data;
    FOR(u32, e, 0, ECX.entity.next, 1) {
        u64 mask = entityMasks[e];
        if (((mask & all) == all) && ((mask & none) == 0) && (!any || (mask & any))) {
            entitySet[entityCount++] = e + 1;   // ECX handles are index + 1
        }
    }

    r3_arr_assign(cid - 1, entitySet, &ECX.config.entitySet);
    r3_arr_write(cid - 1, &entityCount, &ECX.config.entityCount);

    // alloc + compute component set
    u8* componentSet = r3_mem_alloc(ECX.component.count * sizeof(u8), 8);
    u8 componentCount = 0;

    u64* compMasks = (u64*)ECX.component.mask.data;
    FOR(u8, c, 0, ECX.component.next, 1) {
        if (compMasks[c] & all) { // component participates in query
            componentSet[componentCount++] = c + 1; // ECX handles are index + 1
        }
    }

    r3_arr_assign(cid - 1, componentSet, &ECX.config.componentSet);
    r3_arr_write(cid - 1, &componentCount, &ECX.config.componentCount);

    // 'link' query -> config
    r3_arr_write(qid - 1, &cid, &ECX.query.config);
    ((u8*)ECX.query.cached.data)[qid - 1] = 1;

    // alloc per-query local/global maps
    u32* ltg = r3_mem_alloc(entityCount * sizeof(u32), 8);
    u32* gtl = r3_mem_alloc(entityCount * sizeof(u32), 8);
    for (u32 i = 0; i < entityCount; ++i) {
        ltg[i] = entitySet[i];
        gtl[entitySet[i]] = i;
    }
    
    r3_arr_assign(qid - 1, ltg, &ECX.query.localToGlobal);
    r3_arr_assign(qid - 1, gtl, &ECX.query.globalToLocal);

    return cid;
}

none iter(ECXConfig config, ECXSystem sys, ptr user) {
    if (!sys) return;

    u16 cid = config;
    if (!cid || cid > ECX.config.next) return;

    u32* entitySet = ((u32**)ECX.config.entitySet.data)[cid - 1];
    u32 entityCount = ((u32*)ECX.config.entityCount.data)[cid - 1];

    if (!entitySet || !entityCount) return;

    for (u32 i = 0; i < entityCount; ++i) {
        u32 eid = entitySet[i];
        u16 gen = ((u16*)ECX.entity.gen.data)[eid - 1];
        sys(_packEntity(eid, gen, 1234, 1), user);
    }
}


u8 bind(ECXEntity entity, ECXComponent comp) {
    u32 eid = _entityID(entity);
    u16 egen = _entityGEN(entity);
    if (!eid || eid > ECX.entity.next || egen != ((u16*)ECX.entity.gen.data)[eid - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `bind` -- invalid entity: (id)%d (gen)%d\n", eid, egen);
        return 0;
    }

    u8 cid = _componentID(comp);
    u8 cgen = _componentGEN(comp);
    if (!cid || cid > ECX.component.next || cgen != ((u8*)ECX.component.gen.data)[cid - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `bind` -- invalid component: (id)%d (gen)%d\n", cid, cgen);
        return 0;
    }

    u64* entityMask = ((u64*)ECX.entity.mask.data);
    u64 componentMask = ((u64*)ECX.component.mask.data)[cid - 1];
    if ((entityMask[eid - 1] & componentMask) == componentMask) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `bind` -- component not bound to entity: (id)%d (gen)%d\n", eid, egen);
        return 1;
    }

    // apply bind
    entityMask[eid - 1] |= componentMask;

    // incremental update into queries/configs
    for (u16 q = 0; q < ECX.query.count; ++q) {
        u64 all  = ((u64*)ECX.query.all.data)[q];
        u64 any  = ((u64*)ECX.query.any.data)[q];
        u64 none = ((u64*)ECX.query.none.data)[q];

        if (((entityMask[eid - 1] & all) == all) && ((entityMask[eid - 1] & none) == 0) && (!any || (entityMask[eid - 1] & any))) {
            ECXConfig cfg = ((u16*)ECX.query.config.data)[q];
            u32* entitySet = ((u32**)ECX.config.entitySet.data)[cfg - 1];
            u32* entityCount = &((u32*)ECX.config.entityCount.data)[cfg - 1];
            
            // skip if already in set
            u32* gtl = ((u32**)ECX.config.globalToLocal.data)[cfg - 1];
            if (gtl[eid] < *entityCount && entitySet[gtl[eid]] == eid)
                continue;

            // add to dense array (O(1) append)
            entitySet[*entityCount] = eid;
            ((u32**)ECX.config.localToGlobal.data)[cfg - 1][*entityCount] = eid;
            gtl[eid] = (*entityCount)++;
        }
    }

    return 1;
}

u8 unbind(ECXEntity entity, ECXComponent comp) {
    u32 eid = _entityID(entity);
    u16 egen = _entityGEN(entity);
    if (!eid || eid > ECX.entity.next || egen != ((u16*)ECX.entity.gen.data)[eid - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `unbind` -- invalid entity: (id)%d (gen)%d\n", eid, egen);
        return 0;
    }

    u8 cid = _componentID(comp);
    u8 cgen = _componentGEN(comp);
    if (!cid || cid > ECX.component.next || cgen != ((u8*)ECX.component.gen.data)[cid - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `unbind` -- invalid component: (id)%d (gen)%d\n", cid, cgen);
        return 0;
    }

    u64* entityMask = ((u64*)ECX.entity.mask.data);
    u64 componentMask = ((u64*)ECX.component.mask.data)[cid - 1];
    if ((entityMask[eid - 1] & componentMask) != componentMask) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `unbind` -- component not bound to entity: (id)%d (gen)%d\n", eid, egen);
        return 1;
    }

    // apply unbind
    entityMask[eid - 1] &= ~componentMask;

    // --- remove from any now-invalid configs ---
    for (u16 q = 0; q < ECX.query.count; ++q) {
        u64 all  = ((u64*)ECX.query.all.data)[q];
        u64 any  = ((u64*)ECX.query.any.data)[q];
        u64 none = ((u64*)ECX.query.none.data)[q];

        if (!((entityMask[eid - 1] & all) == all && ((entityMask[eid - 1] & none) == 0) && (!any || (entityMask[eid - 1] & any)))) {
            ECXConfig cfg = ((u16*)ECX.query.config.data)[q];
            u32* entitySet = ((u32**)ECX.config.entitySet.data)[cfg - 1];
            u32* entityCount = &((u32*)ECX.config.entityCount.data)[cfg - 1];
            
            u32* gtl = ((u32**)ECX.config.globalToLocal.data)[cfg - 1];
            u32* ltg = ((u32**)ECX.config.localToGlobal.data)[cfg - 1];

            u32 idx = gtl[eid];
            if (idx >= *entityCount) continue;

            // swap-remove O(1)
            u32 last = entitySet[--(*entityCount)];
            entitySet[idx] = last;
            ltg[idx] = last;
            gtl[last] = idx;
        }
    }

    return 1;
}


u8 setFieldArray(u8 field, u8 in, ECXComponent comp) {
    u8 componentId = _componentID(comp);
    u8 componentGen = _componentGEN(comp);
    if (!componentId || componentId > ECX.component.next               ||
        componentGen != ((u8*)ECX.component.gen.data)[componentId - 1] ||
        field >= ((u8*)ECX.component.fieldCount.data)[componentId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `setFieldArray` -- invalid component/field: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }
    
    ptr fields = ((ptr*)ECX.component.field.data)[componentId - 1];
    u32 fieldMax = ((u32*)ECX.component.fieldMax.data)[componentId - 1];
    u16* fieldStride = ((u16**)ECX.component.fieldStride.data)[componentId - 1];
    u16* fieldOffset = ((u16**)ECX.component.fieldOffset.data)[componentId - 1];

    ptr f = ((u8*)fields + fieldOffset[field]);
    if (!r3_mem_set(fieldStride[field] * fieldMax, in, f)) {
        r3_log_stdoutf(ERROR_LOG, "[ECX] Failed `setFieldArray` -- memory set from component field failed: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }
    
    return 1;
}

ptr getFieldArray(u8 field, ECXComponent comp) {
    u8 componentId = _componentID(comp);
    u8 componentGen = _componentGEN(comp);
    if (!componentId || componentId > ECX.component.next               ||
        componentGen != ((u8*)ECX.component.gen.data)[componentId - 1] ||
        field >= ((u8*)ECX.component.fieldCount.data)[componentId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `getFieldArray` -- invalid component/field: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }

    ptr fields = ((ptr*)ECX.component.field.data)[componentId - 1];
    u16* fieldOffset = ((u16**)ECX.component.fieldOffset.data)[componentId - 1];

    return (ptr)((u8*)fields + fieldOffset[field]);
}


u8 setField(u8 field, ptr in, ECXEntity entity, ECXComponent comp) {
    u32 entityId = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    if (!entityId || entityId > ECX.entity.next || entityGen != ((u16*)ECX.entity.gen.data)[entityId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `setField` -- invalid entity: (id)%d (gen)%d\n", entityId, entityGen);
        return 0;
    }

    u8 componentId = _componentID(comp);
    u8 componentGen = _componentGEN(comp);
    if (!componentId || componentId > ECX.component.next               ||
        componentGen != ((u8*)ECX.component.gen.data)[componentId - 1] ||
        field >= ((u8*)ECX.component.fieldCount.data)[componentId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `setField` -- invalid component/field: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }

    u64 entityMask = ((u64*)ECX.entity.mask.data)[entityId - 1];
    u64 componentMask = ((u64*)ECX.component.mask.data)[componentId - 1];
    if ((entityMask & componentMask) != componentMask) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `setField` -- component not bound to entity: (id)%d (gen)%d\n", entityId, entityGen);
        return 0;
    }
    
    ptr fields = ((ptr*)ECX.component.field.data)[componentId - 1];
    u16* fieldStride = ((u16**)ECX.component.fieldStride.data)[componentId - 1];
    u16* fieldOffset = ((u16**)ECX.component.fieldOffset.data)[componentId - 1];

    ptr f = (ptr)((u8*)fields + fieldOffset[field]);
    ptr value = (ptr)((u8*)f + (fieldStride[field] * (entityId - 1)));
    if (!r3_mem_write(fieldStride[field], in, value)) {
        r3_log_stdoutf(ERROR_LOG, "[ECX] Failed `setField` -- memory write to component field failed: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }
    
    return 1;
}

u8 getField(u8 field, ptr out, ECXEntity entity, ECXComponent comp) {
    u32 entityId = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    if (!entityId || entityId > ECX.entity.next || entityGen != ((u16*)ECX.entity.gen.data)[entityId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `getField` -- invalid entity: (id)%d (gen)%d\n", entityId, entityGen);
        return 0;
    }
    
    u8 componentId = _componentID(comp);
    u8 componentGen = _componentGEN(comp);
    if (!componentId || componentId > ECX.component.next               ||
        componentGen != ((u8*)ECX.component.gen.data)[componentId - 1] ||
        field >= ((u8*)ECX.component.fieldCount.data)[componentId - 1]) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `getField` -- invalid component/field: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }

    u64 entityMask = ((u64*)ECX.entity.mask.data)[entityId - 1];
    u64 componentMask = ((u64*)ECX.component.mask.data)[componentId - 1];
    if ((entityMask & componentMask) != componentMask) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `getField` -- component not bound to entity: (id)%d (gen)%d\n", entityId, entityGen);
        return 0;
    }
    
    ptr fields = ((ptr*)ECX.component.field.data)[componentId - 1];
    u16* fieldStride = ((u16**)ECX.component.fieldStride.data)[componentId - 1];
    u16* fieldOffset = ((u16**)ECX.component.fieldOffset.data)[componentId - 1];

    ptr f = ((u8*)fields + fieldOffset[field]);
    ptr value = ((u8*)f + (fieldStride[field] * (entityId - 1)));
    if (!r3_mem_read(fieldStride[field], out, value)) {
        r3_log_stdoutf(ERROR_LOG, "[ECX] Failed `getField` -- memory read from component field failed: (id)%d (gen)%d (field)%d\n", componentId, componentGen, field);
        return 0;
    }
    
    return 1;
}


u8 ECXInit(u32 entityMax) {
    if (ECX.init) {
        r3_log_stdout(WARN_LOG, "[ECX] Skipping redundant init\n");
        return 1;
    }
    
    ECX.init = 1;
    // alloc internal arena
    r3_arena_alloc(ECX_FIELD_DATA_MAX, &ECX.arena);
    if (!ECX.arena.buffer) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal arena alloc failed\n");
        return 0;
    }

    // init entity internal
    if (!entityMax || entityMax > ECX_ENTITY_MAX ||
        !r3_arr_alloc(entityMax, sizeof(u16), &ECX.entity.gen)  ||
        !r3_arr_alloc(entityMax, sizeof(u32), &ECX.entity.free) ||
        !r3_arr_alloc(entityMax, sizeof(u64), &ECX.entity.mask)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal entity array alloc failed\n");
        return ECXExit();
    }
    ECX.entity.next = 0;
    ECX.entity.count = 0;

    // init component internal
    if (!r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u8), &ECX.component.free)           ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u8),  &ECX.component.gen)           ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u64),  &ECX.component.mask)         ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32),  &ECX.component.hash)         ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(ptr),  &ECX.component.field)        ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32),  &ECX.component.fieldMax)     ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32*), &ECX.component.fieldHash)    ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u64),  &ECX.component.fieldSize)    ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u8),   &ECX.component.fieldCount)   ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u16*), &ECX.component.fieldStride)  ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u16*), &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal component array alloc failed\n");
        return ECXExit();
    }
    ECX.component.next = 0;
    ECX.component.count = 0;
    
    // init query internal
    if (!r3_arr_alloc(1, sizeof(u8), &ECX.query.free)             ||
        !r3_arr_alloc(1, sizeof(u64),  &ECX.query.all)            ||
        !r3_arr_alloc(1, sizeof(u64),  &ECX.query.any)            ||
        !r3_arr_alloc(1, sizeof(u64),  &ECX.query.none)           ||
        !r3_arr_alloc(1, sizeof(u32), &ECX.query.seen)            ||
        !r3_arr_alloc(1, sizeof(ECXConfig),  &ECX.query.config)   ||
        !r3_arr_alloc(1, sizeof(u8),   &ECX.query.cached)         ||
        !r3_arr_alloc(1, sizeof(u32*), &ECX.query.localToGlobal)  ||
        !r3_arr_alloc(1, sizeof(u32*), &ECX.query.globalToLocal)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal query array alloc failed\n");
        return ECXExit();
    }
    ECX.query.next = 0;
    ECX.query.count = 0;

    
    // init config internal
    if (!r3_arr_alloc(1, sizeof(u16), &ECX.config.free)                 ||
        !r3_arr_alloc(1, sizeof(u64),  &ECX.config.signature)           ||
        !r3_arr_alloc(1, sizeof(u32*), &ECX.config.entitySet)           ||
        !r3_arr_alloc(1, sizeof(u32),  &ECX.config.entityCount)         ||
        !r3_arr_alloc(1, sizeof(u8*), &ECX.config.componentSet)         ||
        !r3_arr_alloc(1, sizeof(u8),  &ECX.config.componentCount)       ||
        !r3_arr_alloc(1, sizeof(u32*), &ECX.config.localToGlobal)       ||
        !r3_arr_alloc(1, sizeof(u32*), &ECX.config.globalToLocal)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal query array alloc failed\n");
        return ECXExit();
    }
    ECX.config.next = 0;
    ECX.config.count = 0;

    r3_log_stdout(SUCCESS_LOG, "[ECX] Initialized\n");
    return 1;
}

u8 ECXExit(none) {
    if (!ECX.init) {
        r3_log_stdout(WARN_LOG, "[ECX] Skipping redundant exit\n");
        return 1;
    }
    
    ECX.init = 0;
    // dealloc internal arena
    if (!r3_arena_dealloc(0, 0, &ECX.arena)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal arena dealloc failed\n");
    }

    // exit entity internal
    if (!r3_arr_dealloc(&ECX.entity.gen)  ||
        !r3_arr_dealloc(&ECX.entity.free) ||
        !r3_arr_dealloc(&ECX.entity.mask)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal entity array dealloc failed\n");
    }
    ECX.entity.next = 0;
    ECX.entity.count = 0;

    // exit component internal
    if (!r3_arr_dealloc(&ECX.component.free)          ||
        !r3_arr_dealloc(&ECX.component.gen)           ||
        !r3_arr_dealloc(&ECX.component.mask)          ||
        !r3_arr_dealloc(&ECX.component.hash)          ||
        !r3_arr_dealloc(&ECX.component.field)         ||
        !r3_arr_dealloc(&ECX.component.fieldMax)      ||
        !r3_arr_dealloc(&ECX.component.fieldSize)     ||
        !r3_arr_dealloc(&ECX.component.fieldHash)     ||
        !r3_arr_dealloc(&ECX.component.fieldCount)    ||
        !r3_arr_dealloc(&ECX.component.fieldStride)   ||
        !r3_arr_dealloc(&ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal component array dealloc failed\n");
    }
    ECX.component.next = 0;
    ECX.component.count = 0;

    // exit query internal
    if (!r3_arr_dealloc(&ECX.query.free)           ||
        !r3_arr_dealloc(&ECX.query.all)            ||
        !r3_arr_dealloc(&ECX.query.any)            ||
        !r3_arr_dealloc(&ECX.query.none)           ||
        !r3_arr_dealloc(&ECX.query.seen)           ||
        !r3_arr_dealloc(&ECX.query.config)         ||
        !r3_arr_dealloc(&ECX.query.cached)         ||
        !r3_arr_dealloc(&ECX.query.localToGlobal)  ||
        !r3_arr_dealloc(&ECX.query.globalToLocal)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal query array dealloc failed\n");
        return ECXExit();
    }
    ECX.query.next = 0;
    ECX.query.count = 0;

    
    // exit config internal
    if (!r3_arr_dealloc(&ECX.config.free)           ||
        !r3_arr_dealloc(&ECX.config.signature)      ||
        !r3_arr_dealloc(&ECX.config.entitySet)      ||
        !r3_arr_dealloc(&ECX.config.entityCount)    ||
        !r3_arr_dealloc(&ECX.config.localToGlobal)  ||
        !r3_arr_dealloc(&ECX.config.globalToLocal)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal query array dealloc failed\n");
        return ECXExit();
    }
    ECX.config.next = 0;
    ECX.config.count = 0;

    r3_log_stdout(SUCCESS_LOG, "[ECX] Exited\n");
    return 1;
}
