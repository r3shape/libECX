/*
    ECX Proof of concept using r3kit ds/arr API for better funtionality and saftey
    @zafflins - 9/3/2025
    
    ECX 1.2 refactor ( `ecxCompose()` 50% speed up )
    @zafflins - 10/30/2025

    Incremental config updates were silently broken -- decided to refacor the whole lib :)
    @zafflins - 11/7/2025
*/

#include <include/libR3/mem/alloc.h>
#include <include/libR3/io/log.h>
#include <include/libECX/ecx.h>
#include <math.h>

typedef struct ECXEntityStorage {
    u32 nextID;
    u32* deadID;
    u16* entityGen;    // number of times an entity was recycled
    u64* entityMask;   // u64 mask for tracking component bindings
} ECXEntityStorage;

typedef struct ECXComponentStorage {
    u8 nextID;
    u8* deadID;
    u8* componentGen;           // number of times a component was recycled
    u64* componentMask;         // unique u64 identifier mask (e.g (1 << 0))
    u32* componentSlots;        // number of a component's per-field slots
    u32* componentFields;       // number of a component's fields
    u32** componentMembers;     // all u32 entity id's bound to a component
    u32** componentMemberMTE;   // an array used to map a component member's u32 member id to its u32 entity id (arr[mid] = eid)
    u32** componentMemberETM;   // an array used to map a component member's u32 entity id to its u32 member id (arr[eid] = mid)
    ptr* componentFieldArray;   // array of component field arrays
    u32** componentFieldOffset; // an array of component per-field offsets into a component's field array
    u16** componentFieldStride; // an array of component per-field slot strides
} ECXComponentStorage;

typedef struct ECXQueryStorage {
    u16 nextID;
    u64*queryIn;                // component masks included by a query
    u64*queryEx;                // component masks excluded by a query
    u8**  queryCompMTE;         // an array used to map a query component member's u8 member id to its u32 component id (arr[mid] = eid)
    u8**  queryCompETM;         // an array used to map a query component member's u8 component id to its u32 member id (arr[eid] = mid)
    u32** entityMembers;        // all u32 entity id's matched with a query
    u32** queryEntityMTE;       // an array used to map a query component entity member's u32 member id to its u32 entity id (arr[mid] = eid)
    u32** queryEntityETM;       // an array used to map a query component entity member's u32 entity id to its u32 member id (arr[eid] = mid)
    u8** componentMembers;      // all u8 component id's matched with a query
    ptr* compositionFieldArray; // array of all component-view field arrays
} ECXQueryStorage;

static struct ECX {
    ECXComponentStorage cs;
    ECXEntityStorage es;
    ECXQueryStorage qs;
} ECX = {0};

#define ECX_SENTINNEL 0xFFFFFFFF

static u8 ECXRuntimeState = 0;
static u32 ECXRuntimeEntityMax = 0;
static u8  ECXRuntimeComponentMax = 0;

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
} static inline u8 _entityTOMB(ECXEntity e) {
    return ((e >> 0) & 0x1);
} static inline ECXComponent _packComponent(ECXComponent c, u8 g) {
    return ((c & 0xFF) << 8) | ((g & 0xFF) << 0);
} static inline u16 _componentID(ECXComponent c) {
    return ((c >> 8) & 0xFF);
} static inline u16 _componentGEN(ECXComponent c) {
    return ((c >> 0) & 0xFF);
}

static inline none DEBUGARRAY(char* n, ptr a) {
    R3ArrayHeader ah = {0}; r3ArrayHeader(&ah, a);
    r3LogStdOutF(R3_LOG_DEV, "DEBUG ARRAY (%s) -- %p (stride)%d (size)%llu (slots)%llu (count)%llu\n", n, a, ah.stride, ah.size, ah.slots, ah.count);
}

// fnv1a
static inline u32 _hashv1(char* v) {
	if (v) { u32 o = 2166136261u;
        do { o ^= (char)*v++; o *= 16777619u; }
        while (*v); return o;
    } else return I32_MAX;
}


ECXEntity ecxNewEntity(none) {
    ECXEntity entityID = 0;
    if (r3ArrayCount(ECX.es.deadID)) {
        r3PopArray(ECX.es.deadID, &entityID);
    } else entityID = ECX.es.nextID++;
    
    u16 entitySalt = 1234;
    return _packEntity(entityID, ECX.es.entityGen[entityID], entitySalt, 1);
}

// TODO: update query + component member arrays on delete
u8 ecxDelEntity(ECXEntity entity) {
    u32 entityID = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    u16 entityTomb = _entityTOMB(entity);
    if (!entity || !entityTomb || entityID > ECX.es.nextID || entityGen != ECX.es.entityGen[entityID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `DelEntity` -- invalid `ECXEntity` passed: %llu\n", entity);
        return R3_RESULT_ERROR;
    }

    // clear mask, set new gen, and push dead id
    ECX.es.entityMask[entityID] = 0;
    ECX.es.entityGen[entityID] = ++entityGen;    // bump entity generation on delete
    if (!r3PushArray(&entityID, ECX.es.deadID)) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `DelEntity` -- internal entity storage array ops failed for entity: %llu\n", entity);
        return R3_RESULT_ERROR;
    }

    return R3_RESULT_SUCCESS;
}


ECXComponent ecxNewComponent(ECXComponentDesc component) {
    ECXComponent componentID = 0;
    if (r3ArrayCount(ECX.cs.deadID)) {
        r3PopArray(ECX.cs.deadID, &componentID);
    } else componentID = ECX.cs.nextID++;

    // cap component member arrays at component slots (component field arrays are static)
    ECX.cs.componentMembers[componentID] = r3NewArray(component.slots, sizeof(u32));    // at 15M slots this array is 60,000,000 bytes
    ECX.cs.componentMemberMTE[componentID] = r3NewArray(component.slots, sizeof(u32));
    ECX.cs.componentMemberETM[componentID] = r3NewArray(component.slots, sizeof(u32));
    // 180,000,000 bytes across all 3 or 180 MB

    if (!ECX.cs.componentMembers[componentID]   ||
        !ECX.cs.componentMemberMTE[componentID] ||
        !ECX.cs.componentMemberETM[componentID]) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `NewComponent` -- internal component storage array ops failed for component: %d\n", componentID);
        return R3_RESULT_ERROR;
    }
    
    ECX.cs.componentFieldOffset[componentID] = r3NewArray(component.fields, sizeof(u32));
    ECX.cs.componentFieldStride[componentID] = r3NewArray(component.fields, sizeof(u16));
    if (!ECX.cs.componentFieldOffset[componentID] || !ECX.cs.componentFieldStride[componentID]) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `NewComponent` -- internal component storage array ops failed for component: %d\n", componentID);
        r3DelArray(ECX.cs.componentMemberETM[componentID]);
        r3DelArray(ECX.cs.componentMemberMTE[componentID]);
        r3DelArray(ECX.cs.componentMembers[componentID]);
        ECX.cs.componentMemberETM[componentID] = NULL;
        ECX.cs.componentMemberMTE[componentID] = NULL;
        ECX.cs.componentMembers[componentID] = NULL;
        return R3_RESULT_ERROR;
    }

    // compute total field array size and store field offsets
    u64 fieldArraySize = 0;
    FOR_I(0, component.fields, 1) {
        u16 fieldStride = component.fieldv[i].stride;
        ECX.cs.componentFieldOffset[componentID][i] = fieldArraySize;
        ECX.cs.componentFieldStride[componentID][i] = fieldStride;
        fieldArraySize += (fieldStride * component.slots);
    }

    // alloc component field array for all fields
    ECX.cs.componentFieldArray[componentID] = r3AllocMemory(fieldArraySize);
    if (!ECX.cs.componentFieldArray[componentID]) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `NewComponent` -- internal component storage array ops failed for component: %d\n", componentID);
        r3DelArray(ECX.cs.componentFieldStride[componentID]);
        r3DelArray(ECX.cs.componentFieldOffset[componentID]);
        r3DelArray(ECX.cs.componentMemberETM[componentID]);
        r3DelArray(ECX.cs.componentMemberMTE[componentID]);
        r3DelArray(ECX.cs.componentMembers[componentID]);
        ECX.cs.componentFieldStride[componentID] = NULL;
        ECX.cs.componentFieldOffset[componentID] = NULL;
        ECX.cs.componentMemberETM[componentID] = NULL;
        ECX.cs.componentMemberMTE[componentID] = NULL;
        ECX.cs.componentMembers[componentID] = NULL;
        return R3_RESULT_ERROR;
    } // r3LogStdOutF(R3_LOG_DEV, "COMPONENT FIELD ARRAY CREATED -- %p (fields)%d (size)%llu\n", ECX.cs.componentFieldArray[componentID], component.fields, fieldArraySize);

    ECX.cs.componentFields[componentID] = component.fields;
    ECX.cs.componentSlots[componentID] = component.slots;
    ECX.cs.componentMask[componentID] = component.mask;

    return _packComponent(componentID, ECX.cs.componentGen[componentID]);
}

// TODO: update query member arrays on delete
u8 ecxDelComponent(ECXComponent component) {
    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `DelComponent` -- invalid `ECXComponent` passed: %llu\n", component);
        return R3_RESULT_ERROR;
    } r3LogStdOutF(R3_LOG_INFO, "COMPONENT ID %d COMPONENT GEN %d (DELETE)\n", componentID, componentGen);

    // clear mask, fields, slots, set new gen, and push dead id
    ECX.cs.componentMask[componentID] = 0;
    ECX.cs.componentSlots[componentID] = 0;
    ECX.cs.componentFields[componentID] = 0;
    ECX.cs.componentGen[componentID] = ++componentGen;    // bump component generation on delete
    if (!r3PushArray(&componentID, ECX.cs.deadID)
    || !r3FreeMemory(ECX.cs.componentFieldArray[componentID])
    || !r3DelArray(ECX.cs.componentFieldStride[componentID])
    || !r3DelArray(ECX.cs.componentFieldOffset[componentID])
    || !r3DelArray(ECX.cs.componentMemberETM[componentID])
    || !r3DelArray(ECX.cs.componentMemberMTE[componentID])
    || !r3DelArray(ECX.cs.componentMembers[componentID])) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `DelComponent` -- internal component storage array ops failed for component: %llu\n", component);
        return R3_RESULT_ERROR;
    }

    ECX.cs.componentFieldStride[componentID] = NULL;
    ECX.cs.componentFieldOffset[componentID] = NULL;
    ECX.cs.componentFieldArray[componentID] = NULL;
    ECX.cs.componentMemberETM[componentID] = NULL;
    ECX.cs.componentMemberMTE[componentID] = NULL;
    ECX.cs.componentMembers[componentID] = NULL;
    
    return R3_RESULT_SUCCESS;
}


u8 ecxSetFieldArray(u8 field, u8 in, ECXComponent component) {
    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID] || field >= ECX.cs.componentFields[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetFieldArray` -- invalid `ECXComponent`/field passed: (component)%llu (field)%d\n", component, field);
        return R3_RESULT_ERROR;
    }

    u32 fieldSlots = ECX.cs.componentSlots[componentID];
    ptr fieldArray = ECX.cs.componentFieldArray[componentID];
    u16* fieldStrides = ECX.cs.componentFieldStride[componentID];
    u32* fieldOffsets = ECX.cs.componentFieldOffset[componentID];

    ptr f = (ptr)((u8*)fieldArray + fieldOffsets[field]);
    if (!r3SetMemory(fieldStrides[field] * fieldSlots, in, f)) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetFieldArray` -- internal component field array memory set failed: %p\n", f);
        return R3_RESULT_ERROR;
    }

    return R3_RESULT_SUCCESS;
}

ptr ecxGetFieldArray(u8 field, ECXComponent component) {
    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID] || field >= ECX.cs.componentFields[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `GetFieldArray` -- invalid `ECXComponent`/field passed: (component)%llu (field)%d\n", component, field);
        return NULL;
    }

    ptr fieldArray = ECX.cs.componentFieldArray[componentID];
    u32* fieldOffsets = ECX.cs.componentFieldOffset[componentID];

    return (ptr)((u8*)fieldArray + fieldOffsets[field]);
}


u8 ecxGetField(u8 field, ptr out, ECXEntity entity, ECXComponent component) {
    u32 entityID = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    u16 entityTomb = _entityTOMB(entity);
    if (!entity || !entityTomb || entityID > ECX.es.nextID || entityGen != ECX.es.entityGen[entityID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `GetField` -- invalid `ECXEntity` passed: %llu, %d, %d\n", entity, entityGen, ECX.es.entityGen[entityID]);
        return R3_RESULT_ERROR;
    }

    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID] || field >= ECX.cs.componentFields[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `GetField` -- invalid `ECXComponent`/field passed: (component)%llu (field)%d\n", component, field);
        return R3_RESULT_ERROR;
    }

    if ((ECX.es.entityMask[entityID] & ECX.cs.componentMask[componentID]) != ECX.cs.componentMask[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `GetField` -- entity not bound to component: %d -> %d\n", entityID, componentID);
        return R3_RESULT_SUCCESS;
    }

    ptr fieldArray = ECX.cs.componentFieldArray[componentID];
    u16* fieldStrides = ECX.cs.componentFieldStride[componentID];
    u32* fieldOffsets = ECX.cs.componentFieldOffset[componentID];

    ptr f = (ptr)((u8*)fieldArray + fieldOffsets[field]);
    ptr slot = (ptr)((u8*)f + (fieldStrides[field] * entityID));
    if (!r3WriteMemory(fieldStrides[field], slot, out)) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `GetField` -- internal component field array memory read failed: (slot)%p\n", slot);
        return R3_RESULT_SUCCESS;
    }

    return R3_RESULT_SUCCESS;
}

u8 ecxSetField(u8 field, ptr in, ECXEntity entity, ECXComponent component) {
    u32 entityID = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    u16 entityTomb = _entityTOMB(entity);
    if (!entity || !entityTomb || entityID > ECX.es.nextID || entityGen != ECX.es.entityGen[entityID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetField` -- invalid `ECXEntity` passed: %llu, %d, %d\n", entity, entityGen, ECX.es.entityGen[entityID]);
        return R3_RESULT_ERROR;
    }

    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID] || field >= ECX.cs.componentFields[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetField` -- invalid `ECXComponent`/field passed: (component)%llu (field)%d\n", component, field);
        return R3_RESULT_ERROR;
    }

    if ((ECX.es.entityMask[entityID] & ECX.cs.componentMask[componentID]) != ECX.cs.componentMask[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetField` -- entity not bound to component: %d -> %d\n", entityID, componentID);
        return R3_RESULT_SUCCESS;
    }

    ptr fieldArray = ECX.cs.componentFieldArray[componentID];
    u16* fieldStrides = ECX.cs.componentFieldStride[componentID];
    u32* fieldOffsets = ECX.cs.componentFieldOffset[componentID];

    ptr f = (ptr)((u8*)fieldArray + fieldOffsets[field]);
    ptr slot = (ptr)((u8*)f + (fieldStrides[field] * entityID));
    if (!r3WriteMemory(fieldStrides[field], in, slot)) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `SetField` -- internal component field array memory write failed: (slot)%p\n", slot);
        return R3_RESULT_SUCCESS;
    }

    return R3_RESULT_SUCCESS;
}


u8 ecxBind(ECXEntity entity, ECXComponent component) {
    u32 entityID = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    u16 entityTomb = _entityTOMB(entity);
    if (!entity || !entityTomb || entityID > ECX.es.nextID || entityGen != ECX.es.entityGen[entityID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- invalid `ECXEntity` passed: %llu, %d, %d\n", entity, entityGen, ECX.es.entityGen[entityID]);
        return R3_RESULT_ERROR;
    }

    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- invalid `ECXComponent` passed: %llu\n", component);
        return R3_RESULT_ERROR;
    }

    u64 entityMask = ECX.es.entityMask[entityID];
    u64 componentMask = ECX.cs.componentMask[componentID];
    if ((entityMask & componentMask) == componentMask) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- entity already bound to component: %d -> %d\n", entityID, componentID);
        return R3_RESULT_SUCCESS;
    }

    // apply bind -> update component members
    ECX.es.entityMask[entityID] |= componentMask;
    u32 memberID = r3ArrayCount(ECX.cs.componentMembers[componentID]);
    if (memberID != ECX_SENTINNEL && memberID < r3ArraySlots(ECX.cs.componentMembers[componentID]) && !r3SetArray(memberID, &entityID, ECX.cs.componentMembers[componentID])) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- internal component storage ops failed (component entity mappings possibly corrupt): %d -> %d (member id)%d (slots)%llu\n", entityID, componentID, memberID, r3ArraySlots(ECX.cs.componentMembers[componentID]));
        return R3_RESULT_ERROR;
    }
    ECX.cs.componentMemberETM[componentID][entityID] = memberID;
    ECX.cs.componentMemberMTE[componentID][memberID] = entityID;

    // update relevant queries
    entityMask = ECX.es.entityMask[entityID];
    FOR(u16, q, 0, ECX.qs.nextID, 1) {
        u64 queryIn = ECX.qs.queryIn[q];
        u64 queryEx = ECX.qs.queryEx[q];
        if ((((entityMask & queryIn) == queryIn) && !(entityMask & queryEx))
        &&  ((componentMask & queryIn) && !(componentMask & queryEx))) {
            u32 eMemberID = (u32)r3ArrayCount(ECX.qs.entityMembers[q]);
            if (eMemberID == ECX_SENTINNEL || r3InArray(&entityID, ECX.qs.entityMembers[q])) {
                r3LogStdOutF(R3_LOG_OK, "ENTITY %d DENIED QUERY %d MEMBERSHIP -- (EMID)%d\n", entityID, q, eMemberID);
                continue;
            }

            u64 slots = r3ArraySlots(ECX.qs.entityMembers[q]);
            if (eMemberID < slots) { // r3LogStdOutF(R3_LOG_DEV, "ENTITY %d WANTS MEMBERSHIP IN QUERY %d -- (EMID)%d < (slots)%llu\n", entityID, q, eMemberID, slots);
                if (!r3SetArray(eMemberID, &entityID, ECX.qs.entityMembers[q])) {
                    r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- internal query storage ops failed (query entity mappings possibly corrupt): %d -> %d\n", entityID, q);
                    return R3_RESULT_ERROR;
                }
            } else { // r3LogStdOutF(R3_LOG_DEV, "ENTITY %d WANTS MEMBERSHIP IN QUERY %d -- (EMID)%d >= (slots)%llu [TRIGGERED RESIZE]\n", entityID, q, eMemberID, slots);
                u64 nslots = slots + 200;
                ECX.qs.entityMembers[q] = r3ResizeArray(nslots, r3ArrayStride(ECX.qs.entityMembers[q]), ECX.qs.entityMembers[q]);
                ECX.qs.queryEntityMTE[q] = r3ResizeArray(nslots, r3ArrayStride(ECX.qs.queryEntityMTE[q]), ECX.qs.queryEntityMTE[q]);
                if (!ECX.qs.queryEntityMTE[q] || !ECX.qs.entityMembers[q] || !r3SetArray(eMemberID, &entityID, ECX.qs.entityMembers[q])) {
                    r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Bind` -- internal query storage ops failed (query entity mappings possibly corrupt): %d -> %d\n", entityID, q);
                    return R3_RESULT_ERROR;
                }
            } // r3LogStdOutF(R3_LOG_OK, "[BIND] ENTITY %d MMAPPED TO QUERY %d | (EMID)%d (MTE)%d (ETM)%d\n", entityID, q, eMemberID, ECX.qs.queryEntityMTE[q][eMemberID], ECX.qs.queryEntityETM[q][entityID]);
            // update mappings once membership is validated
            ECX.qs.queryEntityMTE[q][eMemberID] = entityID;
            ECX.qs.queryEntityETM[q][entityID] = eMemberID;
        }
    }
    return R3_RESULT_SUCCESS;
}

u8 ecxUnbind(ECXEntity entity, ECXComponent component) {
    u32 entityID = _entityID(entity);
    u16 entityGen = _entityGEN(entity);
    u16 entityTomb = _entityTOMB(entity);
    if (!entity || !entityTomb || entityID > ECX.es.nextID || entityGen != ECX.es.entityGen[entityID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Unbind` -- invalid `ECXEntity` passed: %llu, %d, %d\n", entity, entityGen, ECX.es.entityGen[entityID]);
        return R3_RESULT_ERROR;
    }

    u8 componentID = _componentID(component);
    u8 componentGen = _componentGEN(component);
    if (componentID > ECX.cs.nextID || componentGen != ECX.cs.componentGen[componentID]) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Unbind` -- invalid `ECXComponent` passed: %llu\n", component);
        return R3_RESULT_ERROR;
    }

    u64 entityMask = ECX.es.entityMask[entityID];
    u64 componentMask = ECX.cs.componentMask[componentID];
    if ((entityMask & componentMask) != componentMask) {
        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Unbind` -- entity not bound to component: %d -> %d\n", entityID, componentID);
        return R3_RESULT_SUCCESS;
    }
    
    R3ArrayHeader h = {0};
    r3ArrayHeader(&h, ECX.cs.componentMembers[componentID]);

    // apply unbind -> update component members
    ECX.es.entityMask[entityID] &= ~componentMask;   // apply unbind
    u32 memberID = ECX.cs.componentMemberETM[componentID][entityID];
    if (memberID != ECX_SENTINNEL && memberID < r3ArraySlots(ECX.cs.componentMembers[componentID]) && ECX.cs.componentMemberMTE[componentID][memberID] == entityID) {
        u32 current = ECX.cs.componentMembers[componentID][memberID];
        u32 last = ECX.cs.componentMembers[componentID][h.count - 1];
        
        // swap member mappings
        ECX.cs.componentMemberETM[componentID][current] = ECX_SENTINNEL;  // sentinnel value
        ECX.cs.componentMemberMTE[componentID][memberID] = last;
        ECX.cs.componentMemberETM[componentID][last] = memberID;
    
        // swap members + pop
        u32 temp = 0;
        ECX.cs.componentMembers[componentID][memberID] = ECX.cs.componentMembers[componentID][h.count - 1];
        if (!r3PopArray(ECX.cs.componentMembers[componentID], &temp)) {
            r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Unbind` -- internal component storage ops failed (component member array possibly corrupt): %d -> %d\n", entityID, componentID);
            return R3_RESULT_ERROR;
        }

        // update relevant queries
        entityMask = ECX.es.entityMask[entityID];
        FOR(u16, q, 0, ECX.qs.nextID, 1) {
            u64 queryIn = ECX.qs.queryIn[q];
            u64 queryEx = ECX.qs.queryEx[q];
            if ((((entityMask & queryIn) == queryIn) && !(entityMask & queryEx))
            &&  ((componentMask & queryIn) && !(componentMask & queryEx))) {
                R3ArrayHeader qh = {0};
                r3ArrayHeader(&qh, ECX.qs.entityMembers[q]);
                u32 eMemberID = ECX.qs.queryEntityETM[q][entityID];
                if (eMemberID != ECX_SENTINNEL && ECX.qs.queryEntityMTE[q][eMemberID] == entityID) {
                    current = ECX.qs.entityMembers[q][eMemberID];
                    last = ECX.qs.entityMembers[q][qh.count - 1];

                    // swap member mappings
                    ECX.qs.queryEntityETM[q][current] = ECX_SENTINNEL;    // sentinnel value
                    ECX.qs.queryEntityMTE[q][eMemberID] = last;
                    ECX.qs.queryEntityETM[q][last] = eMemberID;

                    // swap members + pop
                    temp = 0;
                    ECX.qs.entityMembers[q][eMemberID] = ECX.qs.entityMembers[q][qh.count - 1];
                    if (!r3PopArray(ECX.qs.entityMembers[q], &temp)) {
                        r3LogStdOutF(R3_LOG_WARN, "[ECX] Warn while `Unbind` -- internal query storage ops failed (query member array possibly corrupt): %d -> %d\n", entityID, q);
                        return R3_RESULT_ERROR;
                    }
                }
            }
        }
    }
    return R3_RESULT_SUCCESS;
}


ECXQuery ecxQuery(ECXQueryDesc query) {
    if (!query.in) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Failed `Query` -- invalid query descriptor passed (all queries must have at least a single `all` mask)\n");
        return I16_MAX;
    } FOR(ECXQuery, q, 0, ECX.qs.nextID, 1) if (query.in == ECX.qs.queryIn[q] && query.ex == ECX.qs.queryEx[q]) return q;

    ECXQuery queryID = ECX.qs.nextID++;
    
    // compute query component members
    u8 componentMembers = 0;
    FOR(u8, c, 0, ECX.cs.nextID, 1) {
        if ((ECX.cs.componentMask[c] & query.in)
        && !(ECX.cs.componentMask[c] & query.ex)) {
            componentMembers++;
        }
    } if (!componentMembers) {
        r3LogStdOut(R3_LOG_WARN, "[ECX] Failed `Query` -- no components to match query\n");
        return I16_MAX;
    }
    

    ECX.qs.queryCompMTE[queryID] = r3NewArray(componentMembers, sizeof(u8*));
    ECX.qs.queryCompETM[queryID] = r3NewArray(componentMembers, sizeof(u8*));
    if (!ECX.qs.queryCompMTE || !ECX.qs.queryCompETM) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `componentMembers` storage array ops failed\n");
        ECX.qs.nextID--;
        return I16_MAX;
    }

    // alloc per-query component member array to store u8 component IDs
    ECX.qs.componentMembers[queryID] = r3NewArray(componentMembers, sizeof(u8));
    if (!ECX.qs.componentMembers[queryID]) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `componentMembers` storage array ops failed\n");
        r3DelArray(ECX.qs.queryCompETM[queryID]);
        r3DelArray(ECX.qs.queryCompMTE[queryID]);
        ECX.qs.queryCompETM[queryID] = NULL;
        ECX.qs.queryCompMTE[queryID] = NULL;
        ECX.qs.nextID--;
        return I16_MAX;
    }

    // compute component member seed based on smallest component member array
    u8 cMemberID = 0;
    u8 cMemberSeed = 0;
    u64 cMemberSeedMembers = ECX_ENTITY_MAX;    // start conditional at max
    FOR(u8, c, 0, ECX.cs.nextID, 1) {
        if ((ECX.cs.componentMask[c] & query.in)
        && !(ECX.cs.componentMask[c] & query.ex)) {
            R3ArrayHeader ch = {0};
            r3ArrayHeader(&ch, ECX.cs.componentMembers[c]);
            if (!r3SetArray(cMemberID, &c, ECX.qs.componentMembers[queryID])) {
                r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `componentMembers` storage array ops failed\n");
                r3DelArray(ECX.qs.queryCompETM[queryID]);
                r3DelArray(ECX.qs.queryCompMTE[queryID]);
                ECX.qs.queryCompETM[queryID] = NULL;
                ECX.qs.queryCompMTE[queryID] = NULL;
                ECX.qs.nextID--;
                return I16_MAX;
            }

            ECX.qs.queryCompMTE[queryID][cMemberID] = c;
            ECX.qs.queryCompETM[queryID][c] = cMemberID;
            if (ch.count < cMemberSeedMembers) {
                cMemberSeedMembers = ch.count;
                cMemberSeed = c;
            } cMemberID++;
        }
    } // r3LogStdOutF(R3_LOG_INFO, "CHECKED %d SEED %d MEMBERS %d\n", ECX.cs.nextID, cMemberSeed, cMemberSeedMembers);

    // compute query entity members (from query component member seed)
    u32 entityMembers = 0;
    u32* seedMembers = ECX.cs.componentMembers[cMemberSeed];
    
    R3ArrayHeader sh = {0};
    r3ArrayHeader(&sh, seedMembers);
    FOR(u32, e, 0, sh.count, 1) if (((ECX.es.entityMask[seedMembers[e]] & query.in) == query.in) && !(ECX.es.entityMask[seedMembers[e]] & query.ex)) entityMembers++;
    if (!entityMembers) {
        r3LogStdOut(R3_LOG_WARN, "[ECX] Failed `Query` -- no entities bound to query components\n");
        r3DelArray(ECX.qs.componentMembers[queryID]);
        r3DelArray(ECX.qs.queryCompETM[queryID]);
        r3DelArray(ECX.qs.queryCompMTE[queryID]);
        ECX.qs.componentMembers[queryID] = NULL;
        ECX.qs.queryCompETM[queryID] = NULL;
        ECX.qs.queryCompMTE[queryID] = NULL;
        ECX.qs.nextID--;
        return I16_MAX;
    }

    ECX.qs.queryEntityMTE[queryID] = r3NewArray(entityMembers, sizeof(u32*));
    ECX.qs.queryEntityETM[queryID] = r3NewArray(ECXRuntimeEntityMax, sizeof(u32*));
    if (!ECX.qs.queryEntityMTE || !ECX.qs.queryEntityETM) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `componentMembers` storage array ops failed\n");
        r3DelArray(ECX.qs.componentMembers[queryID]);
        r3DelArray(ECX.qs.queryCompETM[queryID]);
        r3DelArray(ECX.qs.queryCompMTE[queryID]);
        ECX.qs.componentMembers[queryID] = NULL;
        ECX.qs.queryCompETM[queryID] = NULL;
        ECX.qs.queryCompMTE[queryID] = NULL;
        ECX.qs.nextID--;
        return I16_MAX;
    }

    // alloc per-query entity member array and store u32 entity IDs
    ECX.qs.entityMembers[queryID] = r3NewArray(entityMembers, sizeof(u32));
    if (!ECX.qs.entityMembers[queryID]) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `entityMembers` storage array ops failed\n");
        r3DelArray(ECX.qs.componentMembers[queryID]);
        r3DelArray(ECX.qs.queryEntityETM[queryID]);
        r3DelArray(ECX.qs.queryEntityMTE[queryID]);
        r3DelArray(ECX.qs.queryCompETM[queryID]);
        r3DelArray(ECX.qs.queryCompMTE[queryID]);
        ECX.qs.componentMembers[queryID] = NULL;
        ECX.qs.queryEntityETM[queryID] = NULL;
        ECX.qs.queryEntityMTE[queryID] = NULL;
        ECX.qs.queryCompETM[queryID] = NULL;
        ECX.qs.queryCompMTE[queryID] = NULL;
        ECX.qs.nextID--;
        return I16_MAX;
    }

    u32 eMemberID = 0;
    FOR(u32, e, 0, sh.count, 1) {
        if (!(ECX.es.entityMask[seedMembers[e]] & query.ex)
        &&  ((ECX.es.entityMask[seedMembers[e]] & query.in) == query.in)) {
            if (!r3SetArray(eMemberID, &seedMembers[e], ECX.qs.entityMembers[queryID])) {
                r3LogStdOut(R3_LOG_ERROR, "[ECX] Warn while `Query` -- internal query `entityMembers` storage array ops failed\n");
                r3DelArray(ECX.qs.componentMembers[queryID]);
                r3DelArray(ECX.qs.queryEntityETM[queryID]);
                r3DelArray(ECX.qs.queryEntityMTE[queryID]);
                r3DelArray(ECX.qs.queryCompETM[queryID]);
                r3DelArray(ECX.qs.queryCompMTE[queryID]);
                ECX.qs.componentMembers[queryID] = NULL;
                ECX.qs.queryEntityETM[queryID] = NULL;
                ECX.qs.queryEntityMTE[queryID] = NULL;
                ECX.qs.queryCompETM[queryID] = NULL;
                ECX.qs.queryCompMTE[queryID] = NULL;
                ECX.qs.nextID--;
                return I16_MAX;
            } // r3LogStdOutF(R3_LOG_OK, "[QUERY] ENTITY %d MMAPPED TO QUERY %d | (EMID)%d (MTE)%d (ETM)%d\n", seedMembers[e], queryID, eMemberID, ECX.qs.queryEntityMTE[queryID][eMemberID], ECX.qs.queryEntityETM[queryID][seedMembers[e]]);
            ECX.qs.queryEntityMTE[queryID][eMemberID] = seedMembers[e];
            ECX.qs.queryEntityETM[queryID][seedMembers[e]] = eMemberID;
            eMemberID++;
        }
    }
    
    ECX.qs.queryIn[queryID] = query.in;
    ECX.qs.queryEx[queryID] = query.ex;

    return queryID;
}


u8 ecxCompose(ECXComposition* composition, ECXQuery query) {
    if (!composition) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `Compose` -- invalid `ECXComposition` passed: %p\n", composition);
        return R3_RESULT_ERROR;
    } if (query > ECX.qs.nextID) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `Compose` -- invalid `ECXQuery` passed: %d\n", query);
        return R3_RESULT_ERROR;
    }

    R3ArrayHeader h = {0};
    r3ArrayHeader(&h, ECX.qs.componentMembers[query]);
    
    // compute total number of component member fields + store per-component view field count
    u32 fieldTotal = 0;
    FOR(u8, c, 0, h.slots, 1) {
        u8 componentID = ECX.qs.componentMembers[query][c];
        u32 componentFields = ECX.cs.componentFields[componentID];
        fieldTotal += componentFields;
    } // r3LogStdOutF(R3_LOG_INFO, "COMPOSITION %d COMPONENTS %d FIELDS\n", h.slots, fieldTotal);

    // alloc a composition field array based on the number of component views and all fields (if not cached)
    ptr viewHead = ECX.qs.compositionFieldArray[query];
    if (!viewHead) {
        ECX.qs.compositionFieldArray[query] = r3AllocMemory(sizeof(ptr*) * h.slots + sizeof(ptr) * fieldTotal);
        if (!ECX.qs.compositionFieldArray[query]) {
            r3LogStdOut(R3_LOG_ERROR, "[ECX] Failed `Compose` -- failed to allocate composition field array\n");
            return R3_RESULT_ERROR;
        } else viewHead = ECX.qs.compositionFieldArray[query];
    } // r3LogStdOutF(R3_LOG_DEV, "[ECX] COMPOSITION FIELD ARRAY -- %p\n", viewHead);
    
    ptr** viewv = (ptr**)viewHead;    // outer array of component views
    ptr* fieldv = (ptr*)((u8*)viewHead + sizeof(ptr*) * h.slots); // outer array of component view fieldsets
    FOR(u8, c, 0, h.slots, 1) {
        u8 componentID = ECX.qs.componentMembers[query][c];
        u32 componentFields = ECX.cs.componentFields[componentID];
        ptr componentFieldArray = ECX.cs.componentFieldArray[componentID];
        u32* componentFieldOffsets = ECX.cs.componentFieldOffset[componentID];

        viewv[c] = fieldv;
        FOR(u32, f, 0, componentFields, 1) *fieldv++ = (ptr)((u8*)componentFieldArray + componentFieldOffsets[f]);

        composition->viewv[c].fieldv = viewv[c];
        composition->viewv[c].fields = componentFields;
    } composition->views = h.slots;

    return R3_RESULT_SUCCESS;
}

u8 ecxDecompose(ECXQuery query) {
    if (query > ECX.qs.nextID) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `Decompose` -- invalid `ECXQuery` passed: %d\n", query);
        return R3_RESULT_ERROR;
    } if (!r3FreeMemory(ECX.qs.compositionFieldArray[query])) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `Decompose` -- internal query composition storage could not be destroyed: %d\n", query);
        return R3_RESULT_ERROR;
    } return R3_RESULT_SUCCESS;
}


none ecxIter(ECXQuery query, ECXSystem sys, ptr user) {
    if (query > ECX.qs.nextID) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `IterQuery` -- invalid `ECXQuery` passed: %d\n", query);
        return;
    } if (!sys) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `IterQuery` -- invalid `ECXSystem` passed: %p\n", sys);
        return;
    }

    u32* entityMembers = ECX.qs.entityMembers[query];
    if (!entityMembers) {
        r3LogStdOutF(R3_LOG_ERROR, "[ECX] Failed `IterQuery` -- invalid `ECXQuery` passed: %d\n", query);
        return;
    }

    ECXComposition c = {0};
    ecxCompose(&c, query);
    FOR(u32, e, 0, r3ArrayCount(entityMembers), 1) sys(entityMembers[e], user, &c);

    return;
}


u8 ecxInit(u32 max) {
    r3LogToggle(R3_LOG_DEV);
    r3LogToggle(R3_LOG_INFO);

    ECXRuntimeEntityMax = max;
    ECXRuntimeComponentMax = ECX_COMPONENT_MAX;

    // initialize internal entity storage
    ECX.es.nextID = 0;
    ECX.es.deadID = (u32*)r3NewArray(ECXRuntimeEntityMax, sizeof(u32));
    ECX.es.entityGen = (u16*)r3NewArray(ECXRuntimeEntityMax, sizeof(u16));
    ECX.es.entityMask = (u64*)r3NewArray(ECXRuntimeEntityMax, sizeof(u64));
    if (!ECX.es.deadID || !ECX.es.entityGen || !ECX.es.entityMask) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Failed `Init` -- internal entity storage could not be created\n");
        return R3_RESULT_FATAL;
    }
    r3LogStdOut(R3_LOG_INFO, "\n\nDEBUGGING INTERNAL ENTITY STORAGE ARRAYS\n\n");
    DEBUGARRAY("ECX.es.deadID", ECX.es.deadID);
    DEBUGARRAY("ECX.es.entityGen", ECX.es.entityGen);
    DEBUGARRAY("ECX.es.entityMask", ECX.es.entityMask);

    // initialize internal component storage
    ECX.cs.nextID = 0;
    ECX.cs.deadID = (u8*)r3NewArray(ECXRuntimeComponentMax, sizeof(u8));
    ECX.cs.componentGen = (u8*)r3NewArray(ECXRuntimeComponentMax, sizeof(u8));
    ECX.cs.componentMask = (u64*)r3NewArray(ECXRuntimeComponentMax, sizeof(u64));
    ECX.cs.componentSlots = (u32*)r3NewArray(ECXRuntimeComponentMax, sizeof(u32));
    ECX.cs.componentFields = (u32*)r3NewArray(ECXRuntimeComponentMax, sizeof(u32));
    ECX.cs.componentMembers = (u32**)r3NewArray(ECXRuntimeComponentMax, sizeof(u32*));
    ECX.cs.componentFieldArray = (ptr*)r3NewArray(ECXRuntimeComponentMax, sizeof(ptr));
    ECX.cs.componentMemberMTE = (u32**)r3NewArray(ECXRuntimeComponentMax, sizeof(u32*));
    ECX.cs.componentMemberETM = (u32**)r3NewArray(ECXRuntimeComponentMax, sizeof(u32*));
    ECX.cs.componentFieldOffset = (u32**)r3NewArray(ECXRuntimeComponentMax, sizeof(u32*));
    ECX.cs.componentFieldStride = (u16**)r3NewArray(ECXRuntimeComponentMax, sizeof(u16*));
    if (!ECX.cs.deadID                  ||
        !ECX.cs.componentGen            ||
        !ECX.cs.componentMask           ||
        !ECX.cs.componentSlots          ||
        !ECX.cs.componentFields         ||
        !ECX.cs.componentMembers        ||
        !ECX.cs.componentMemberMTE      ||
        !ECX.cs.componentMemberETM      ||
        !ECX.cs.componentFieldArray     ||
        !ECX.cs.componentFieldOffset    ||  
        !ECX.cs.componentFieldStride) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Failed `Init` -- internal component storage could not be created\n");
        if (!r3DelArray(ECX.es.deadID) || !r3DelArray(ECX.es.entityGen) || !r3DelArray(ECX.es.entityMask)) {
            r3LogStdOut(R3_LOG_WARN, "[ECX] Error while `Init` -- internal entity storage could not be destroyed on fallback\n");
        } return R3_RESULT_FATAL;
    }

    r3LogStdOut(R3_LOG_INFO, "\n\nDEBUGGING INTERNAL COMPONENT STORAGE ARRAYS\n\n");
    DEBUGARRAY("ECX.cs.deadID", ECX.cs.deadID);
    DEBUGARRAY("ECX.cs.componentGen", ECX.cs.componentGen);
    DEBUGARRAY("ECX.cs.componentMask", ECX.cs.componentMask);
    DEBUGARRAY("ECX.cs.componentSlots", ECX.cs.componentSlots);
    DEBUGARRAY("ECX.cs.componentFields", ECX.cs.componentFields);
    DEBUGARRAY("ECX.cs.componentMembers", ECX.cs.componentMembers);
    DEBUGARRAY("ECX.cs.componentMemberMTE", ECX.cs.componentMemberMTE);
    DEBUGARRAY("ECX.cs.componentMemberETM", ECX.cs.componentMemberETM);
    DEBUGARRAY("ECX.cs.componentFieldArray", ECX.cs.componentFieldArray);
    DEBUGARRAY("ECX.cs.componentFieldOffset", ECX.cs.componentFieldOffset);
    DEBUGARRAY("ECX.cs.componentFieldStride", ECX.cs.componentFieldStride);

    // initialize internal query storage
    ECX.qs.nextID = 0;
    ECX.qs.queryIn = (u64*)r3NewArray(ECX_QUERY_MAX, sizeof(u64));
    ECX.qs.queryEx = (u64*)r3NewArray(ECX_QUERY_MAX, sizeof(u64));
    ECX.qs.queryCompMTE = (u8**)r3NewArray(ECX_QUERY_MAX, sizeof(u8*));
    ECX.qs.queryCompETM = (u8**)r3NewArray(ECX_QUERY_MAX, sizeof(u8*));
    ECX.qs.entityMembers = (u32**)r3NewArray(ECX_QUERY_MAX, sizeof(u32*));
    ECX.qs.queryEntityMTE = (u32**)r3NewArray(ECX_QUERY_MAX, sizeof(u32*));
    ECX.qs.queryEntityETM = (u32**)r3NewArray(ECX_QUERY_MAX, sizeof(u32*));
    ECX.qs.componentMembers = (u8**)r3NewArray(ECX_QUERY_MAX, sizeof(u8*));
    ECX.qs.compositionFieldArray = (ptr*)r3NewArray(ECX_QUERY_MAX, sizeof(ptr));
    if (!ECX.qs.queryIn
    ||  !ECX.qs.queryEx
    ||  !ECX.qs.queryCompMTE
    ||  !ECX.qs.queryCompETM
    ||  !ECX.qs.entityMembers
    ||  !ECX.qs.queryEntityMTE
    ||  !ECX.qs.queryEntityETM
    ||  !ECX.qs.componentMembers
    ||  !ECX.qs.compositionFieldArray) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Failed `Init` -- internal component storage could not be created\n");
        if (!r3DelArray(ECX.es.deadID) || !r3DelArray(ECX.es.entityGen) || !r3DelArray(ECX.es.entityMask)) {
            r3LogStdOut(R3_LOG_WARN, "[ECX] Error while `Init` -- internal entity storage could not be destroyed on fallback\n");
        } if (!r3DelArray(ECX.cs.deadID)           ||
          !r3DelArray(ECX.cs.componentGen)         ||
          !r3DelArray(ECX.cs.componentMask)        ||
          !r3DelArray(ECX.cs.componentSlots)       ||
          !r3DelArray(ECX.cs.componentFields)      ||
          !r3DelArray(ECX.cs.componentMembers)     ||
          !r3DelArray(ECX.cs.componentMemberMTE)   ||
          !r3DelArray(ECX.cs.componentMemberETM)   ||
          !r3DelArray(ECX.cs.componentFieldArray)  ||
          !r3DelArray(ECX.cs.componentFieldOffset) ||
          !r3DelArray(ECX.cs.componentFieldStride)) {
            r3LogStdOut(R3_LOG_WARN, "[ECX] Error while `Exit` -- internal component storage could not be destroyed\n");
        } return R3_RESULT_FATAL;
    }

    r3LogStdOut(R3_LOG_INFO, "\n\nDEBUGGING INTERNAL QUERY STORAGE ARRAYS\n\n");
    DEBUGARRAY("ECX.qs.queryIn", ECX.qs.queryIn);
    DEBUGARRAY("ECX.qs.queryEx", ECX.qs.queryEx);
    DEBUGARRAY("ECX.qs.entityMembers", ECX.qs.entityMembers);
    DEBUGARRAY("ECX.qs.componentMembers", ECX.qs.componentMembers);

    ECXRuntimeState = 1;
    
    r3LogToggle(R3_LOG_DEV);
    r3LogToggle(R3_LOG_INFO);

    r3LogStdOut(R3_LOG_DEV, "[ECX] INITIALIZED\n");
    return R3_RESULT_SUCCESS;
}

u8 ecxExit(none) {
    if (!ECXRuntimeState) return R3_RESULT_SUCCESS;

    ECXRuntimeState = 0;

    if (!r3DelArray(ECX.es.deadID) || !r3DelArray(ECX.es.entityGen) || !r3DelArray(ECX.es.entityMask)) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Error while `Exit` -- internal entity storage could not be destroyed\n");
    }
    
    FOR(u8, c, 0, ECX.cs.nextID, 1) {
        if ((ECX.cs.componentMembers[c] && !r3DelArray(ECX.cs.componentMembers[c]))
        ||  (ECX.cs.componentMemberMTE[c] && !r3DelArray(ECX.cs.componentMemberMTE[c]))
        ||  (ECX.cs.componentMemberETM[c] && !r3DelArray(ECX.cs.componentMemberETM[c]))
        ||  (ECX.cs.componentFieldArray[c] && !r3FreeMemory(ECX.cs.componentFieldArray[c]))
        ||  (ECX.cs.componentFieldOffset[c] && !r3DelArray(ECX.cs.componentFieldOffset[c]))
        ||  (ECX.cs.componentFieldStride[c] && !r3DelArray(ECX.cs.componentFieldStride[c]))) {
            r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `Exit` -- internal component storage could not be destroyed for component: %d\n", c);
        }
    } if (!r3DelArray(ECX.cs.deadID)               ||
          !r3DelArray(ECX.cs.componentGen)         ||
          !r3DelArray(ECX.cs.componentMask)        ||
          !r3DelArray(ECX.cs.componentSlots)       ||
          !r3DelArray(ECX.cs.componentFields)      ||
          !r3DelArray(ECX.cs.componentMembers)     ||
          !r3DelArray(ECX.cs.componentMemberMTE)   ||
          !r3DelArray(ECX.cs.componentMemberETM)   ||
          !r3DelArray(ECX.cs.componentFieldArray)  ||
          !r3DelArray(ECX.cs.componentFieldOffset) ||
          !r3DelArray(ECX.cs.componentFieldStride)) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Error while `Exit` -- internal component storage could not be destroyed\n");
    }

    FOR(u16, q, 0, ECX.qs.nextID, 1) {
       if ((ECX.qs.queryCompETM[q] && !r3DelArray(ECX.qs.queryCompETM[q]))
       ||  (ECX.qs.queryCompMTE[q] && !r3DelArray(ECX.qs.queryCompMTE[q]))
       ||  (ECX.qs.entityMembers[q] && !r3DelArray(ECX.qs.entityMembers[q]))
       ||  (ECX.qs.queryEntityETM[q] && !r3DelArray(ECX.qs.queryEntityETM[q]))
       ||  (ECX.qs.queryEntityMTE[q] && !r3DelArray(ECX.qs.queryEntityMTE[q]))
       ||  (ECX.qs.componentMembers[q] && !r3DelArray(ECX.qs.componentMembers[q]))
       ||  (ECX.qs.compositionFieldArray[q] && !r3FreeMemory(ECX.qs.compositionFieldArray[q]))) {
            r3LogStdOutF(R3_LOG_ERROR, "[ECX] Error while `Exit` -- internal query storage could not be destroyed for query: %d\n", q);
        }
    }
    if (!r3DelArray(ECX.qs.queryIn)       || !r3DelArray(ECX.qs.queryEx)
    || !r3DelArray(ECX.qs.queryCompMTE)   || !r3DelArray(ECX.qs.queryCompETM)
    || !r3DelArray(ECX.qs.queryEntityMTE) || !r3DelArray(ECX.qs.queryEntityETM)
    || !r3DelArray(ECX.qs.entityMembers)  || !r3DelArray(ECX.qs.componentMembers)) {
        r3LogStdOut(R3_LOG_ERROR, "[ECX] Error while `Exit` -- internal query storage could not be destroyed\n");
    }

    r3LogStdOut(R3_LOG_DEV, "[ECX] DEINITIALIZED\n");
    return R3_RESULT_SUCCESS;
}
