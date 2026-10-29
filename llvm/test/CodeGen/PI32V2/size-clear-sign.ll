; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; x & 0x7fffffff (fabs of a float) is the 2-byte `r &= ~(1 << 31)` in r0-r7,
; not a bit field extract (4 bytes).

; CHECK-LABEL: abs_bits:
; CHECK:       r0 &= 2147483647
define i32 @abs_bits(i32 %x) {
  %r = and i32 %x, 2147483647
  ret i32 %r
}

; CHECK-LABEL: fabs:
; CHECK:       r0 &= 2147483647
define float @fabs(float %x) {
  %r = call float @llvm.fabs.f32(float %x)
  ret float %r
}

; other low masks stay field extracts
; CHECK-LABEL: low30:
; CHECK:       uextra(r0, p:0, l:30)
define i32 @low30(i32 %x) {
  %r = and i32 %x, 1073741823
  ret i32 %r
}

declare float @llvm.fabs.f32(float)
