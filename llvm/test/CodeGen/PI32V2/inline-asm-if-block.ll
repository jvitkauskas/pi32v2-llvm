; An if block written in inline asm keeps its braces in assembly output (the asm parser closes the
; block, and for text writes the brace too), so the .s file assembles again.
; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s
; The assembly output assembles again, to the same code as the object llc writes directly.
; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj -o %t.rt.o
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s -o %t.o
; RUN: llvm-objdump -d %t.rt.o | grep -v "file format" > %t.rt.dis
; RUN: llvm-objdump -d %t.o | grep -v "file format" > %t.dis
; RUN: diff %t.rt.dis %t.dis

; CHECK-LABEL: if_then:
; CHECK:       if (r0 != 0) {
; CHECK:       r0 = 1
; CHECK:       }
define i32 @if_then(i32 %x) {
  %v = call i32 asm "if ($0 != 0) {\0A$0 = 1\0A}", "=r,0"(i32 %x)
  ret i32 %v
}

; CHECK-LABEL: if_else:
; CHECK:       if (r0 != 0) {
; CHECK:       r0 = 1
; CHECK:       } else {
; CHECK:       r0 = 2
; CHECK:       }
define i32 @if_else(i32 %x) {
  %v = call i32 asm "if ($0 != 0) {\0A$0 = 1\0A}else{\0A$0 = 2\0A}", "=r,0"(i32 %x)
  ret i32 %v
}

; A rep block parsed from inline asm is written as a block, `rep r1 {` (its length is the block's),
; not with the length's expression, which wouldn't assemble.
; CHECK-LABEL: rep_if:
; CHECK:       rep r1 {
; CHECK:       if (r0 != 0) {
; CHECK:       } else {
; CHECK:       }
; CHECK:       }
define i32 @rep_if(i32 %x, i32 %n) {
  %v = call i32 asm "rep $2 {\0Aif ($0 != 0) {\0A$0 = 1\0A}else{\0A$0 = 2\0A}\0A}", "=r,0,r"(i32 %x, i32 %n)
  ret i32 %v
}
