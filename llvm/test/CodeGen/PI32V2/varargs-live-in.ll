; A variadic function pushes the argument registers its named arguments did not use. They are live on entry, whether
; or not the body reads them: the machine verifier took the push for a use of undefined registers.
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

declare void @llvm.va_start(ptr)
declare void @llvm.va_end(ptr)

; CHECK-LABEL: first:
; CHECK:       [--sp] = {r3-r1}
define i32 @first(i32 %n, ...) {
  %ap = alloca ptr
  call void @llvm.va_start(ptr %ap)
  %v = va_arg ptr %ap, i32
  call void @llvm.va_end(ptr %ap)
  ret i32 %v
}
