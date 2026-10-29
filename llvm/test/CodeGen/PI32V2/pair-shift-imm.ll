; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; A 64-bit shift by a constant under 32 is the pair shift (r1_r0 <<= 24).

; CHECK-LABEL: shl24:
; CHECK:       r1_r0 <<= 24
define i64 @shl24(i64 %x) {
  %r = shl i64 %x, 24
  ret i64 %r
}

; CHECK-LABEL: lshr5:
; CHECK:       r1_r0 >>= 5
define i64 @lshr5(i64 %x) {
  %r = lshr i64 %x, 5
  ret i64 %r
}

; CHECK-LABEL: ashr31:
; CHECK:       r1_r0 >>>= 31
define i64 @ashr31(i64 %x) {
  %r = ashr i64 %x, 31
  ret i64 %r
}

; 32 and over are word moves
; CHECK-LABEL: shl32:
; CHECK-NOT:   <<=
define i64 @shl32(i64 %x) {
  %r = shl i64 %x, 32
  ret i64 %r
}
