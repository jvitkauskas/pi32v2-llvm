; A block still open at the end of an inline asm statement is an error, in assembly output as in an
; object (its lengths were never set; text output used to be written anyway, and not assemble).
; RUN: not llc -mtriple=pi32v2 -mcpu=r3 < %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s -o /dev/null 2>&1 | FileCheck %s

; CHECK: error: this block has no closing }
define i32 @open_if(i32 %x) {
  %v = call i32 asm "if ($0 != 0) {\0A$0 = 1", "=r,0"(i32 %x)
  ret i32 %v
}
