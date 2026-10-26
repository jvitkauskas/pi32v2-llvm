; RUN: llc -mtriple=pi32v2 -mcpu=r3 -pi32v2-large-program=true -filetype=obj < %s \
; RUN:   | llvm-objdump -dr - | FileCheck %s
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s \
; RUN:   | llvm-objdump -dr - | FileCheck %s --check-prefix=SMALL

; JieLi's -pi32v2-large-program: a call the assembler leaves to the linker is
; the 6-byte call.xl (R_PI32V2_EXTEND_CALL_32M2), one it resolves stays short,
; and a call in tail position stays a call.

declare void @far(i32)

define internal void @loc(i32 %x) noinline {
  call void @far(i32 %x)
  ret void
}

; CHECK-LABEL: <f>:
; CHECK:      80 ff 00 00 00 00 call 0
; CHECK-NEXT: R_PI32V2_EXTEND_CALL_32M2 far
; CHECK:      call
; CHECK-NOT:  R_PI32V2
; CHECK:      80 ff 00 00 00 00 call 0
; CHECK-NEXT: R_PI32V2_EXTEND_CALL_32M2 far
; CHECK-NEXT: {pc, r4} = [sp++]
; SMALL-LABEL: <f>:
; SMALL:      R_PI32V2_LONG_JUMP_23M2 far
; SMALL:      goto 0
; SMALL-NEXT: R_PI32V2_LONG_JUMP_23M2 far
define void @f(i32 %x) {
  call void @far(i32 %x)
  call void @loc(i32 %x)
  tail call void @far(i32 2)
  ret void
}

; CHECK-LABEL: <g>:
; CHECK:      call r1
; CHECK-NEXT: pc = [sp++]
define void @g(ptr %p) {
  tail call void %p(i32 3)
  ret void
}
