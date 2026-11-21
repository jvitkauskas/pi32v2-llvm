; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

; A vector argument is its lanes, each a scalar argument of its own: a lane narrower than a word takes a word,
; in r0-r3 and then on the stack. clang's va_arg (clang/lib/CodeGen/Targets/PI32V2.cpp) reads a vector so.

; Lane 3 is r3 and the int after the vector is the first stack word.
; CHECK-LABEL: lanes8:
; CHECK: r1 = r3.b0 (s)
; CHECK: r0 = [sp]
; CHECK: r0 = r1 + r0
define i32 @lanes8(<4 x i8> %v, i32 %k) {
  %e = extractelement <4 x i8> %v, i32 3
  %x = sext i8 %e to i32
  %r = add i32 %x, %k
  ret i32 %r
}

; After one int: lanes in r1, r2, r3 and the first stack word, then the next int.
; CHECK-LABEL: call8:
; CHECK-DAG: [sp+4] = r{{[0-9]+}}
; CHECK-DAG: [sp] = r3
; CHECK-DAG: r3 = r{{[0-9]+}}
; CHECK: call take8
declare i32 @take8(i32, <4 x i8>, i32)
define i32 @call8(<4 x i8> %v) {
  %r = call i32 @take8(i32 1, <4 x i8> %v, i32 2)
  ret i32 %r
}

; Eight bools are eight words.
; CHECK-LABEL: bools:
; CHECK: r0 = [sp+16]
define i32 @bools(<8 x i1> %v, i32 %k) {
  ret i32 %k
}
