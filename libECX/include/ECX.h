#ifndef __ECX_H__
#define __ECX_H__

#include <r3kit/include/ds/arr.h>

#define ECX_MEMORY  1 * GiB
#define ECX_ENTITY_MAX      0xFFFFFFFF
#define ECX_FIELD_MAX       0xFF
#define ECX_COMPONENT_MAX   0x40

typedef u64 ECXEntity;
typedef u16 ECXQuery;
typedef u8  ECXContext;
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
    u32 max;                // field max
} ECXComponentDesc;

typedef struct ECXQueryDesc {
    u64 any;    // components that MAY be bound
    u64 all;    // components that MUST be bound
    u64 none;   // components that MAY NOT be bound
} ECXQueryDesc;

typedef struct ECXView {
    ptr* fieldSet;
    u16 fieldCount;
} ECXView;

typedef struct ECXComposition {
    ECXView viewSet[ECX_COMPONENT_MAX];
    u8 viewCount;
} ECXComposition;

typedef none (*ECXSystem)(u32 index, ptr user, ECXComposition* comp);

R3_API ECXEntity newEntity(none);
R3_API u8 delEntity(ECXEntity);

R3_API ECXComponent newComponent(ECXComponentDesc comp);
R3_API u8 delComponent(ECXComponent comp);

R3_API ECXQuery query(ECXQueryDesc desc);
R3_API ECXComposition compose(ECXQuery config);
R3_API u8 decompose(ECXQuery config);

R3_API none iter(ECXQuery config, ECXSystem sys, ptr user);

R3_API u8 bind(ECXEntity ent, ECXComponent comp);
R3_API u8 unbind(ECXEntity ent, ECXComponent comp);

R3_API ptr getFieldArray(u8 field, ECXComponent comp);
R3_API u8 getField(u8 field, ptr out, ECXEntity ent, ECXComponent comp);

R3_API u8 setFieldArray(u8 field, u8 in, ECXComponent comp);
R3_API u8 setField(u8 field, ptr in, ECXEntity ent, ECXComponent comp);

R3_API u8 ECXInit(u32 entityMax);
R3_API u8 ECXExit(none);

#endif // __ECX_H__