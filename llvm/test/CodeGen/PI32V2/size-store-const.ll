; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; A constant that the 4-byte store-immediate takes ([r+n] = 0x3F800000) is
; stored that way when used once or twice; three or more stores of it are
; 2 bytes each from a register loaded once.

; CHECK-LABEL: twice:
; CHECK-DAG:   [r0+4] = 0x3F800000
; CHECK-DAG:   [r0+8] = 0x3F800000
define void @twice(ptr %p) {
  %a = getelementptr float, ptr %p, i32 1
  %b = getelementptr float, ptr %p, i32 2
  store float 1.0, ptr %a
  store float 1.0, ptr %b
  ret void
}

; CHECK-LABEL: four:
; CHECK:       r{{[0-9]+}} = 0x3F800000
; CHECK-NOT:   0x3F800000
; CHECK-DAG:   [r0+4] = r{{[0-9]+}}
; CHECK-DAG:   [r0+8] = r{{[0-9]+}}
; CHECK-DAG:   [r0+12] = r{{[0-9]+}}
; CHECK-DAG:   [r0+16] = r{{[0-9]+}}
define void @four(ptr %p) {
  %a = getelementptr float, ptr %p, i32 1
  %b = getelementptr float, ptr %p, i32 2
  %c = getelementptr float, ptr %p, i32 3
  %d = getelementptr float, ptr %p, i32 4
  store float 1.0, ptr %a
  store float 1.0, ptr %b
  store float 1.0, ptr %c
  store float 1.0, ptr %d
  ret void
}
