; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; A signed 64-bit division by a zero-extended divisor known to be non-negative
; (zext nneg, or a value tracking proves) is the hardware divide, not a
; __divdi3 call (code in RAM cannot call the flash library).

; CHECK-LABEL: nneg:
; CHECK-NOT:   __divdi3
; CHECK:       (s)
define i64 @nneg(i64 %x, i32 %y) {
  %n = and i32 %y, 2147483647
  %z = zext nneg i32 %n to i64
  %q = sdiv i64 %x, %z
  ret i64 %q
}

; CHECK-LABEL: tracked:
; CHECK-NOT:   __divdi3
; CHECK:       (s)
define i64 @tracked(i64 %x, i32 %y) {
  %a = call i32 @llvm.abs.i32(i32 %y, i1 true)
  %n = add nuw nsw i32 %a, 16777216
  %z = zext i32 %n to i64
  %q = sdiv i64 %x, %z
  ret i64 %q
}

; not known non-negative: the library call
; CHECK-LABEL: unknown:
; CHECK:       __divdi3
define i64 @unknown(i64 %x, i32 %y) {
  %z = zext i32 %y to i64
  %q = sdiv i64 %x, %z
  ret i64 %q
}

declare i32 @llvm.abs.i32(i32, i1)
