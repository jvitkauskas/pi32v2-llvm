# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj %s -o %t
# RUN: llvm-objdump -d %t | FileCheck %s
# Block forms as the SDK's inline assembly writes them (JieLi 20260730.1 assembles this file to the same bytes):
# the condition without spaces, rep and iff nested in one another, a compare branch after the block.
# The compiler passes { and } through unchanged (clang/test/CodeGen/pi32v2-inline-asm.c).

r0 = 0
r1 = 127
r1 += r4
if(r1<0){
r1 = 0
}
if(r1>254){
r1 = 254
}
ifs(r1<0){
r1 = 0
}
ifs(r1>254){
r1 = 254
}
# CHECK: 6: a1 e9 00 00{{ *}}if (r1 < 0) {
# CHECK: a: 41 20{{ *}}r1 = 0
# CHECK: c: 21 ec fe 00{{ *}}if (r1 > 254) {
# CHECK: 10: 79 3e{{ *}}r1 = 254
# CHECK: 12: a1 ed 00 00{{ *}}ifs (r1 < 0) {
# CHECK: 18: 21 ee fe 00{{ *}}ifs (r1 > 254) {

1:
rep r1 {
r0 = [r2++=4]
r0 = fabs(r0)
iff(r0 > r3) {
r3 = r0
}
}
if(r1>0) goto 1b
if(r3!=r0) {
r3 = uextra(r3,p:23,l:8)
r3 += -127
}
# CHECK: rep 12 r1 {
# CHECK: r0 = [r2++=4]
# CHECK: r0 = r0 & 0x7FFFFFFF
# CHECK: iff (r0 > r3) {
# CHECK: if (r1 > 0) goto
# CHECK: if (r3 != r0) {
# CHECK: r3 = uextra(r3, p:23, l:8)
# CHECK: r3 += -127
