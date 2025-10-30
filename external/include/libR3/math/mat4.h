#ifndef __R3_MATH_MAT_H__
#define __R3_MATH_MAT_H__

#include <include/libR3/math/num.h>
#include <include/libR3/io/log.h>

#define R3LOGM4(m, msg) r3LogStdOutF(R3_LOG_DEV, "m4 %s \n|%0.1f, %0.1f, %0.1f, %0.1f|\n|%0.1f, %0.1f, %0.1f, %0.1f|\n|%0.1f, %0.1f, %0.1f, %0.1f|\n|%0.1f, %0.1f, %0.1f, %0.1f|\n", msg, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15])

#define R3M4ID(m) f32 m[16] = { \
    1, 0, 0, 0, \
    0, 1, 0, 0, \
    0, 0, 1, 0, \
    0, 0, 0, 1  \
}

/* Row Major API */
#define R3M4RC(m, r, c) (m[(r) * 4 + (c)])

#define R3M4XR(m, v) f32 v[3] = {m[0], m[1], m[2]}
#define R3M4YR(m, v) f32 v[3] = {m[4], m[5], m[6]}
#define R3M4ZR(m, v) f32 v[3] = {m[8], m[9], m[10]}

#define R3M4RMxM4(m, a, b)      \
    f32 m[16] = {0};            \
    FOR(u8, r, 0, 4, 1) {       \
        FOR(u8, c, 0, 4, 1) {   \
            FOR_I(0, 4, 1) {    \
                m[r * 4 + c] += a[i * 4 + c] * b[r * 4 + i]; \
            }                   \
        }                       \
    }

#define R3M4RM(m, vx, vy, vz) f32 m[16] = { \
    vx[0], vx[1], vx[2], 0,     \
    vy[0], vy[1], vy[2], 0,     \
    vz[0], vz[1], vz[2], 0,     \
    0, 0, 0, 1                  \
}

#define R3M4RMxV3(m, v, v2)         \
    f32 v2[3] = {(                  \
        m[0] * v[0] +               \
        m[1] * v[1] +               \
        m[2] * v[2] + m[3]          \
    ), (                            \
        m[4] * v[0] +               \
        m[5] * v[1] +               \
        m[6] * v[2] + m[7]          \
    ), (                            \
        m[8] * v[0] +               \
        m[9] * v[1] +               \
        m[10] * v[2] + m[11]        \
    )}



/* Column Major API */
#define R3M4CR(m, r, c) (m[(c) * 4 + (r)])

#define R3M4XC(m, v) f32 v[3] = {m[0], m[4], m[8]}
#define R3M4YC(m, v) f32 v[3] = {m[1], m[5], m[9]}
#define R3M4ZC(m, v) f32 v[3] = {m[2], m[6], m[10]}

#define R3M4CM(m, vx, vy, vz) f32 m[16] = { \
    vx[0], vy[0], vz[0], 0,     \
    vx[1], vy[1], vz[1], 0,     \
    vx[2], vy[2], vz[2], 0,     \
    0, 0, 0, 1                  \
}

#define R3M4CMxM4(m, a, b)      \
    f32 m[16] = {0};            \
    FOR(u8, r, 0, 4, 1) {       \
        FOR(u8, c, 0, 4, 1) {   \
            FOR_I(0, 4, 1) {    \
                m[c * 4 + r] += a[i * 4 + r] * b[c * 4 + i]; \
            }                   \
        }                       \
    }

#define R3M4CMxV3(m, v, v2)         \
    f32 v2[3] = {(                  \
        m[0] * v[0] +               \
        m[4] * v[1] +               \
        m[8] * v[2] + m[12]         \
    ), (                            \
        m[1] * v[0] +               \
        m[5] * v[1] +               \
        m[9] * v[2] + m[13]         \
    ), (                            \
        m[2] * v[0] +               \
        m[6] * v[1] +               \
        m[10] * v[2] + m[14]        \
    )}

#endif // __R3_MATH_MAT_H__