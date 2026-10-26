; RUN: opt -S -passes=dse -mtriple=pi32v2 < %s | FileCheck %s
; RUN: opt -S -passes=dse -mtriple=riscv32-unknown-elf < %s 2>&1 | FileCheck %s --check-prefix=RV32

; pi32v2 firmware brings its own allocator, and JieLi's compiler never forms
; calloc: a zalloc's malloc and memset are not folded into a calloc call (a
; calloc that is this zalloc would call itself).

declare noalias ptr @malloc(i32) allockind("alloc,uninitialized") allocsize(0)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; CHECK-LABEL: @zalloc(
; CHECK: call ptr @malloc(
; CHECK: call void @llvm.memset
; CHECK-NOT: calloc
; RV32-LABEL: @zalloc(
; RV32: call ptr @calloc(
define ptr @zalloc(i32 %n) {
  %p = call ptr @malloc(i32 %n)
  %z = icmp eq ptr %p, null
  br i1 %z, label %done, label %clear
clear:
  call void @llvm.memset.p0.i32(ptr %p, i8 0, i32 %n, i1 false)
  br label %done
done:
  ret ptr %p
}
