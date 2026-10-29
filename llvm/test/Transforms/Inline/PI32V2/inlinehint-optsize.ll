; REQUIRES: pi32v2-registered-target
; RUN: opt -mtriple=pi32v2 -passes='require<profile-summary>,cgscc(inline)' -S %s | FileCheck %s
; RUN: opt -mtriple=pi32v2 -passes='require<profile-summary>,cgscc(inline)' -inlinehint-threshold=325 -S %s | FileCheck %s --check-prefix=EXPLICIT

; A hint must not duplicate a large helper into size-mode wrappers or their
; callers. Keep the speed-mode threshold, minsize policy, explicit tuning,
; tiny inlines, alwaysinline, and last-call-to-local inlining intact.

target triple = "pi32v2"

declare void @first()
declare void @second()

define i32 @hinted(i32 %x) inlinehint optsize "function-inline-cost"="275" {
  %cond = icmp eq i32 %x, 0
  br i1 %cond, label %zero, label %nonzero
zero:
  call void @first()
  ret i32 7
nonzero:
  call void @second()
  %r = add i32 %x, 3
  ret i32 %r
}

; CHECK-LABEL: define{{.*}} i32 @size_wrapper(
; CHECK: call i32 @hinted(
; CHECK: ret i32
; EXPLICIT-LABEL: define{{.*}} i32 @size_wrapper(
; EXPLICIT-NOT: call i32 @hinted(
; EXPLICIT: ret i32
define i32 @size_wrapper(i32 %x) optsize {
  %r = call i32 @hinted(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: define{{.*}} i32 @size_caller(
; CHECK: call i32 @hinted(
; CHECK: }
define i32 @size_caller(i32 %x, i1 %cond) optsize {
  br i1 %cond, label %yes, label %no
yes:
  %r = call i32 @hinted(i32 %x)
  ret i32 %r
no:
  ret i32 0
}

; CHECK-LABEL: define{{.*}} i32 @speed_caller(
; CHECK-NOT: call i32 @hinted(
; CHECK: ret i32
define i32 @speed_caller(i32 %x) {
  %r = call i32 @hinted(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: define{{.*}} i32 @minsize_caller(
; CHECK: call i32 @hinted(
; CHECK: ret i32
define i32 @minsize_caller(i32 %x) minsize optsize {
  %r = call i32 @hinted(i32 %x)
  ret i32 %r
}

define i32 @tiny(i32 %x) inlinehint "function-inline-cost"="0" {
  %r = add i32 %x, 1
  ret i32 %r
}

; CHECK-LABEL: define{{.*}} i32 @tiny_caller(
; CHECK-NOT: call i32 @tiny(
; CHECK: ret i32
define i32 @tiny_caller(i32 %x) optsize {
  %r = call i32 @tiny(i32 %x)
  ret i32 %r
}

define i32 @forced(i32 %x) alwaysinline "function-inline-cost"="2000" {
  %r = add i32 %x, 9
  ret i32 %r
}

; CHECK-LABEL: define{{.*}} i32 @forced_caller(
; CHECK-NOT: call i32 @forced(
; CHECK: ret i32
define i32 @forced_caller(i32 %x) optsize {
  %r = call i32 @forced(i32 %x)
  ret i32 %r
}

define internal i32 @local(ptr %p, i32 %n) inlinehint {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %added, %loop ]
  %v = load volatile i32, ptr %p
  %added = add i32 %sum, %v
  %next = add i32 %i, 1
  %done = icmp eq i32 %next, %n
  br i1 %done, label %exit, label %loop
exit:
  ret i32 %added
}

; CHECK-LABEL: define{{.*}} i32 @local_caller(
; CHECK-NOT: call i32 @local(
; CHECK: ret i32
define i32 @local_caller(ptr %p, i32 %n) optsize {
  %r = call i32 @local(ptr %p, i32 %n)
  ret i32 %r
}

; The hint cap is applied before the cold-call-site threshold. It must not
; subtract from the cold threshold and prevent a zero-cost inline there.
; CHECK-LABEL: define{{.*}} i32 @cold_hint_caller(
; CHECK-NOT: call i32 @tiny(
; CHECK: ret i32
define i32 @cold_hint_caller(i32 %x, i1 %cond) optsize {
  br i1 %cond, label %cold, label %hot, !prof !0
cold:
  %r = call i32 @tiny(i32 %x)
  ret i32 %r
hot:
  ret i32 %x
}

!0 = !{!"branch_weights", i32 1, i32 1000000}
