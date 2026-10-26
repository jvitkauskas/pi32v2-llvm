; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

; A struct passed by value (byval, as JieLi's clang emits it) is one pointer
; word to a copy the caller makes in its own frame (pi32v2-isa docs/ABI.md,
; section 5.4): in the argument's register, or in its stack slot after r0-r3.
; The callee uses the pointer and may write to the copy.

%struct.S = type { [5 x i32] }
%struct.B = type { [3 x i8] }

declare void @use(i32, ptr byval(%struct.S) align 4, i32, ptr byval(%struct.B) align 1)
declare void @use5(i32, i32, i32, i32, ptr byval(%struct.S) align 4)

; CHECK-LABEL: call:
; CHECK:      sp += -24
; CHECK:      [sp+20] =
; CHECK:      [sp+4] =
; CHECK:      b[sp+3] =
; CHECK-DAG:  r1 = sp + 4
; CHECK-DAG:  r3 = sp + 1
; CHECK-DAG:  r0 = 1
; CHECK-DAG:  r2 = 2
; CHECK:      call use
define void @call(ptr %p, ptr %q) {
  call void @use(i32 1, ptr byval(%struct.S) align 4 %p, i32 2, ptr byval(%struct.B) align 1 %q)
  ret void
}

; CHECK-LABEL: call5:
; CHECK:      r0 = sp + 4
; CHECK-NEXT: [sp] = r0
; CHECK:      call use5
define void @call5(ptr %p) {
  call void @use5(i32 1, i32 2, i32 3, i32 4, ptr byval(%struct.S) align 4 %p)
  ret void
}

; A copy too large to inline is made by memcpy, before the call's arguments.
%struct.L = type { [64 x i32] }
declare void @useL(ptr byval(%struct.L) align 4)

; CHECK-LABEL: callL:
; CHECK:      sp += -256
; CHECK:      [[COPY:r[0-9]+]] = sp + 0
; CHECK:      r2 = 256
; CHECK-NEXT: call memcpy
; CHECK-NEXT: r0 = [[COPY]]
; CHECK-NEXT: call useL
define void @callL(ptr %p) {
  call void @useL(ptr byval(%struct.L) align 4 %p)
  ret void
}

; The callee writes to the copy through the pointer it was given.
; CHECK-LABEL: take:
; CHECK:      [r1+4] =
define i32 @take(i32 %x, ptr byval(%struct.S) align 4 %s, i32 %y) {
  %a1 = getelementptr inbounds i32, ptr %s, i32 1
  %v1 = load i32, ptr %a1
  %n = add i32 %v1, %y
  store i32 %n, ptr %a1
  %v0 = load i32, ptr %s
  %r = add i32 %n, %v0
  %r2 = add i32 %r, %x
  ret i32 %r2
}

; The pointer to a fifth argument's copy is read from the caller's outgoing
; area.
; CHECK-LABEL: take5:
; CHECK:      r0 = [sp]
; CHECK-NEXT: r0 = [r0+16]
define i32 @take5(i32 %a, i32 %b, i32 %c, i32 %d, ptr byval(%struct.S) align 4 %s) {
  %a4 = getelementptr inbounds i32, ptr %s, i32 4
  %v = load i32, ptr %a4
  ret i32 %v
}
