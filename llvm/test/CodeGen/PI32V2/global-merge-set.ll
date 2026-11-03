; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 -data-sections < %s | FileCheck %s
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 -data-sections < %s | llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj -o /dev/null

; Merged globals are named with .set, as JieLi's compiler prints them: the
; assembler reads "x = y" as an instruction.

@a = internal global i32 0, align 4
@b = internal global i32 0, align 4

define void @ab(i32 %v) {
  store volatile i32 %v, ptr @a
  store volatile i32 %v, ptr @b
  ret void
}

; CHECK: .set a, .L_MergedGlobals
; CHECK: .set b, .L_MergedGlobals+4
