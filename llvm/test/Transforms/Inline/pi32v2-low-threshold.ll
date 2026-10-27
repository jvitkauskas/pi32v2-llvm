; RUN: opt -S -passes=inline -inline-threshold=5 -mtriple=pi32v2 < %s | FileCheck %s
; REQUIRES: pi32v2-registered-target

; PI32V2 lowers the threshold by 40 optimizing for size; an explicit lower
; -inline-threshold (the AC79 SDK passes small ones) stops at 0.

define internal i32 @g(i32 %x) optsize {
  %m = mul i32 %x, 3
  %a = add i32 %m, 1
  ret i32 %a
}

; CHECK-LABEL: @f(
; CHECK: ret i32
define i32 @f(i32 %x) optsize {
  %a = call i32 @g(i32 %x)
  %b = call i32 @g(i32 %a)
  %s = add i32 %a, %b
  ret i32 %s
}
