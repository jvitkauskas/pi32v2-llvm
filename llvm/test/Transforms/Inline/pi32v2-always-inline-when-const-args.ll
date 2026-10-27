; RUN: opt -S -passes=inline < %s | FileCheck %s

; JieLi's always_inline_when_const_args: inlined where every argument is a
; constant (a global's address too), whatever the cost; not otherwise.

@g = global [4 x i32] zeroinitializer
declare void @sink(i32)

define void @big(i32 %a, ptr %p) "always-inline-when-const-args" {
  switch i32 %a, label %d [ i32 1, label %one
                            i32 2, label %two ]
one:
  call void @sink(i32 11)
  call void @sink(i32 12)
  call void @sink(i32 13)
  br label %d
two:
  call void @sink(i32 21)
  call void @sink(i32 22)
  call void @sink(i32 23)
  br label %d
d:
  %v = load i32, ptr %p
  call void @sink(i32 %v)
  call void @sink(i32 %a)
  call void @sink(i32 31)
  call void @sink(i32 32)
  call void @sink(i32 33)
  call void @sink(i32 34)
  ret void
}

; CHECK-LABEL: @all_const(
; CHECK-NOT: call void @big
; CHECK: ret void
define void @all_const() minsize optsize {
  call void @big(i32 1, ptr @g)
  ret void
}

; CHECK-LABEL: @one_var(
; CHECK: call void @big(i32 %x, ptr @g)
define void @one_var(i32 %x) minsize optsize {
  call void @big(i32 %x, ptr @g)
  ret void
}
