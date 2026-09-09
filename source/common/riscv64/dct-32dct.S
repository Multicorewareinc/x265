/*****************************************************************************
 * Copyright (C) 2026 MulticoreWare, Inc
 *
 * Authors: daichengrong <daichengrong@iscas.ac.cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#include "asm.S"

#ifdef __APPLE__
.section __RODATA,__rodata
#else
.section .rodata
#endif

.align 4

.set dct32_shift_1, 4 + BIT_DEPTH - 8
.set dct32_shift_2, 11

.text

#define DCT32_O_CONSTANT_1_0 90, 90, 88, 85, 82, 78, 73, 67, 61, 54, 46, 38, 31, 22, 13, 4
#define DCT32_O_CONSTANT_3_1  90, 82, 67, 46, 22, -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13
#define DCT32_O_CONSTANT_5_2  88, 67, 31, -13, -54, -82, -90, -78, -46, -4, 38, 73, 90, 85, 61, 22
#define DCT32_O_CONSTANT_7_3  85, 46, -13, -67, -90, -73, -22, 38, 82, 88, 54, -4, -61, -90, -78, -31
#define DCT32_O_CONSTANT_9_4  82, 22, -54, -90, -61, 13, 78, 85, 31, -46, -90, -67, 4, 73, 88, 38
#define DCT32_O_CONSTANT_11_5  78, -4, -82, -73, 13, 85, 67, -22, -88, -61, 31, 90, 54, -38, -90, -46
#define DCT32_O_CONSTANT_13_6  73, -31, -90, -22, 78, 67, -38, -90, -13, 82, 61, -46, -88, -4, 85, 54
#define DCT32_O_CONSTANT_15_7  67, -54, -78, 38, 85, -22, -90, 4, 90, 13, -88, -31, 82, 46, -73, -61
#define DCT32_O_CONSTANT_17_8  61, -73, -46, 82, 31, -88, -13, 90, -4, -90, 22, 85, -38, -78, 54, 67
#define DCT32_O_CONSTANT_19_9  54, -85, -4, 88, -46, -61, 82, 13, -90, 38, 67, -78, -22, 90, -31, -73
#define DCT32_O_CONSTANT_21_10  46, -90, 38, 54, -90, 31, 61, -88, 22, 67, -85, 13, 73, -82,  4, 78
#define DCT32_O_CONSTANT_23_11  38, -88, 73, -4, -67, 90, -46, -31, 85, -78, 13, 61, -90, 54, 22, -82
#define DCT32_O_CONSTANT_25_12  31, -78, 90, -61, 4, 54, -88, 82, -38, -22, 73, -90, 67, -13, -46, 85
#define DCT32_O_CONSTANT_27_13  22, -61, 85, -90, 73, -38, -4, 46, -78, 90, -82, 54, -13, -31, 67, -88
#define DCT32_O_CONSTANT_29_14  13, -38, 61, -78, 88, -90, 85, -73, 54, -31, 4, 22, -46, 67, -82, 90
#define DCT32_O_CONSTANT_31_15  4, -13, 22, -31, 38, -46, 54, -61, 67, -73, 78, -82, 85, -88, 90, -90


#define DCT32_EO_CONSTANT_2_0  90, 87, 80, 70, 57, 43, 25, 9
#define DCT32_EO_CONSTANT_6_1  87, 57,  9, -43, -80, -90, -70, -25
#define DCT32_EO_CONSTANT_10_2  80,  9, -70, -87, -25, 57, 90, 43
#define DCT32_EO_CONSTANT_14_3  70, -43, -87,  9, 90, 25, -80, -57

#define DCT32_EO_CONSTANT_18_4  57, -80, -25, 90, -9, -87, 43, 70
#define DCT32_EO_CONSTANT_22_5  43, -90, 57, 25, -87, 70,  9, -80
#define DCT32_EO_CONSTANT_26_6  25, -70, 90, -80, 43,  9, -57, 87
#define DCT32_EO_CONSTANT_30_7  9, -25, 43, -57, 70, -80, 87, -90

.macro  lx rd, addr
#if (__riscv_xlen == 32)
        lw      \rd, \addr
#elif (__riscv_xlen == 64)
        ld      \rd, \addr
#else
        lq      \rd, \addr
#endif
.endm

.macro  sx rd, addr
#if (__riscv_xlen == 32)
        sw      \rd, \addr
#elif (__riscv_xlen == 64)
        sd      \rd, \addr
#else
        sq      \rd, \addr
#endif
.endm

.macro butterfly e, o, tmp_p, tmp_m
        vadd.vv         \tmp_p, \e, \o
        vsub.vv         \tmp_m, \e, \o
.endm

.macro butterfly_widen e, o, tmp_p, tmp_m
        vwadd.vv         \tmp_p, \e, \o
        vwsub.vv         \tmp_m, \e, \o
.endm

.macro DCT32_EEO_CAL dst, m1, m2, m3, m4, s1, s2, s3, s4, line, shift
    li              a2, \m1
    li              a3, \m2
    li              a4, \m3
    li              a5, \m4
    vmul.vx         \dst, \s1, a2
    vmacc.vx        \dst, a3, \s2
    vmacc.vx        \dst, a4, \s3
    vmacc.vx        \dst, a5, \s4
.endm

.macro DCT32_4_DST_ADD_1_MEMBER first, in, dst_start_index, dst1, dst2, dst3, dst4, t0, t1,t2,t3,t4,t5,t6,t7,t8,t9,t10,t11,t12,t13,t14,t15
.if \dst_start_index == 0
    li              a2, \t0
    li              a3, \t1
    li              a4, \t2
    li              a5, \t3
.elseif \dst_start_index == 4
    li              a2, \t4
    li              a3, \t5
    li              a4, \t6
    li              a5, \t7
.elseif \dst_start_index == 8
    li              a2, \t8
    li              a3, \t9
    li              a4, \t10
    li              a5, \t11
.else
    li              a2, \t12
    li              a3, \t13
    li              a4, \t14
    li              a5, \t15
.endif

.if \first == 1
    vmul.vx        \dst1, \in, a2
    vmul.vx        \dst2, \in, a3
    vmul.vx        \dst3, \in, a4
    vmul.vx        \dst4, \in, a5
.else
    vmacc.vx       \dst1, a2, \in
    vmacc.vx       \dst2, a3, \in
    vmacc.vx       \dst3, a4, \in
    vmacc.vx       \dst4, a5, \in
.endif
.endm

.macro DCT32_STORE_L line, shift, in
    vnclip.wi           \in, \in, \shift
    addi                t0, a1, 32 * 2 * \line
    vse16.v             \in, (t0)
.endm

.macro tr_32xN_rvv name, shift
function func_tr_32xN_\name\()_rvv
        .option arch, +zba
        // E saved from tmp stack
        mv              a7, t5
        // one vector bytes after widen
        slli            t2, t4, 2
        // O saved from tmp stack + 16xE
        slli            t0, t2, 4
        add             a6, t5, t0

        // load 0-3 28-31
        add             t0, a0, 2*0
        vlsseg4e16.v        v0,(a0), t3
        add             t0, a0, 2*28
        vlsseg4e16.v       v4,(t0), t3

        butterfly_widen     v0, v7, v8, v16
        butterfly_widen     v1, v6, v10, v18
        butterfly_widen     v2, v5, v12, v20
        butterfly_widen     v3, v4, v14, v22

        // load 4-7 24-27
        add             t0, a0, 2*4
        vlsseg4e16.v       v0,(t0), t3
        add             t0, a0, 2*24
        vlsseg4e16.v       v4,(t0), t3

        // save E 0 1 2 3
        vse32.v         v8, (a7)
        add             a7, a7, t2
        vse32.v         v10, (a7)
        add             a7, a7, t2
        vse32.v         v12, (a7)
        add             a7, a7, t2
        vse32.v         v14, (a7)

        // save O 1 2 3 4
        vse32.v         v16, (a6)
        add             a6, a6, t2
        vse32.v         v18, (a6)
        add             a6, a6, t2
        vse32.v         v20, (a6)
        add             a6, a6, t2
        vse32.v         v22, (a6)

        vsetvli zero, zero, e32, m2, ta, ma
        DCT32_4_DST_ADD_1_MEMBER     1, v16, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_1_0
        DCT32_4_DST_ADD_1_MEMBER     0, v18, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_3_1

        DCT32_4_DST_ADD_1_MEMBER     0, v20, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_5_2
        DCT32_4_DST_ADD_1_MEMBER     0, v22, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_7_3

        vsetvli zero, zero, e16, m1, ta, ma
        butterfly_widen     v0, v7, v8, v16
        butterfly_widen     v1, v6, v10, v18
        butterfly_widen     v2, v5, v12, v20
        butterfly_widen     v3, v4, v14, v22

        // load 8-11 20-23
        add             t0, a0, 2*8
        vlsseg4e16.v       v0,(t0), t3
        add             t0, a0, 2*20
        vlsseg4e16.v       v4,(t0), t3

        // save E 4 5 6 7
        add             a7, a7, t2
        vse32.v         v8, (a7)
        add             a7, a7, t2
        vse32.v         v10, (a7)
        add             a7, a7, t2
        vse32.v         v12, (a7)
        add             a7, a7, t2
        vse32.v         v14, (a7)

        // save O 4 5 6 7
        add             a6, a6, t2
        vse32.v         v16, (a6)
        add             a6, a6, t2
        vse32.v         v18, (a6)
        add             a6, a6, t2
        vse32.v         v20, (a6)
        add             a6, a6, t2
        vse32.v         v22, (a6)

        vsetvli zero, zero, e32, m2, ta, ma
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_9_4
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_11_5

        DCT32_4_DST_ADD_1_MEMBER      0, v20, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_13_6
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_15_7

        vsetvli zero, zero, e16, m1, ta, ma
        butterfly_widen     v0, v7, v8, v16
        butterfly_widen     v1, v6, v10, v18
        butterfly_widen     v2, v5, v12, v20
        butterfly_widen     v3, v4, v14, v22

        // load 12-15 16-19
        add             t0, a0, 2*12
        vlsseg4e16.v       v0,(t0), t3
        add             t0, a0, 2*16
        vlsseg4e16.v       v4,(t0), t3

        // save E 8 9 10 11
        add             a7, a7, t2
        vse32.v         v8, (a7)
        add             a7, a7, t2
        vse32.v         v10, (a7)
        add             a7, a7, t2
        vse32.v         v12, (a7)
        add             a7, a7, t2
        vse32.v         v14, (a7)

        // save O 8 9 10 11
        add             a6, a6, t2
        vse32.v         v16, (a6)
        add             a6, a6, t2
        vse32.v         v18, (a6)
        add             a6, a6, t2
        vse32.v         v20, (a6)
        add             a6, a6, t2
        vse32.v         v22, (a6)

        vsetvli zero, zero, e32, m2, ta, ma
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_17_8
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_19_9

       DCT32_4_DST_ADD_1_MEMBER      0, v20, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_21_10
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_23_11

        vsetvli zero, zero, e16, m1, ta, ma
        butterfly_widen     v0, v7, v8, v16
        butterfly_widen     v1, v6, v10, v18
        butterfly_widen     v2, v5, v12, v20
        butterfly_widen     v3, v4, v14, v22

        // save E 12 13 14 15
        add             a7, a7, t2
        vse32.v         v8, (a7)
        add             a7, a7, t2
        vse32.v         v10, (a7)
        add             a7, a7, t2
        vse32.v         v12, (a7)
        add             a7, a7, t2
        vse32.v         v14, (a7)

        vsetvli zero, zero, e32, m2, ta, ma
       DCT32_4_DST_ADD_1_MEMBER      0, v16, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_25_12
       DCT32_4_DST_ADD_1_MEMBER      0, v18, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_27_13

       DCT32_4_DST_ADD_1_MEMBER      0, v20, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_29_14
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 0, v24, v26, v28, v30, DCT32_O_CONSTANT_31_15

        vsetvli zero, zero, e16, m1, ta, ma
        DCT32_STORE_L   1, \shift, v24
        DCT32_STORE_L   3, \shift, v26
        DCT32_STORE_L   5, \shift, v28
        DCT32_STORE_L   7, \shift, v30


        // cal dst 4-15
        vsetvli zero, zero, e32, m2, ta, ma
        // 12
       DCT32_4_DST_ADD_1_MEMBER      1, v16, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_25_12
       DCT32_4_DST_ADD_1_MEMBER      1, v16, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_25_12
       DCT32_4_DST_ADD_1_MEMBER      1, v16, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_25_12
        // reload O0 to v16
        slli                        t0, t2, 4
        add                         a6, t5, t0
        vle32.v                     v16, (a6)

        // 13
       DCT32_4_DST_ADD_1_MEMBER      0, v18, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_27_13
       DCT32_4_DST_ADD_1_MEMBER      0, v18, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_27_13
       DCT32_4_DST_ADD_1_MEMBER      0, v18, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_27_13
        // reload O1 to v18
        add                         a6, a6, t2
        vle32.v                     v18, (a6)

        // 14
       DCT32_4_DST_ADD_1_MEMBER      0, v20, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_29_14
       DCT32_4_DST_ADD_1_MEMBER      0, v20, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_29_14
       DCT32_4_DST_ADD_1_MEMBER      0, v20, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_29_14
        // reload O2 to v20
        add                         a6, a6, t2
        vle32.v                     v20, (a6)

        // 15
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_31_15
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_31_15
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_31_15
        // reload O3 to v22
        add                         a6, a6, t2
        vle32.v                     v22, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v16, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_1_0
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_1_0
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_1_0
        // reload O4 to v16
        add                         a6, a6, t2
        vle32.v                     v16, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v18, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_3_1
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_3_1
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_3_1
        // reload O5 to v18
        add                         a6, a6, t2
        vle32.v                     v18, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v20, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_5_2
        DCT32_4_DST_ADD_1_MEMBER      0, v20, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_5_2
        DCT32_4_DST_ADD_1_MEMBER      0, v20, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_5_2
        // reload O6 to v20
        add                         a6, a6, t2
        vle32.v                     v20, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v22, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_7_3
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_7_3
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_7_3
        // reload O7 to v22
        add                         a6, a6, t2
        vle32.v                     v22, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v16, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_9_4
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_9_4
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_9_4
        // reload O8 to v16
        add                         a6, a6, t2
        vle32.v                     v16, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v18, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_11_5
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_11_5
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_11_5
        // reload O9 to v18
        add                         a6, a6, t2
        vle32.v                     v18, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v20, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_13_6
        DCT32_4_DST_ADD_1_MEMBER      0, v20, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_13_6
        DCT32_4_DST_ADD_1_MEMBER      0, v20, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_13_6
        // reload O10 to v20
        add                         a6, a6, t2
        vle32.v                     v20, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v22, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_15_7
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_15_7
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_15_7
        // reload O11 to v22
        add                         a6, a6, t2
        vle32.v                     v22, (a6)


        DCT32_4_DST_ADD_1_MEMBER      0, v16, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_17_8
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_17_8
        DCT32_4_DST_ADD_1_MEMBER      0, v16, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_17_8

        // reload   E 0 to v16
        add                             a7, t5, zero
        vle32.v                         v16, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v18, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_19_9
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_19_9
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_19_9
        // reload   E1 to v18
        add                             a7, a7, t2
        vle32.v                         v18, (a7)

       DCT32_4_DST_ADD_1_MEMBER      0, v20, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_21_10
       DCT32_4_DST_ADD_1_MEMBER      0, v20, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_21_10
       DCT32_4_DST_ADD_1_MEMBER      0, v20, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_21_10
        // reload   E2 to v20
        add                             a7, a7, t2
        vle32.v                         v20, (a7)

       DCT32_4_DST_ADD_1_MEMBER      0, v22, 4, v0, v2, v4, v6, DCT32_O_CONSTANT_23_11

        vsetvli zero, zero, e16, m1, ta, ma
        // write 9 11 13 15
        DCT32_STORE_L   9, \shift, v0
        DCT32_STORE_L   11, \shift, v2
        DCT32_STORE_L   13, \shift, v4
        DCT32_STORE_L   15, \shift, v6

        // reload   E3 to v0
        add                             a7, a7, t2
        vle32.v                         v0, (a7)
        // reload   E12 to v2
        add                             a7, a7, t2
        sh3add                          a7, t2, a7
        vle32.v                         v2, (a7)
        // reload   E13 to v4
        add                             a7, a7, t2
        vle32.v                         v4, (a7)
        // reload   E14 to v6
        add                             a7, a7, t2
        vle32.v                         v6, (a7)

        vsetvli zero, zero, e32, m2, ta, ma
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 8, v8, v10, v12, v14, DCT32_O_CONSTANT_23_11
        // write 17 19 21 23
        vsetvli zero, zero, e16, m1, ta, ma
        DCT32_STORE_L   17, \shift, v8
        DCT32_STORE_L   19, \shift, v10
        DCT32_STORE_L   21, \shift, v12
        DCT32_STORE_L   23, \shift, v14

        // reload   E15 to v8
        add                             a7, a7, t2
        vle32.v                         v8, (a7)

        vsetvli zero, zero, e32, m2, ta, ma
       DCT32_4_DST_ADD_1_MEMBER      0, v22, 12, v24, v26, v28, v30, DCT32_O_CONSTANT_23_11
        vsetvli zero, zero, e16, m1, ta, ma
        // write 25 27 29 31
        DCT32_STORE_L   25, \shift, v24
        DCT32_STORE_L   27, \shift, v26
        DCT32_STORE_L   29, \shift, v28
        DCT32_STORE_L   31, \shift, v30

        vsetvli zero, zero, e32, m2, ta, ma
        // cal  E 3 12  EE EO  3
        butterfly v0, v2, v10, v0
        // save EE 3
        slli            t0, t2, 4
        add             a6, t5, t0
        vse32.v         v10, (a6)
        // reload E 4
        sh2add          a7, t2, t5
        vle32.v         v10, (a7)

        // cal dst 2 4 6 10
        DCT32_4_DST_ADD_1_MEMBER      1, v0, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_14_3

        // cal  E 2 13  EE EO  2
        butterfly v20, v4, v12, v20
        // save EE 2
        add             a6, a6, t2
        vse32.v         v12, (a6)
        // reload E 5
        add             a7, a7, t2
        vle32.v         v12, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v20, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_10_2

        // cal E 1 14  EE EO  1
        butterfly v18, v6, v14, v18
        // save EE 1
        add             a6, a6, t2
        vse32.v         v14, (a6)
        // reload E 6
        add             a7, a7, t2
        vle32.v         v14, (a7)
        DCT32_4_DST_ADD_1_MEMBER      0, v18, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_6_1

        // cal  E 0 15  EE EO  0
        butterfly v16, v8, v22, v16
        // reload EE 0
        add             a6, a6, t2
        vse32.v         v22, (a6)
        // reload E 7
        add             a7, a7, t2
        vle32.v         v22, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v16, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_2_0

        // cal dst 18 22 26 30
        DCT32_4_DST_ADD_1_MEMBER      1, v0, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_14_3
        // reload E 8   v0
        add             a7, a7, t2
        vle32.v         v0, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v20, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_10_2
        // reload E 9     v20
        add             a7, a7, t2
        vle32.v         v20, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v18, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_6_1
        // reload E 10     v18
        add             a7, a7, t2
        vle32.v         v18, (a7)

        DCT32_4_DST_ADD_1_MEMBER      0, v16, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_2_0


        // cal  E 7 8  EE EO  7
        butterfly v22, v0, v16, v22
        // reload E 11     v0
        add             a7, a7, t2
        vle32.v         v0, (a7)
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_30_7
        DCT32_4_DST_ADD_1_MEMBER      0, v22, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_30_7

        // cal  E 6 9  EE EO  6
        butterfly v14, v20, v22, v14
        // reload EE 0  v20
        vle32.v         v20, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v14, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_26_6
        DCT32_4_DST_ADD_1_MEMBER      0, v14, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_26_6

        // cal E 5 10  EE EO  5
        butterfly v12, v18, v14, v12

        // reload EE 1  v18
        sub             a6, a6, t2
        vle32.v         v18, (a6)

        DCT32_4_DST_ADD_1_MEMBER      0, v12, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_22_5
        DCT32_4_DST_ADD_1_MEMBER      0, v12, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_22_5
        // load EE 1  v18

        // cal  E 4 11  EE EO  4
        butterfly v10, v0, v12, v10
        // reload EE 2  v18
        sub             a6, a6, t2
        vle32.v         v0, (a6)
        DCT32_4_DST_ADD_1_MEMBER      0, v10, 0, v24 v26 v28 v30, DCT32_EO_CONSTANT_18_4
        DCT32_4_DST_ADD_1_MEMBER      0, v10, 4, v2 v4 v6 v8, DCT32_EO_CONSTANT_18_4
        // reload EE 3  v10
        sub             a6, a6, t2
        vle32.v         v10, (a6)

        //write dst 2 6 10 14 18 22 26 30
        vsetvli zero, zero, e16, m1, ta, ma
        DCT32_STORE_L   2, \shift, v24
        DCT32_STORE_L   6, \shift, v26
        DCT32_STORE_L   10, \shift, v28
        DCT32_STORE_L   14, \shift, v30

        DCT32_STORE_L   18, \shift, v2
        DCT32_STORE_L   22, \shift, v4
        DCT32_STORE_L   26, \shift, v6
        DCT32_STORE_L   30, \shift, v8

        vsetvli zero, zero, e32, m2, ta, ma
        //  EE 0-7 ready in register

        // EE 3 4 EEE EEO 3
        butterfly       v10, v12, v28, v26
        // EE 1 6 EEE EEO 1
        butterfly       v18, v22, v24, v22
        // EE 2 5 EEE EEO 2
        butterfly       v0, v14, v30, v10
        // EE 0 7       EEE EEO 0
        butterfly v20, v16, v14, v12


        // EEO[0-4] v12 v22 v16 v26
        //dst 4 12 20 28
        DCT32_EEO_CAL   v4, 89, 75, 50, 18, v12, v22, v10, v26, 4, \shift
        DCT32_EEO_CAL   v8, 75, -18, -89, -50, v12, v22, v10, v26, 12, \shift
        DCT32_EEO_CAL   v6, 50, -89, 18, 75, v12, v22, v10, v26, 20, \shift
        DCT32_EEO_CAL   v16, 18, -50, 75, -89, v12, v22, v10, v26, 28, \shift

        vsetvli         zero, zero, e16, m1, ta, ma

        DCT32_STORE_L   4, \shift, v4
        DCT32_STORE_L   12, \shift, v8
        DCT32_STORE_L   20, \shift, v6
        DCT32_STORE_L   28, \shift, v16

        vsetvli         zero, zero, e32, m2, ta, ma
        # EEEE[0] = EEE[0] + EEE[3];
        # EEEO[0] = EEE[0] - EEE[3];
        butterfly       v14, v28, v16, v20
        # EEEE[1] = EEE[1] + EEE[2];
        # EEEO[1] = EEE[1] - EEE[2];
        butterfly       v24, v30, v2, v4


        # dst[0] = (int16_t)((g_t32[0][0] * EEEE[0] + g_t32[0][1] * EEEE[1] + add) >> shift);
        // 64 64
        li              a2, 64
        li              a3, 64
        vmul.vx         v18, v16, a2
        vmacc.vx        v18, a3, v2
        # dst[8 * line] = (int16_t)((g_t32[8][0] * EEEO[0] + g_t32[8][1] * EEEO[1] + add) >> shift);
        // 83  36
        li              a2, 83
        li              a3, 36
        vmul.vx         v6, v20, a2
        vmacc.vx        v6, a3, v4
        # dst[16 * line] = (int16_t)((g_t32[16][0] * EEEE[0] + g_t32[16][1] * EEEE[1] + add) >> shift);
        // 64  -64
        li              a2, 64
        li              a3, -64
        vmul.vx         v8, v16, a2
        vmacc.vx        v8, a3, v2
        # dst[24 * line] = (int16_t)((g_t32[24][0] * EEEO[0] + g_t32[24][1] * EEEO[1] + add) >> shift);
        // 36 -83
        li              a2, 36
        li              a3, -83
        vmul.vx         v10, v20, a2
        vmacc.vx        v10, a3, v4

        //write dst 0 8 16 24
        vsetvli         zero, zero, e16, m1, ta, ma
        DCT32_STORE_L   0, \shift, v18
        DCT32_STORE_L   8, \shift, v6
        DCT32_STORE_L   16, \shift, v8
        DCT32_STORE_L   24, \shift, v10

        ret
endfunc
.endm

tr_32xN_rvv firstpass, dct32_shift_1
tr_32xN_rvv secondpass, dct32_shift_2

.macro DCT_N size
function PFX(dct_\size\()_v_opt)
        .option arch, +zba

        addi    sp, sp, -16
        sx      ra, (sp)

        mv      t6, a1
        csrwi   vxrm, 0

        li     t1, 32
        vsetvli t4, t1, e16, m1, ta, ma

        li      t0, 4096
        // temp stack address
        sub     t5, sp, t0
        li      t0, 2048
        sub     sp, t5, t0

        // a0
        mv      a1, sp
        slli    t3, a2, 1
1:
        jal     func_tr_32xN_firstpass_rvv
        mul     t0, t4, t3
        add     a0, a0, t0
        slli    t0, t4, 1
        add     a1, a1, t0
        sub     t1, t1, t4
        bnez    t1, 1b

        li      t1, 32
        mv      a0, sp
        mv      a1, t6
        li      t3, 64
1:
        jal     func_tr_32xN_secondpass_rvv
        slli    t0, t4, 6
        add     a0, a0, t0
        slli    t0, t4, 1
        add     a1, a1, t0
        sub     t1, t1, t4
        bnez    t1, 1b

2:
        li      t0, 4096+2048
        add     sp, sp, t0
        lx      ra, (sp)
        addi    sp, sp, 16

        ret
endfunc
.endm

DCT_N 32
