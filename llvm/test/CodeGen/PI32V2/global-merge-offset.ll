; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s --check-prefix=DEF
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 -pi32v2-merge-max-offset=20 < %s | FileCheck %s --check-prefix=W20
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 -pi32v2-merge-max-offset=10 < %s | FileCheck %s --check-prefix=W10
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 -global-merge-ignore-single-use=true < %s | FileCheck %s --check-prefix=ALL

; JieLi's -pi32v2-merge-max-offset counts words: a merged block is at most
; N * 4 bytes, and a global joins only if it is smaller than that. Without it
; the limit is 255 bytes. Globals are merged in the sets functions use
; together (GlobalMerge's -global-merge-ignore-single-use=false); the generic
; default, one block of every global used with another, is larger here.

@a = internal global [40 x i8] zeroinitializer, align 1
@b = internal global [40 x i8] zeroinitializer, align 1
@c = internal global [40 x i8] zeroinitializer, align 1
@d = internal global [40 x i8] zeroinitializer, align 1

define void @ab(i32 %i) {
  %pa = getelementptr [40 x i8], ptr @a, i32 0, i32 %i
  store i8 1, ptr %pa
  %pb = getelementptr [40 x i8], ptr @b, i32 0, i32 %i
  store i8 2, ptr %pb
  ret void
}
define void @cd(i32 %i) {
  %pc = getelementptr [40 x i8], ptr @c, i32 0, i32 %i
  store i8 3, ptr %pc
  %pd = getelementptr [40 x i8], ptr @d, i32 0, i32 %i
  store i8 4, ptr %pd
  ret void
}

; Two blocks, by use.
; DEF-DAG: .comm .L_MergedGlobals,80,1
; DEF-DAG: .comm .L_MergedGlobals.1,80,1

; 80 bytes: the same two.
; W20-DAG: .comm .L_MergedGlobals,80,1
; W20-DAG: .comm .L_MergedGlobals.1,80,1

; 40 bytes: no 40-byte array is smaller than the limit.
; W10-NOT: .L_MergedGlobals

; The generic default: one block of all four.
; ALL: .comm .L_MergedGlobals,160,1
; ALL-NOT: .L_MergedGlobals.1
