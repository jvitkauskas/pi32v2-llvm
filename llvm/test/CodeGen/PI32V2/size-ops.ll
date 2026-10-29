; RUN: llc -mtriple=pi32v2 -mcpu=r3 -mattr=+fprev1 -O2 < %s | FileCheck %s
;
; Instructions the selector now uses: reverse subtract, the bit operations by
; a bit number, bit clear by a register, field insert, a modified-immediate
; float constant, a constant stored without a register.

; CHECK-LABEL: rsub:
; CHECK:       r0 = 0x64 - r0
define i32 @rsub(i32 %x) {
  %r = sub i32 100, %x
  ret i32 %r
}

; CHECK-LABEL: neg:
; CHECK:       r0 = 0x0 - r0
define i32 @neg(i32 %x) {
  %r = sub i32 0, %x
  ret i32 %r
}

; CHECK-LABEL: setbit:
; CHECK:       r0 = r0 | (1 << r1)
define i32 @setbit(i32 %x, i32 %b) {
  %s = shl i32 1, %b
  %r = or i32 %x, %s
  ret i32 %r
}

; CHECK-LABEL: clrbit:
; CHECK:       r0 = r0 & ~(1 << r1)
define i32 @clrbit(i32 %x, i32 %b) {
  %s = shl i32 1, %b
  %n = xor i32 %s, -1
  %r = and i32 %x, %n
  ret i32 %r
}

; CHECK-LABEL: togbit:
; CHECK:       r0 = r0 ^ (1 << r1)
define i32 @togbit(i32 %x, i32 %b) {
  %s = shl i32 1, %b
  %r = xor i32 %x, %s
  ret i32 %r
}

; CHECK-LABEL: andbit:
; CHECK:       r0 = r0 & (1 << r1)
define i32 @andbit(i32 %x, i32 %b) {
  %s = shl i32 1, %b
  %r = and i32 %x, %s
  ret i32 %r
}

; CHECK-LABEL: bic:
; CHECK:       r0 = r0 & ~r1
define i32 @bic(i32 %x, i32 %m) {
  %n = xor i32 %m, -1
  %r = and i32 %x, %n
  ret i32 %r
}

; CHECK-LABEL: rmwbit:
; CHECK:       [r0+8] |= 1 << r1
define void @rmwbit(ptr %p, i32 %b) {
  %a = getelementptr i32, ptr %p, i32 2
  %v = load i32, ptr %a
  %s = shl i32 1, %b
  %r = or i32 %v, %s
  store i32 %r, ptr %a
  ret void
}

; the field of b into a: a mask, a shift and an or are one instruction
; CHECK-LABEL: insert:
; CHECK:       r0 <= insert(r1, p:8, l:8)
define i32 @insert(i32 %a, i32 %b) {
  %k = and i32 %a, -65281
  %s = shl i32 %b, 8
  %m = and i32 %s, 65280
  %r = or i32 %k, %m
  ret i32 %r
}

; CHECK-LABEL: insert_or:
; CHECK:       r0 <= insert(r1, p:7, l:7)
define i32 @insert_or(i32 %a, i32 %b) {
  %a1 = and i32 %a, 127
  %m = and i32 %b, 127
  %s = shl i32 %m, 7
  %r = or i32 %a1, %s
  ret i32 %r
}

; 0.5f is a modified immediate
; CHECK-LABEL: fconst:
; CHECK:       r1 = 0x3F000000
define float @fconst(float %x) {
  %q = fadd float %x, 0.5
  ret float %q
}

; a constant stored once needs no register
; CHECK-LABEL: storeonce:
; CHECK:       [r0+8] = 0x5
; CHECK-NEXT:  rts
define void @storeonce(ptr %p) {
  %a = getelementptr i32, ptr %p, i32 2
  store i32 5, ptr %a
  ret void
}
