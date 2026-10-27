; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O0 < %s | FileCheck %s

; A frame with a variable-sized object is addressed from r7: a slot in reach
; of [r7+n] is loaded and stored there, with no register for its address
; (none might be free).

declare void @use(ptr, ptr)

; CHECK-LABEL: dyn:
; CHECK:      r7 = sp
; CHECK-NEXT: [r7+0] = r1
; CHECK:      r1 = r7 + 0
; CHECK-NEXT: call use
define void @dyn(i32 %n, i32 %x) {
  %slot = alloca i32, align 4
  store volatile i32 %x, ptr %slot, align 4
  %buf = alloca i8, i32 %n, align 4
  call void @use(ptr %buf, ptr %slot)
  ret void
}
