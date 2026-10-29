; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; A select on a compare with zero of r0..r7 is a 2-byte branch over the move.

; a select on zero is a 2-byte branch over the move, not an if block
; CHECK-LABEL: selz:
; CHECK:       if (r0 == 0) goto
; CHECK-NOT:   {
define i32 @selz(i32 %x, i32 %y) {
  %c = icmp ne i32 %x, 0
  %r = select i1 %c, i32 %y, i32 7
  ret i32 %r
}
