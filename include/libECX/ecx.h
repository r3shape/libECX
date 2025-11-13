#ifndef __ECX_H__
#define __ECX_H__

#include <include/libR3/ds/array.h>

#define ECX_MEMORY          2ULL * GiB
#define ECX_QUERY_MAX       0x0FFF
#define ECX_FIELD_MAX       0xFF
#define ECX_ENTITY_MAX      0xFFFFFFFF
#define ECX_COMPONENT_MAX   0x40

typedef u16 ECXQuery;
typedef u64 ECXEntity;
typedef u16 ECXComponent;

typedef struct ECXFieldDesc {
    u16 stride;     // element stride
    char* hash;       // TODO: string-name hash for hash array lookups
} ECXFieldDesc;

typedef struct ECXComponentDesc {
    ECXFieldDesc* fieldv;   // field descriptors
    char* hash;             // TODO: string-name hash for hash array lookups
    u8 fields;              // number of fields
    u64 mask;               // identifier bitmask
    u32 slots;                // field max
} ECXComponentDesc;

typedef struct ECXQueryDesc {
    u64 in;   // components included by a query
    u64 ex;   // components excluded by a query
} ECXQueryDesc;

typedef struct ECXView {
    ptr* fieldv;
    u32 fields;
} ECXView;

typedef struct ECXComposition {
    ECXView viewv[ECX_COMPONENT_MAX];
    u8 views;
} ECXComposition;

typedef none (*ECXSystem)(u32 index, ptr user, ECXComposition* composition);

R3_PUBLIC_API u8 ecxExit(none);
R3_PUBLIC_API u8 ecxInit(u32 max);

R3_PUBLIC_API ECXEntity ecxNewEntity(none);
R3_PUBLIC_API u8 ecxDelEntity(ECXEntity entity);

R3_PUBLIC_API ECXComponent ecxNewComponent(ECXComponentDesc component);
R3_PUBLIC_API u8 ecxDelComponent(ECXComponent component);

R3_PUBLIC_API ECXQuery ecxQuery(ECXQueryDesc desc);

R3_PUBLIC_API u8 ecxCompose(ECXComposition* composition, ECXQuery query);
R3_PUBLIC_API u8 ecxDecompose(ECXQuery query);

R3_PUBLIC_API none ecxIter(ECXQuery query, ECXSystem sys, ptr user);

R3_PUBLIC_API u8 ecxBind(ECXEntity entity, ECXComponent component);
R3_PUBLIC_API u8 ecxUnbind(ECXEntity entity, ECXComponent component);

R3_PUBLIC_API u8 ecxSetFieldArray(u8 field, u8 in, ECXComponent component);
R3_PUBLIC_API ptr ecxGetFieldArray(u8 field, ECXComponent component);

R3_PUBLIC_API u8 ecxGetField(u8 field, ptr out, ECXEntity entity, ECXComponent component);
R3_PUBLIC_API u8 ecxSetField(u8 field, ptr in, ECXEntity entity, ECXComponent component);

#endif // __ECX_H__