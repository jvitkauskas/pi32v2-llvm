; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -O2 -filetype=obj < %s -o /dev/null

define i64 @pair_words(i64 %a) {
; CHECK-LABEL: pair_words:
; CHECK: r1 = r0
  %v = call i64 asm "$0.h = $0.l", "=r,0"(i64 %a)
  ret i64 %v
}

define float @fabs_alias(float %a) {
; CHECK-LABEL: fabs_alias:
; CHECK: r0 = r0 & 0x7FFFFFFF
  %v = call float asm "$0 = fabs($0)", "=r,0"(float %a)
  ret float %v
}
