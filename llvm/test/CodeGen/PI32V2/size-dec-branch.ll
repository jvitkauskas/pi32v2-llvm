; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; Decrement-and-branch: a counted loop ends in one dbnz (PI32V2DecBranch).

; CHECK-LABEL: count:
; CHECK:       if (--r1 != 0) goto
define void @count(ptr %p, i32 %n) {
entry:
  %c = icmp eq i32 %n, 0
  br i1 %c, label %end, label %loop
loop:
  %i = phi i32 [ %n, %entry ], [ %i1, %loop ]
  %q = phi ptr [ %p, %entry ], [ %q1, %loop ]
  store volatile i32 0, ptr %q
  %q1 = getelementptr i32, ptr %q, i32 1
  %i1 = add i32 %i, -1
  %d = icmp eq i32 %i1, 0
  br i1 %d, label %end, label %loop
end:
  ret void
}
