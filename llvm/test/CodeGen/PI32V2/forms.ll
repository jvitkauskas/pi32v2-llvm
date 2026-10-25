; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
; The forms JieLi's compiler uses that the code generator now picks too: short
; push/pop ranges, compare-with-constant branches, tail calls, hardware float,
; the carry chain and the pair multiply; and frames past the sp forms' reach.

declare i32 @g(i32)
declare void @use(ptr)

; CHECK-LABEL: tail:
; CHECK:      r0 = r0 + 1
; CHECK-NEXT: goto g
define i32 @tail(i32 %a) nounwind {
  %b = add i32 %a, 1
  %r = tail call i32 @g(i32 %b)
  ret i32 %r
}

; CHECK-LABEL: saves:
; CHECK:      [--sp] = {rets, r4}
; CHECK:      call g
; CHECK:      {pc, r4} = [sp++]
define i32 @saves(i32 %a) nounwind {
  %r = call i32 @g(i32 %a)
  %s = add i32 %r, %a
  ret i32 %s
}

; CHECK-LABEL: brimm:
; CHECK:      ifs (r0 <= 4) {
; CHECK-NEXT: r0 = 1
; CHECK-NEXT: rts
; CHECK-NEXT: }
; CHECK:      goto g
define i32 @brimm(i32 %a) nounwind {
  %c = icmp slt i32 %a, 5
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  %r = tail call i32 @g(i32 %a)
  ret i32 %r
}

; CHECK-LABEL: fadd:
; CHECK:      r0 = r0 + r1 (f)
; CHECK-NEXT: rts
define float @fadd(float %a, float %b) nounwind {
  %r = fadd float %a, %b
  ret float %r
}

; CHECK-LABEL: fconv:
; CHECK:      r0 = itof(r0)
; CHECK:      r0 = r0 * r1 (f)
; CHECK:      r0 = ftoi(r0) (trunc)
define i32 @fconv(i32 %a) nounwind {
  %f = sitofp i32 %a to float
  %m = fmul float %f, 1.5
  %r = fptosi float %m to i32
  ret i32 %r
}

; CHECK-LABEL: fbr:
; CHECK:      iff (r0 < r1) {
define i32 @fbr(float %a, float %b) nounwind {
  %c = fcmp olt float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 2
}

; CHECK-LABEL: add64:
; CHECK:      r0 = r0 + r2
; CHECK-NEXT: r1 = r1 + r3 + c
define i64 @add64(i64 %a, i64 %b) nounwind {
  %r = add i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: sub64:
; CHECK:      r0 = r0 - r2
; CHECK-NEXT: r1 = r1 - r3 - !c
define i64 @sub64(i64 %a, i64 %b) nounwind {
  %r = sub i64 %a, %b
  ret i64 %r
}

; CHECK-LABEL: mulw:
; CHECK:      r1_r0 = r0 * r1 (u)
; CHECK-NEXT: rts
define i64 @mulw(i32 %a, i32 %b) nounwind {
  %x = zext i32 %a to i64
  %y = zext i32 %b to i64
  %r = mul i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: retaddr:
; CHECK:      = rets
define ptr @retaddr() nounwind {
  call void @use(ptr null)
  %r = call ptr @llvm.returnaddress(i32 0)
  ret ptr %r
}
declare ptr @llvm.returnaddress(i32)

; A slot beyond r = sp + n's reach: its address from sp in steps.
; CHECK-LABEL: big:
; CHECK:      call use
; CHECK-NEXT: r0 = sp{{$}}
; CHECK-NEXT: r0 = r0 + 6004
define i32 @big() nounwind {
  %a = alloca [2000 x i32], align 4
  call void @use(ptr %a)
  %p = getelementptr [2000 x i32], ptr %a, i32 0, i32 1500
  %v = load i32, ptr %p
  ret i32 %v
}

; A select is an if block: the false value first, then the true one under the
; condition (no branch).
; CHECK-LABEL: sel:
; CHECK:      r2 = 0
; CHECK-NEXT: ifs (r0 < r1) {
; CHECK-NEXT: r2 = 1
; CHECK-NEXT: }
define i32 @sel(i32 %a, i32 %b) nounwind {
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i32
  %s = add i32 %r, %a
  ret i32 %s
}

; Short conditional code is an if block too (if blocks pass).
; CHECK-LABEL: cond:
; CHECK:      if (r0 != 0) {
; CHECK-NEXT: goto g
; CHECK-NEXT: }
define i32 @cond(i32 %a) nounwind {
  %c = icmp eq i32 %a, 0
  br i1 %c, label %z, label %n
z:
  ret i32 5
n:
  %r = tail call i32 @g(i32 %a)
  ret i32 %r
}
