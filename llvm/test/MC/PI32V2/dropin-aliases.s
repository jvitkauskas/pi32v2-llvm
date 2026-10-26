# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 --show-encoding %s | FileCheck %s
# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj %s -o %t
# RUN: llvm-objdump -d %t | FileCheck %s --check-prefix=OBJ
# Oracle-probed aliases: JieLi 20260730.1, -mcpu=r3.

[r5++] = r6
# CHECK: [r5++=4] = r6 ; encoding: [0xd6,0x05]

[r2--] = r3
# CHECK: [r2++=-4] = r3 ; encoding: [0xab,0x05]

h[r0++] = r2
# CHECK: h[r0++=2] = r2 ; encoding: [0x82,0x06]

h[r3--] = r5
# CHECK: h[r3++=-2] = r5 ; encoding: [0xbd,0x06]

b[r2++] = r4
# CHECK: b[r2++=1] = r4 ; encoding: [0xa4,0x07]

r1 = h[r0++] (u)
# CHECK: r1 = h[r0++=2] (u) ; encoding: [0x01,0x06]

r3 = h[r0++] (s)
# CHECK: r3 = h[r0++=2] (s) ; encoding: [0xd4,0xed,0x02,0x30]

r9 = h[r10--] (s)
# CHECK: r9 = h[r10++=-2] (s) ; encoding: [0xd7,0xed,0xae,0x9f]

r1 = b[r4++] (s)
# CHECK: r1 = b[r4++=1] (s) ; encoding: [0xd4,0xee,0x41,0x10]

r12 = b[r15--] (s)
# CHECK: r12 = b[r15++=-1] (s) ; encoding: [0xd5,0xee,0xff,0xcf]

r6 = -r6
# CHECK: r6 = 0x0 - r6 ; encoding: [0xa6,0xe0,0x00,0x60]

r11 = -r12
# CHECK: r11 = 0x0 - r12 ; encoding: [0xab,0xe0,0x00,0xc0]

r1 = fabs(r1)
# CHECK: r1 = r1 & 0x7FFFFFFF ; encoding: [0x71,0xe1,0x00,0x14]

r12 = fabs(r9)
# CHECK: r12 = r9 & 0x7FFFFFFF ; encoding: [0x7c,0xe1,0x00,0x94]

r5_r4.h = r2
# CHECK: r5 = r2 ; encoding: [0x25,0x16]

r3_r2.l = r5_r4.h
# CHECK: r2 = r5 ; encoding: [0x52,0x16]

r15_r14.l = uextra(r0,p:0,l:23)
# CHECK: r14 = uextra(r0, p:0, l:23) ; encoding: [0xbe,0xe1,0x5c,0x00]

r0 = r3_r2 >>> 14 (round)
# CHECK: r0 = r3_r2 >>> 14 (round) ; encoding: [0xd2,0xe1,0x2e,0x0c]

r5_r4.l = r0 >< r1 (sat)
# CHECK: r4 = r0 >< r1 (sat) ; encoding: [0xcc,0xe1,0x03,0x41]

r1_r0 = copex(r3_r2) (23)
# CHECK: r1_r0 = copex(r3_r2) (23) ; encoding: [0xff,0xe1,0x17,0x02]

r15_r14 = copex(r13_r12) (11)
# CHECK: r15_r14 = copex(r13_r12) (11) ; encoding: [0xff,0xe1,0x0b,0xec]

r7_r6 = cmpu(r7_r6 < r3_r2) (v)
# CHECK: r7_r6 = cmp(r7_r6 < r3_r2) (v) ; encoding: [0x3c,0xe5,0x63,0x62]

r5_r4 = r12*r0 (s)
# CHECK: r5_r4 = r12 * r0 (s) ; encoding: [0xf8,0xe1,0xc0,0x50]

r3_r2 += r7*r7 (s)
# CHECK: r3_r2 += r7 * r7 (s) ; encoding: [0xfc,0xe1,0x70,0x37]

r5_r4 = h[r1++]*r2 (s)
# CHECK: r5_r4 = h[r1 ++= 2]*r2 (s) ; encoding: [0xe8,0xee,0x11,0x52]

r9_r8 += h[r15++=2]*r5 (s)
# CHECK: r9_r8 += h[r15 ++= 2]*r5 (s) ; encoding: [0xec,0xee,0xf1,0x95]

r5_r4 = [r0--]*[r7++] (s)
# CHECK: r5_r4 = [r0 ++= -4]*[r7 ++= 4] (s) ; encoding: [0x61,0xe9,0x0f,0x57]

r3_r2 = h[r8++=2]*[r1++=0] (s)
# CHECK: r3_r2 = h[r8 ++= 2]*[r1 ++= 0] (s) ; encoding: [0xe0,0xec,0x81,0x31]

r3_r2 = [r0++=0]*[r2++] (s)
# CHECK: r3_r2 = [r0 ++= 0]*[r2 ++= 4] (s) ; encoding: [0x61,0xe9,0x00,0x32]

r5_r4 += [r0++]*[r7--] (s)
# CHECK: r5_r4 += [r0 ++= 4]*[r7 ++= -4] (s) ; encoding: [0xef,0xe9,0x01,0x57]

r15_r14 = h[r8--]*r9 (u)
# CHECK: r15_r14 = h[r8 ++= -2]*r9 (u) ; encoding: [0xe8,0xee,0x8f,0xe9]

r1 = 1.f
# CHECK: r1 = 0x3F800000 ; encoding: [0x60,0xe0,0x7e,0x15]

r2 = 10.f
# CHECK: r2 = 1092616192 ; encoding: [0xc2,0xff,0x00,0x00,0x20,0x41]

r3 = 1.5f
# CHECK: r3 = 0x3FC00000 ; encoding: [0x60,0xe0,0x7f,0x35]

r4 = -1.f
# CHECK: r4 = 3212836864 ; encoding: [0xc4,0xff,0x00,0x00,0x80,0xbf]

r9 -= 1 # r4 = [r1++=4]
# CHECK: r9 = r9 - 0x1  # ; encoding: [0xf9,0xf0,0x01,0x90]
# CHECK: r4 = [r1++=4] ; encoding: [0x14,0x05]

r2 = -r1 #
r1 = [r0]
# CHECK: r2 = 0x0 - r1  # ; encoding: [0xa2,0xf0,0x00,0x10]
# CHECK: r1 = [r0+0] ; encoding: [0x01,0x60]

r12 = fabs(r9) # r0 = [r1++]
# CHECK: r12 = r9 & 0x7FFFFFFF  # ; encoding: [0x7c,0xf1,0x00,0x94]
# CHECK: r0 = [r1++=4] ; encoding: [0x10,0x05]

r4=-r4(f)
# CHECK: r4 = r4 ^ 0x80000000 ; encoding: [0x54,0xe1,0x00,0x44]

r11=-r12(f)
# CHECK: r11 = r12 ^ 0x80000000 ; encoding: [0x5b,0xe1,0x00,0xc4]

r1=r7_r6>>>7
# CHECK: r1 = r7_r6 >> 7 (s) ; encoding: [0xde,0xe1,0x67,0x18]

r2=r15_r14>>>r8
# CHECK: r2 = r15_r14 >> r8 (s) ; encoding: [0xdf,0xe1,0xe1,0x28]

r4=r4+|+|+|+r6(uavg,rnd)
# CHECK: r4 = r4 +|+|+|+ r6 (rnd, uavg) ; encoding: [0x20,0xe4,0x48,0x46]

r7><=r10
# CHECK: r7 = r7 >< r10 ; encoding: [0xcc,0xe1,0x72,0x7a]

r4><=r1(sat)
# CHECK: r4 = r4 >< r1 (sat) ; encoding: [0xcc,0xe1,0x43,0x41]

ifs(r6==32) {
nop
}
# CHECK: if (r6 == 32) { ; encoding:
# OBJ: 26 e8 20 00{{ *}}if (r6 == 32) {
# CHECK: nop ; encoding: [0x00,0x00]

ifs(r7!=0) {
nop
}
# CHECK: if (r7 != 0) { ; encoding:
# OBJ: a7 e8 00 00{{ *}}if (r7 != 0) {
# CHECK: nop ; encoding: [0x00,0x00]

1:
nop
ifs(--r10!=0)goto 1b
# CHECK: if (--r10 != 0) goto
# OBJ: 0a ea fd ff{{ *}}if (--r10 != 0) goto
ifs (--r3 != 0) goto 1b
# CHECK: if (--r3 != 0) goto
# OBJ: 03 ea fb ff{{ *}}if (--r3 != 0) goto
