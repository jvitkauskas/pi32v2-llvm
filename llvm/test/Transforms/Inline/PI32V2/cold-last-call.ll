; REQUIRES: pi32v2-registered-target
; RUN: opt -mtriple=pi32v2 -passes='require<profile-summary>,cgscc(inline)' -S %s | FileCheck %s --check-prefix=PI32V2
; RUN: opt -mtriple=x86_64 -passes='require<profile-summary>,cgscc(inline)' -S %s | FileCheck %s --check-prefix=GENERIC
;
; Coldness must not suppress the size saving from removing a sole local callee
; in an optsize caller. Preserve cold inlining policy for speed-mode callers,
; shared callees, escaping functions and noinline calls.

declare i32 @work(i32)

; PI32V2-LABEL: define i32 @size_caller(
; PI32V2-NOT: call i32 @sole(
; PI32V2: call i32 @work(
; PI32V2: ret i32
; GENERIC-LABEL: define i32 @size_caller(
; GENERIC: call i32 @sole(
define i32 @size_caller(i1 %c, i32 %x) optsize {
  br i1 %c, label %cold, label %exit, !prof !0
cold:
  %r = call i32 @sole(i32 %x)
  ret i32 %r
exit:
  ret i32 0
}

define internal i32 @sole(i32 %x) {
  %a = call i32 @work(i32 %x)
  %b = call i32 @work(i32 %a)
  %c = call i32 @work(i32 %b)
  ret i32 %c
}

; PI32V2-LABEL: define i32 @speed_caller(
; PI32V2: call i32 @speed_sole(
; GENERIC-LABEL: define i32 @speed_caller(
; GENERIC: call i32 @speed_sole(
define i32 @speed_caller(i1 %c, i32 %x) {
  br i1 %c, label %cold, label %exit, !prof !0
cold:
  %r = call i32 @speed_sole(i32 %x)
  ret i32 %r
exit:
  ret i32 0
}

define internal i32 @speed_sole(i32 %x) {
  %a = call i32 @work(i32 %x)
  %b = call i32 @work(i32 %a)
  %c = call i32 @work(i32 %b)
  ret i32 %c
}

; PI32V2-LABEL: define i32 @shared_caller(
; PI32V2: call i32 @shared(
; PI32V2: call i32 @shared(
define i32 @shared_caller(i1 %c, i32 %x) optsize {
  br i1 %c, label %cold, label %exit, !prof !0
cold:
  %a = call i32 @shared(i32 %x)
  %b = call i32 @shared(i32 %a)
  ret i32 %b
exit:
  ret i32 0
}

define internal i32 @shared(i32 %x) {
  %a = call i32 @work(i32 %x)
  %b = call i32 @work(i32 %a)
  ret i32 %b
}

@escaped = global ptr @address_taken

; PI32V2-LABEL: define i32 @escape_caller(
; PI32V2: call i32 @address_taken(
define i32 @escape_caller(i1 %c, i32 %x) optsize {
  br i1 %c, label %cold, label %exit, !prof !0
cold:
  %r = call i32 @address_taken(i32 %x)
  ret i32 %r
exit:
  ret i32 0
}

define internal i32 @address_taken(i32 %x) {
  %a = call i32 @work(i32 %x)
  %b = call i32 @work(i32 %a)
  ret i32 %b
}

; PI32V2-LABEL: define i32 @noinline_caller(
; PI32V2: call i32 @noinline_sole(
define i32 @noinline_caller(i1 %c, i32 %x) optsize {
  br i1 %c, label %cold, label %exit, !prof !0
cold:
  %r = call i32 @noinline_sole(i32 %x) noinline
  ret i32 %r
exit:
  ret i32 0
}

define internal i32 @noinline_sole(i32 %x) {
  %a = call i32 @work(i32 %x)
  %b = call i32 @work(i32 %a)
  ret i32 %b
}

!0 = !{!"branch_weights", i32 1, i32 10000}
