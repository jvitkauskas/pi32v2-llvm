; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; Base register anchors: the first of a run of far accesses steps the base
; (pre-increment), the others are 16-bit.

; the first of four far loads steps the base, the others are 16-bit
; CHECK-LABEL: anchor:
; CHECK:       = [++r0=120]
; CHECK-NEXT:  = [r0+4]
; CHECK:       = [r0+8]
define i32 @anchor(ptr %s) {
  %a = getelementptr i32, ptr %s, i32 30
  %b = getelementptr i32, ptr %s, i32 31
  %c = getelementptr i32, ptr %s, i32 32
  %x = load volatile i32, ptr %a
  %y = load volatile i32, ptr %b
  %z = load volatile i32, ptr %c
  %t = add i32 %x, %y
  %u = add i32 %t, %z
  ret i32 %u
}

; an indexed loop steps its pointer in the load ([r++=4]), one instruction
; CHECK-LABEL: sum:
; CHECK:       = [r{{[0-9]+}}++=4]
define i32 @sum(ptr %p, i32 %n) {
entry:
  %c = icmp eq i32 %n, 0
  br i1 %c, label %end, label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s1, %loop ]
  %a = getelementptr i32, ptr %p, i32 %i
  %v = load i32, ptr %a
  %s1 = add i32 %s, %v
  %i1 = add i32 %i, 1
  %d = icmp eq i32 %i1, %n
  br i1 %d, label %end, label %loop
end:
  %r = phi i32 [ 0, %entry ], [ %s1, %loop ]
  ret i32 %r
}

; A 3-byte access has an addressing mode too (CodeGenPrepare asserts if no mode
; is legal for its type).
; CHECK-LABEL: load_i24:
define i24 @load_i24(ptr %p, i32 %i) {
  %q = getelementptr i8, ptr %p, i32 %i
  %r = getelementptr i8, ptr %q, i32 6
  %v = load i24, ptr %r, align 1
  ret i24 %v
}
