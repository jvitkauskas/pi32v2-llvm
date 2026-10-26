; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

; No vector registers: vector values (SROA makes a struct copy a <6 x i32>,
; and JieLi's bitcode may be vectorized) are split into words, compares too.

; CHECK-LABEL: copy_if:
; CHECK-COUNT-6: if ((r2 & 0x1) != 0) {
; CHECK: rts
define void @copy_if(ptr %d, ptr %s, i1 %c) {
  %v = load <6 x i32>, ptr %s, align 4
  %o = load <6 x i32>, ptr %d, align 4
  %r = select i1 %c, <6 x i32> %v, <6 x i32> %o
  store <6 x i32> %r, ptr %d, align 4
  ret void
}

; CHECK-LABEL: lanes:
; CHECK-COUNT-4: = smin(
; CHECK: rts
define <4 x i32> @lanes(<4 x i32> %a, <4 x i32> %b) {
  %c = icmp slt <4 x i32> %a, %b
  %m = select <4 x i1> %c, <4 x i32> %a, <4 x i32> %b
  ret <4 x i32> %m
}
