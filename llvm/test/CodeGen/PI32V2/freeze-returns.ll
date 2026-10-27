; RUN: opt -S -mtriple=pi32v2 -passes='default<O2>' < %s | FileCheck %s
; RUN: opt -S -mtriple=pi32v2 -passes='lto<O2>' < %s | FileCheck %s

; A function that falls off its end without a return (the undef incoming
; value) returns an unknown value, not undefined behaviour: the caller's test
; of it, and the initialisation it guards, stay (JieLi's compiler keeps them;
; the AC79 SDK's Wi-Fi driver is set up that way).

@flag = internal constant i8 0
declare void @init_adapter()

define internal i32 @mem_init() noinline {
entry:
  %f = load i8, ptr @flag
  %on = icmp ne i8 %f, 0
  br i1 %on, label %work, label %exit
work:
  br label %exit
exit:
  %r = phi i32 [ 0, %work ], [ undef, %entry ]
  ret i32 %r
}

; CHECK-LABEL: define {{.*}}@probe(
; CHECK: call void @init_adapter()
define i32 @probe() {
  %s = call i32 @mem_init()
  %ok = icmp eq i32 %s, 0
  br i1 %ok, label %go, label %fail
go:
  call void @init_adapter()
  ret i32 0
fail:
  ret i32 -1
}
