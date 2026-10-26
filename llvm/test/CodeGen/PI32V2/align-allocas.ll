; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

; The stack is word-aligned and not realigned: a local that needs more gets
; room for its alignment, and its address is rounded up in it.

declare void @g(ptr)

; CHECK-LABEL: f16:
; CHECK:      sp += -28
; CHECK:      r0 = r0 + 15
; CHECK-NEXT: r0 = r0 & 0xFFFFFFF0
; CHECK-NEXT: call g
define void @f16() {
  %buf = alloca [16 x i8], align 16
  call void @llvm.lifetime.start.p0(ptr %buf)
  call void @g(ptr %buf)
  call void @llvm.lifetime.end.p0(ptr %buf)
  ret void
}

; An i64 local aligned to 8 by other IR: its address is rounded up to 8.
; CHECK-LABEL: hi:
; CHECK:      sp += -12
; CHECK:      & 0xFFFFFFF8
; CHECK:      call g
define i32 @hi() {
  %r = alloca i64, align 8
  call void @g(ptr %r)
  %p = getelementptr inbounds i8, ptr %r, i32 4
  %v = load i32, ptr %p, align 4
  ret i32 %v
}
