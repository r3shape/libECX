#ifndef __ECX_H__
#define __ECX_H__

#include <r3kit/include/ds/arr.h>

#define ECX_ENTITY_MAX      0xFFFFFFFF
#define ECX_COMPONENT_MAX   0x40
#define ECX_FIELD_DATA_MAX  1 * GiB

typedef u64 ECXEntity;
typedef u16 ECXConfig;
typedef u8  ECXContext;
typedef u8  ECXComponent;

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

R3_API ECXEntity newEntity(none);
R3_API u8 delEntity(ECXEntity);

R3_API ECXComponent newComponent(ECXComponentDesc comp);
R3_API u8 delComponent(ECXComponent comp);

R3_API ECXConfig query(ECXQueryDesc query);
R3_API u8 bind(ECXEntity ent, ECXComponent comp);
R3_API u8 unbind(ECXEntity ent, ECXComponent comp);

R3_API u8 getFieldArray(u8 field, ptr out, ECXComponent comp);
R3_API u8 getField(u8 field, ptr out, ECXEntity ent, ECXComponent comp);

R3_API u8 setFieldArray(u8 field, ptr in, ECXComponent comp);
R3_API u8 setField(u8 field, ptr in, ECXEntity ent, ECXComponent comp);

R3_API u8 ECXInit(u32 entityMax);
R3_API u8 ECXExit(none);

#endif // __ECX_H__