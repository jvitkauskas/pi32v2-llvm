; Module-level assembly is read twice more than it is assembled: once for the module summary and once for the IR
; symbol table of a bitcode file. Neither of those warns about what the AC791N runs differently; the code
; generator does, once, when the statement is assembled.
; RUN: opt -module-summary -o %t.bc %s 2>&1 | FileCheck %s --check-prefix=QUIET --allow-empty
; RUN: llc -mtriple=pi32v2 -filetype=obj -o /dev/null %t.bc 2>&1 | FileCheck %s --check-prefix=WARN
; RUN: llc -mtriple=pi32v2 -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=WARN

; QUIET-NOT: warning:
; WARN: warning: popcnt.rr does nothing on the AC791N
; WARN-NOT: warning:

target triple = "pi32v2"

module asm(target_cpu: "ac791n")
    "by_hand: r0 = popcnt(r1)"
