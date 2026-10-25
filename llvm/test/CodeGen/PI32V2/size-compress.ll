; RUN: llc -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s | llvm-objdump -d - | FileCheck %s
; Check emitted bytes, since assembly text permits both long and short forms.

; CHECK-LABEL: <neg32>:
; CHECK: 10 20{{.*}}r0 = -32
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @neg32() minsize {
  ret i32 -32
}

; CHECK-LABEL: <neg1>:
; CHECK: 10 3f{{.*}}r0 = -1
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @neg1() minsize {
  ret i32 -1
}

; CHECK-LABEL: <add32>:
; CHECK: c8 20{{.*}}r0 += 32
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @add32(i32 %a) minsize {
  %r = add i32 %a, 32
  ret i32 %r
}

; CHECK-LABEL: <add127>:
; CHECK: d8 3f{{.*}}r0 += 127
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @add127(i32 %a) minsize {
  %r = add i32 %a, 127
  ret i32 %r
}

; CHECK-LABEL: <bitnot>:
; CHECK: 88 19{{.*}}r0 = ~r0
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @bitnot(i32 %a) minsize {
  %r = xor i32 %a, -1
  ret i32 %r
}

; CHECK-LABEL: <mask>:
; CHECK: {{[0-9a-f][0-9a-f] [0-9a-f][0-9a-f] [0-9a-f][0-9a-f] [0-9a-f][0-9a-f]}}{{.*}}r0 = r0 & 0xFFFFFF00
; CHECK-NEXT: {{.*}}80 00{{.*}}rts
define i32 @mask(i32 %a) minsize {
  %r = and i32 %a, -256
  ret i32 %r
}
