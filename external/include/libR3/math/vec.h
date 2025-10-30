
#ifndef __R3_MATH_VEC_H__
#define __R3_MATH_VEC_H__

#include <include/libR3/math/num.h>
#include <include/libR3/io/log.h>

#define R3LOGV2(v, msg) r3LogStdOutF(R3_LOG_DEV, "v2 %s |%0.1f, %0.1f|\n", msg, v[0], v[1])
#define R3LOGV3(v, msg) r3LogStdOutF(R3_LOG_DEV, "v3 %s |%0.1f, %0.1f, %0.1f|\n", msg, v[0], v[1], v[2])
#define R3LOGV4(v, msg) r3LogStdOutF(R3_LOG_DEV, "v4 %s |%0.1f, %0.1f, %0.1f, %0.1f|\n", msg, v[0], v[1], v[2], v[3])

#define R3VX(v) v[0]
#define R3VY(v) v[1]
#define R3VZ(v) v[2]

#define R3V2(v, x, y)           f32 v[2] = {(x), (y)}
#define R3V3(v, x, y, z)        f32 v[3] = {(x), (y), (z)}
#define R3V4(v, x, y, z, w)     f32 v[4] = {(x), (y), (z), (w)}

#define R3ScaleV2(v, s) \
    v[0] *= s;          \
    v[1] *= s;

#define R3ScaleV3(v, s) \
    v[0] *= s;          \
    v[1] *= s;          \

#define R3ScaleV4(v, s) \
    v[0] *= s;          \
    v[1] *= s;          \
    v[2] *= s;          \
    v[3] *= s;          \


#define R3AddV2(v1, v2)          \
    v1[0] = R3Add(v1[0], v2[0]); \
    v1[1] = R3Add(v1[1], v2[1]); \

#define R3AddV3(v1, v2)          \
    v1[0] = R3Add(v1[0], v2[0]); \
    v1[1] = R3Add(v1[1], v2[1]); \
    v1[2] = R3Add(v1[2], v2[2]); \

#define R3AddV4(v1, v2)          \
    v1[0] = R3Add(v1[0], v2[0]); \
    v1[1] = R3Add(v1[1], v2[1]); \
    v1[2] = R3Add(v1[2], v2[2]); \
    v1[3] = R3Add(v1[3], v2[3]);


#define R3SubV2(v1, v2)          \
    v1[0] = R3Sub(v1[0], v2[0]); \
    v1[1] = R3Sub(v1[1], v2[1]); \

#define R3SubV3(v1, v2)          \
    v1[0] = R3Sub(v1[0], v2[0]); \
    v1[1] = R3Sub(v1[1], v2[1]); \
    v1[2] = R3Sub(v1[2], v2[2]); \

#define R3SubV4(v1, v2)          \
    v1[0] = R3Sub(v1[0], v2[0]); \
    v1[1] = R3Sub(v1[1], v2[1]); \
    v1[2] = R3Sub(v1[2], v2[2]); \
    v1[3] = R3Sub(v1[3], v2[3]);


#define R3MulV2(v1, v2)          \
    v1[0] = R3Mul(v1[0], v2[0]); \
    v1[1] = R3Mul(v1[1], v2[1]); \

#define R3MulV3(v1, v2)          \
    v1[0] = R3Mul(v1[0], v2[0]); \
    v1[1] = R3Mul(v1[1], v2[1]); \
    v1[2] = R3Mul(v1[2], v2[2]); \

#define R3MulV4(v1, v2)          \
    v1[0] = R3Mul(v1[0], v2[0]); \
    v1[1] = R3Mul(v1[1], v2[1]); \
    v1[2] = R3Mul(v1[2], v2[2]); \
    v1[3] = R3Mul(v1[3], v2[3]);


#define R3DivV2(v1, v2)          \
    v1[0] = R3Div(v1[0], v2[0]); \
    v1[1] = R3Div(v1[1], v2[1]); \

#define R3DivV3(v1, v2)          \
    v1[0] = R3Div(v1[0], v2[0]); \
    v1[1] = R3Div(v1[1], v2[1]); \
    v1[2] = R3Div(v1[2], v2[2]); \

#define R3DivV4(v1, v2)          \
    v1[0] = R3Div(v1[0], v2[0]); \
    v1[1] = R3Div(v1[1], v2[1]); \
    v1[2] = R3Div(v1[2], v2[2]); \
    v1[3] = R3Div(v1[3], v2[3]);


#define R3DotV2(v1, v2) ((v1[0] * v2[0]) + (v1[1] * v2[1]))
#define R3DotV3(v1, v2) ((v1[0] * v2[0]) + (v1[1] * v2[1]) + (v1[2] * v2[2]))
#define R3DotV4(v1, v2) ((v1[0] * v2[0]) + (v1[1] * v2[1]) + (v1[2] * v2[2]) + (v1[3] * v2[3]))

#endif // __R3_MATH_VEC_H__