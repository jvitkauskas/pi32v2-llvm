; RUN: llvm-as < %s | llvm-dis | FileCheck %s

; JieLi's clang emits its __builtin_pi32v2_<name> as llvm.pi32v2.<name>, and
; its optimizer forms llvm.pi32v2.shfms64; they become what this LLVM's clang
; emits for the builtins.

target triple = "pi32v2"

declare void @llvm.pi32v2.cli()
declare void @llvm.pi32v2.sti()
declare void @llvm.pi32v2.csync()
declare void @llvm.pi32v2.nop()
declare void @llvm.pi32v2.lockset()
declare void @llvm.pi32v2.flush(i32)
declare void @llvm.pi32v2.pfetch(i32)
declare i32 @llvm.pi32v2.clz(i32)
declare i32 @llvm.pi32v2.cls(i32)
declare i32 @llvm.pi32v2.popcount(i32)
declare i32 @llvm.pi32v2.rev8(i32)
declare i32 @llvm.pi32v2.zcmp(i32)
declare i32 @llvm.pi32v2.uextra(i32, i32, i32)
declare i32 @llvm.pi32v2.sextra(i32, i32, i32)
declare i32 @llvm.pi32v2.shfms64(i64, i32)

; CHECK-LABEL: define void @plain(i32 %a)
; CHECK-NEXT: call void asm sideeffect "cli", "~{memory}"()
; CHECK-NEXT: call void asm sideeffect "sti", "~{memory}"()
; CHECK-NEXT: call void asm sideeffect "csync", "~{memory}"()
; CHECK-NEXT: call void asm sideeffect "nop", ""()
; CHECK-NEXT: call void asm sideeffect "lockset", "~{memory}"()
; CHECK-NEXT: call void asm sideeffect "flush [$0]", "r,~{memory}"(i32 %a)
; CHECK-NEXT: call void asm sideeffect "pfetch [$0]", "r"(i32 %a)
define void @plain(i32 %a) {
  call void @llvm.pi32v2.cli()
  call void @llvm.pi32v2.sti()
  call void @llvm.pi32v2.csync()
  call void @llvm.pi32v2.nop()
  call void @llvm.pi32v2.lockset()
  call void @llvm.pi32v2.flush(i32 %a)
  call void @llvm.pi32v2.pfetch(i32 %a)
  ret void
}

; CHECK-LABEL: define i32 @bits(i32 %x)
; CHECK: call i32 @llvm.ctlz.i32(i32 %x, i1 false)
; CHECK: [[S:%.*]] = ashr i32 %x, 31
; CHECK: [[X:%.*]] = xor i32 %x, [[S]]
; CHECK: call i32 @llvm.ctlz.i32(i32 [[X]], i1 false)
; CHECK: call i32 @llvm.ctpop.i32(i32 %x)
; CHECK: call i32 @llvm.bswap.i32(i32 %x)
; CHECK: call i32 asm "$0 = zcmp($1)", "=r,r"(i32 %x)
define i32 @bits(i32 %x) {
  %a = call i32 @llvm.pi32v2.clz(i32 %x)
  %b = call i32 @llvm.pi32v2.cls(i32 %x)
  %c = call i32 @llvm.pi32v2.popcount(i32 %x)
  %d = call i32 @llvm.pi32v2.rev8(i32 %x)
  %e = call i32 @llvm.pi32v2.zcmp(i32 %x)
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %c
  %s3 = add i32 %s2, %d
  %s4 = add i32 %s3, %e
  ret i32 %s4
}

; The l bits from bit p; sextra sign-extends a field that ends inside the word.
; CHECK-LABEL: define i32 @fields(i32 %x)
; CHECK: [[U:%.*]] = lshr i32 %x, 3
; CHECK: and i32 [[U]], 31
; CHECK: [[L:%.*]] = shl i32 %x, 22
; CHECK: ashr i32 [[L]], 26
; CHECK: lshr i32 %x, 30
define i32 @fields(i32 %x) {
  %u = call i32 @llvm.pi32v2.uextra(i32 %x, i32 3, i32 5)
  %s = call i32 @llvm.pi32v2.sextra(i32 %x, i32 4, i32 6)
  %t = call i32 @llvm.pi32v2.sextra(i32 %x, i32 30, i32 8)
  %z = call i32 @llvm.pi32v2.uextra(i32 %x, i32 32, i32 4)
  %r1 = add i32 %u, %s
  %r2 = add i32 %r1, %t
  %r3 = add i32 %r2, %z
  ret i32 %r3
}

; shfms64: the pair shifted right arithmetically (count: the low 8 bits, at
; most 63) and clamped to the signed 32-bit range.
; CHECK-LABEL: define i32 @shf(i64 %x, i32 %n)
; CHECK: [[C:%.*]] = and i32 %n, 255
; CHECK: [[M:%.*]] = call i32 @llvm.umin.i32(i32 [[C]], i32 63)
; CHECK: [[Z:%.*]] = zext i32 [[M]] to i64
; CHECK: [[A:%.*]] = ashr i64 %x, [[Z]]
; CHECK: [[LO:%.*]] = call i64 @llvm.smax.i64(i64 [[A]], i64 -2147483648)
; CHECK: [[HI:%.*]] = call i64 @llvm.smin.i64(i64 [[LO]], i64 2147483647)
; CHECK: trunc i64 [[HI]] to i32
define i32 @shf(i64 %x, i32 %n) {
  %r = call i32 @llvm.pi32v2.shfms64(i64 %x, i32 %n)
  ret i32 %r
}

; CHECK-NOT: llvm.pi32v2
