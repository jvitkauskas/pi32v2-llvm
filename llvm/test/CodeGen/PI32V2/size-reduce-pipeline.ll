; RUN: llc -mtriple=pi32v2 -O2 -debug-pass=Structure %s -o /dev/null 2>&1 | FileCheck %s

; Whole-function permutations run before frame lowering. Local recoloring
; must run after sinking and tail duplication, so freeing/reusing a register
; cannot cause a later pass to duplicate copies in loop blocks.
; CHECK: PI32V2 size reduction
; CHECK: PostRA Machine Sink
; CHECK: Prologue/Epilogue Insertion
; CHECK: Tail Duplication
; CHECK: Basic Block Placement
; CHECK: PI32V2 size reduction
; CHECK-NEXT: PI32V2 if blocks

define i32 @pipeline(i32 %x) optsize {
  %r = add i32 %x, 64
  ret i32 %r
}
