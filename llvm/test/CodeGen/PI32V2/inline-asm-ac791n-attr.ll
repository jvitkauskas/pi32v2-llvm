; The CPU a function names (clang's -mcpu, rustc's -C target-cpu, both written as the "target-cpu" attribute) is
; the one its inline assembly is checked against.
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -o /dev/null < %s 2>&1 | FileCheck %s

; CHECK: warning: popcnt.rr does nothing on the AC791N
; CHECK-NOT: warning:
define i32 @on_chip(i32 %x) "target-cpu"="ac791n" {
  %v = call i32 asm "$0 = popcnt($1)", "=r,r"(i32 %x)
  ret i32 %v
}
define i32 @elsewhere(i32 %x) "target-cpu"="r3" {
  %v = call i32 asm "$0 = popcnt($1)", "=r,r"(i32 %x)
  ret i32 %v
}
