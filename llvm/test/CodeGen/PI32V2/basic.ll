; RUN: llc -mtriple=pi32v2 -O2 < %s | FileCheck %s
; JieLi's convention (pi32v2-isa docs/ABI.md): arguments in r0..r3 then at sp,
; results in r0/r1, r4..r15 and rets saved by one push, the pop returns.

; CHECK-LABEL: add:
; CHECK:      r0 = r0 + r1
; CHECK-NEXT: rts
define i32 @add(i32 %a, i32 %b) nounwind {
  %r = add i32 %a, %b
  ret i32 %r
}

; CHECK-LABEL: max:
; CHECK:      r0 = smax(r0, r1)
; CHECK-NEXT: rts
define i32 @max(i32 %a, i32 %b) nounwind {
  %c = icmp sgt i32 %a, %b
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; CHECK-LABEL: loads:
; CHECK:      r2 = [r0+12]
; CHECK:      [r0+12] = r1
; CHECK:      r0 = b[r0+1] (s)
define i32 @loads(ptr %p, i32 %v) nounwind {
  %q = getelementptr i32, ptr %p, i32 3
  %old = load i32, ptr %q
  store i32 %v, ptr %q
  %b = getelementptr i8, ptr %p, i32 1
  %c = load i8, ptr %b
  %e = sext i8 %c to i32
  %r = add i32 %old, %e
  ret i32 %r
}

declare void @escape(ptr)
; As JieLi's compiler (docs/ABI.md section 6.1): rets pushed, eight bytes for
; a five-byte array, its address, the call, the pop into pc. JieLi's puts the
; array at sp+3; a local of a word or more is word-aligned here (align-allocas.ll).
; CHECK-LABEL: frame:
; CHECK:      [--sp] = rets
; CHECK-NEXT: sp += -8
; CHECK-NEXT: r0 = sp + 0
; CHECK-NEXT: call escape
; CHECK-NEXT: sp += 8
; CHECK-NEXT: pc = [sp++]
define void @frame() nounwind {
  %a = alloca [5 x i8]
  call void @escape(ptr %a)
  ret void
}

declare i32 @many(i32, i32, i32, i32, i32, i32)
; CHECK-LABEL: stackargs:
; CHECK:      r0 = 6
; CHECK-NEXT: [sp+4] = r0
; CHECK-NEXT: r0 = 5
; CHECK-NEXT: [sp] = r0
; CHECK:      r3 = 4
; CHECK:      call many
define i32 @stackargs() nounwind {
  %r = call i32 @many(i32 1, i32 2, i32 3, i32 4, i32 5, i32 6)
  ret i32 %r
}

@g = global i32 7
; CHECK-LABEL: global:
; CHECK:      r0 = g
; CHECK-NEXT: r0 = [r0+0]
define i32 @global() nounwind {
  %v = load i32, ptr @g
  ret i32 %v
}
