; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -O2 -filetype=obj < %s -o /dev/null

; "r" chooses an aligned pair for a 64-bit value.
define i64 @pair_r(i32 %a, i32 %b) {
; CHECK-LABEL: pair_r:
; CHECK: r1_r0 = r0 * r1 (s)
  %v = call i64 asm "$0 = $1 * $2 (s)", "=r,r,r"(i32 %a, i32 %b)
  ret i64 %v
}

; Simultaneous early-clobber outputs exhaust each bank. The asm text is a
; comment so the assertions describe allocation, independent of instructions.
define void @word_banks() {
; CHECK-LABEL: word_banks:
; CHECK: words r{{[0-7]}} r{{[0-7]}} r{{[0-7]}} r{{[0-7]}} r{{[0-7]}} r{{[0-7]}} r{{[0-7]}} r{{[0-7]}}
; CHECK: high r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}} r{{(8|9|1[0-5])}}
  %v = call {i32,i32,i32,i32,i32,i32,i32,i32} asm sideeffect "// words $0 $1 $2 $3 $4 $5 $6 $7", "=&w,=&w,=&w,=&w,=&w,=&w,=&w,=&w"()
  %h = call {i32,i32,i32,i32,i32,i32,i32,i32} asm sideeffect "// high $0 $1 $2 $3 $4 $5 $6 $7", "=&W,=&W,=&W,=&W,=&W,=&W,=&W,=&W"()
  ret void
}

define void @pair_banks() {
; CHECK-LABEL: pair_banks:
; CHECK: pairs r{{[1357]}}_r{{[0246]}} r{{[1357]}}_r{{[0246]}} r{{[1357]}}_r{{[0246]}} r{{[1357]}}_r{{[0246]}}
; CHECK: highpairs r{{(9|11|13|15)}}_r{{(8|10|12|14)}} r{{(9|11|13|15)}}_r{{(8|10|12|14)}} r{{(9|11|13|15)}}_r{{(8|10|12|14)}} r{{(9|11|13|15)}}_r{{(8|10|12|14)}}
  %v = call {i64,i64,i64,i64} asm sideeffect "// pairs $0 $1 $2 $3", "=&d,=&d,=&d,=&d"()
  %h = call {i64,i64,i64,i64} asm sideeffect "// highpairs $0 $1 $2 $3", "=&D,=&D,=&D,=&D"()
  ret void
}

; Inputs, tied operands, narrow values and floating point bit patterns.
define i32 @word_rw(i32 %a) {
; CHECK-LABEL: word_rw:
; CHECK: r0 = r0 + 1
  %v = call i32 asm "$0 += 1", "=w,0"(i32 %a)
  ret i32 %v
}

define i16 @short_pair(i16 %a) {
; CHECK-LABEL: short_pair:
; CHECK: r1_r0 = r1_r0
  %v = call i16 asm "// $0 = $1", "=d,d"(i16 %a)
  ret i16 %v
}

define float @float_word(float %a) {
; CHECK-LABEL: float_word:
; CHECK: r0 &= 2147483647
  %v = call float asm "$0 = $0 & 0x7FFFFFFF", "=w,0"(float %a)
  ret float %v
}

define double @double_pair(double %a) {
; CHECK-LABEL: double_pair:
; CHECK: r1_r0 = r1_r0
  %v = call double asm "// $0 = $1", "=r,r"(double %a)
  ret double %v
}

define i64 @high_pair_rw(i64 %a) {
; CHECK-LABEL: high_pair_rw:
; CHECK: r{{(9|11|13|15)}}_r{{(8|10|12|14)}} <<= 1
  %v = call i64 asm "$0 <<= 1", "=D,0"(i64 %a)
  ret i64 %v
}

define i64 @low_pair_rw(i64 %a) {
; CHECK-LABEL: low_pair_rw:
; CHECK: r{{[1357]}}_r{{[0246]}} <<= 1
  %v = call i64 asm "$0 <<= 1", "=d,0"(i64 %a)
  ret i64 %v
}

define i64 @wide_word(i64 %a) {
; CHECK-LABEL: wide_word:
; CHECK: word r0
; CHECK: r1 = 0
  %v = call i64 asm "// word $0", "=w,0"(i64 %a)
  ret i64 %v
}
