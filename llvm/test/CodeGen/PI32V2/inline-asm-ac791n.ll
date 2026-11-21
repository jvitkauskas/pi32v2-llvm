; With the AC791N as the CPU, inline assembly is checked against what that chip's hardware does (pi32v2-isa's
; table, key hw): it runs popcnt as a no-op, for example. A warning, and the instruction is still assembled; the
; compiler's own popcount is computed in software for every CPU.
; RUN: llc -mtriple=pi32v2 -mcpu=ac791n < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,WARN
; RUN: llc -mtriple=pi32v2 -mcpu=ac791n -filetype=obj -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=WARN
; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,QUIET
; The CPU of the function counts, not the module's.
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -filetype=obj -o /dev/null < %s 2>&1 | FileCheck %s --check-prefix=QUIET --allow-empty

; WARN: warning: popcnt.rr does nothing on the AC791N
; WARN-NOT: warning:
; QUIET-NOT: warning:

; CHECK-LABEL: by_hand:
; CHECK: r0 = popcnt(r0)
define i32 @by_hand(i32 %x) {
  %v = call i32 asm "$0 = popcnt($1)", "=r,r"(i32 %x)
  ret i32 %v
}

; CHECK-LABEL: by_compiler:
; CHECK-NOT: popcnt(
; CHECK: rts
define i32 @by_compiler(i32 %x) {
  %v = call i32 @llvm.ctpop.i32(i32 %x)
  ret i32 %v
}
