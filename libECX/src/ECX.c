/*
    ECX Proof of concept using r3kit ds/arr API for better funtionality and saftey
    @zafflins - 9/3/2025
*/

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
        Array free;        // array of free entity handles
        Array mask;        // array of entity component masks
    } entity;

    // component internal arrays alloced statically
    struct {
        u8 next;                // array of next component handle
        u8 count;               // number of components
        Array free;             // array of free component handles

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

        Array seen;             // array of config component mask combinations
        Array entity;           // array of entity IDs offset by config     | [ position+color+velocity(0, 1, 2, 3, 4, 5) | position+color(3, 1, 5) ]
        Array component;        // array of component IDs offset by config  | [ position+color+velocity(0, 1, 2, 3, 4, 5) | position+color(3, 1, 5) ]
        Array configOffset;     // array of config offsets
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
}

ECXEntity newEntity(none) {
    ECXEntity e = 0;
    if (r3_arr_count(&ECX.entity.free)) {
        if (!r3_arr_pop(&e, &ECX.entity.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newEntity` -- entity internal array pop failed\n");
            return 0;
        }
        // keep ID -- bump generation -- new salt ~@zafflins 9/15/25
        e = _packEntity(_entityID(e), _entityGEN(e)+1, 1234, 1);
    } else {
        // increment next -- then assign (preserves zero as an invalid entity ID) ~@zafflins 9/15/25
        e = _packEntity((ECXEntity)++ECX.entity.next, 1, 1234, 1);
    }

    ECX.entity.count++;
    return e;
}

u8 delEntity(ECXEntity entity) {
    u32 id = _entityID(entity);
    if (!id || id > ECX.entity.next) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `delEntity` -- invalid entity: %d\n", id);
        return 1;
    } id--; // we preserve zero, so subtract one from id
    
    // points to scoped entity ~@zafflins 9/15/25
    if (!r3_arr_push(&entity, &ECX.entity.free) ||
        !r3_arr_write(id, &(u64){0}, &ECX.entity.mask)) {   // clear entity-component mask
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

    ECXComponent c = 0;
    u64 id = ECX.component.next;
    if (r3_arr_count(&ECX.component.free)) {
        if (!r3_arr_pop(&c, &ECX.component.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array pop failed\n");
            return 0;
        }
    } else { c = ++ECX.component.next; }
    
    u8 result = 1;
    // allocate and assign field arrays from internal arena
    if (!r3_arr_assign(id, r3_arena_alloc(sizeof(u32) * comp.fields, &ECX.arena), &ECX.component.fieldHash)   ||
        !r3_arr_assign(id, r3_arena_alloc(sizeof(u16) * comp.fields, &ECX.arena), &ECX.component.fieldStride) ||
        !r3_arr_assign(id, r3_arena_alloc(sizeof(u16) * comp.fields, &ECX.arena), &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array assignment failed\n");
        result = 0;
    }

    u32* fieldHashes;
    u16* fieldStrides;
    u16* fieldOffsets;
    if (!r3_arr_read(id, &fieldHashes, &ECX.component.fieldHash)     ||
        !r3_arr_read(id, &fieldStrides, &ECX.component.fieldStride)  ||
        !r3_arr_read(id, &fieldOffsets, &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array read failed\n");
        result = 0;
    }
    
    if (!r3_arr_write(id, &comp.mask, &ECX.component.mask)        ||
        !r3_arr_write(id, &comp.hash, &ECX.component.hash)        ||
        !r3_arr_write(id, &comp.max, &ECX.component.fieldMax)     ||
        !r3_arr_write(id, &comp.fields, &ECX.component.fieldCount)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` -- component internal array write failed\n");
        result = 0;
    }
    
    u64 offset = 0;
    FOR(u8, field, 0, comp.fields, 1) {
        // TODO: 32-bit fnv1a comp.fieldv[field].hash
        fieldHashes[field] = 42069;
        fieldOffsets[field] = offset;
        fieldStrides[field] = comp.fieldv[field].stride;
        
        // accumulate field offsets
        offset += comp.fieldv[field].stride * comp.max;
    }
    
    // allocate and assign field buffer from internal arena -- or fallback from accumulated fails
    if (!r3_arr_write(id, &offset, &ECX.component.fieldSize)                         ||
        !r3_arr_assign(id, r3_arena_alloc(offset, &ECX.arena), &ECX.component.field) || !result) {
        // component fallback -- read out field data for dealloc and return handle
        r3_log_stdout(ERROR_LOG, "[ECX] `newComponent` fallback\n");
        ptr fieldHash = 0;
        ptr fieldStride = 0;
        ptr fieldOffset = 0;
        if (!(r3_arr_read(id, &fieldHash, &ECX.component.fieldHash) && r3_arr_read(id, &fieldStride, &ECX.component.fieldStride) && r3_arr_read(id, &fieldOffset, &ECX.component.fieldOffset))   ||
            !r3_arena_dealloc(sizeof(u32) * comp.fields, fieldHash, &ECX.arena)         ||
            !r3_arena_dealloc(sizeof(u16) * comp.fields, fieldStride, &ECX.arena)       ||
            !r3_arena_dealloc(sizeof(u16) * comp.fields, fieldOffset, &ECX.arena)       ||
            !r3_arr_push(&c, &ECX.component.free)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Failed `newComponent` fallback -- component internal arena dealloc/array push failed\n");
        }
        return 0;
    }

    ECX.component.count++;
    return c;
}

u8 delComponent(ECXComponent comp) {
    if (!comp || comp > ECX.component.next) {
        r3_log_stdoutf(WARN_LOG, "[ECX] Skipping `delComponent` -- invalid component: %d\n", comp);
        return 1;
    }
    u64 id = comp - 1;  // we preserved zero as invalid, so subtract one
    
    // read out field data for dealloc
    u8 fieldCount = 0;
    u64 fieldSize = 0;
    ptr field = 0;
    ptr fieldHash = 0;
    ptr fieldStride = 0;
    ptr fieldOffset = 0;
    if (!(r3_arr_read(id, &fieldSize, &ECX.component.fieldSize) && r3_arr_read(id, &fieldCount, &ECX.component.fieldCount))
    ||  !(r3_arr_read(id, &field, &ECX.component.field) && r3_arr_read(id, &fieldHash, &ECX.component.fieldHash))
    ||  !(r3_arr_read(id, &fieldStride, &ECX.component.fieldStride) && r3_arr_read(id, &fieldOffset, &ECX.component.fieldOffset))
    ||  (!fieldSize && fieldCount) || (!field || !fieldHash || !fieldStride || !fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during `delComponent` -- component internal array read failed\n");
        return 0;
    }
    
    if (!r3_arena_dealloc(fieldSize, field, &ECX.arena)                         ||
        !r3_arr_write(id, &(u8){0}, &ECX.component.mask)                        ||
        !r3_arr_write(id, &(u8){0}, &ECX.component.hash)                        ||
        !r3_arr_write(id, &(u8){0}, &ECX.component.fieldMax)                    ||
        !r3_arr_write(id, &(u8){0}, &ECX.component.fieldSize)                   ||
        !r3_arr_write(id, &(u8){0}, &ECX.component.fieldCount)                  ||
        !r3_arena_dealloc(sizeof(u32) * fieldCount, fieldHash, &ECX.arena)      ||
        !r3_arena_dealloc(sizeof(u16) * fieldCount, fieldStride, &ECX.arena)    ||
        !r3_arena_dealloc(sizeof(u16) * fieldCount, fieldOffset, &ECX.arena)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during `delComponent` -- component internal array write failed\n");
        return 0;
    }

    ECX.component.count--;
    return 1;
}

ECXConfig query(ECXQueryDesc query) { return 0; }

u8 bind(ECXEntity ent, ECXComponent comp) { return 0; }
u8 unbind(ECXEntity ent, ECXComponent comp) { return 0; }

u8 setFieldArray(u8 field, ptr in, ECXComponent comp) { return 0; }
u8 getFieldArray(u8 field, ptr out, ECXComponent comp) { return 0; }

u8 setField(u8 field, ptr in, ECXEntity ent, ECXComponent comp) { return 0; }
u8 getField(u8 field, ptr out, ECXEntity ent, ECXComponent comp) { return 0; }

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
        !r3_arr_alloc(entityMax, sizeof(ECXEntity), &ECX.entity.free) ||
        !r3_arr_alloc(entityMax, sizeof(u64), &ECX.entity.mask)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal entity array alloc failed\n");
        return ECXExit();
    }
    ECX.entity.next = 0;
    ECX.entity.count = 0;

    // init component internal
    if (!r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(ECXComponent), &ECX.component.free) ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u64),  &ECX.component.mask)         ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32),  &ECX.component.hash)         ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(ptr),  &ECX.component.field)        ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32),  &ECX.component.fieldMax)     ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u64),  &ECX.component.fieldSize)    ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u8),   &ECX.component.fieldCount)   ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u32*), &ECX.component.fieldHash)    ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u16*), &ECX.component.fieldStride)  ||
        !r3_arr_alloc(ECX_COMPONENT_MAX, sizeof(u16*), &ECX.component.fieldOffset)) {
        r3_log_stdout(ERROR_LOG, "[ECX] Error during init -- internal component array alloc failed\n");
        return ECXExit();
    }
    ECX.component.next = 0;
    ECX.component.count = 0;

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
    if (!r3_arr_dealloc(&ECX.entity.free) ||
        !r3_arr_dealloc(&ECX.entity.mask)) {
            r3_log_stdout(ERROR_LOG, "[ECX] Error during exit -- internal entity array dealloc failed\n");
    }
    ECX.entity.next = 0;
    ECX.entity.count = 0;

    // exit component internal
    if (!r3_arr_dealloc(&ECX.component.free)          ||
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

    r3_log_stdout(SUCCESS_LOG, "[ECX] Exited\n");
    return 1;
}
