; RUN: llc -mtriple=pi32v2 -mcpu=r3 -function-sections=0 < %s | FileCheck %s
; RUN: llc -mtriple=pi32v2 -mcpu=r3 -function-sections=0 -filetype=obj < %s \
; RUN:   | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; Without function sections (the linker's LTO), the functions share .text and
; their near calls are 2 bytes; the ones that may be dead get a section each
; for --gc-sections: a library function nothing in the module uses (memmove,
; __udivsi3, which code generation may call), and a local function only such a
; function uses. A library function used here (memset) and a local function
; used from .text stay in .text.

; CHECK:      .section .text.memmove,"ax"
; CHECK-NEXT: .globl memmove
; CHECK:      .section .text.dead_helper,"ax"
; CHECK:      .section .text.__udivsi3,"ax"
; CHECK:      .text
; CHECK-NEXT: .globl memset
; CHECK-NOT:  .section
; CHECK:      .globl live
; CHECK-NOT:  .section
; CHECK:      live_helper:

define ptr @memmove(ptr %d, ptr %s, i32 %n) noinline {
  call void @dead_helper(ptr %d)
  ret ptr %d
}

define internal void @dead_helper(ptr %p) noinline {
  store volatile i8 0, ptr %p
  ret void
}

define i32 @__udivsi3(i32 %a, i32 %b) noinline {
  %q = udiv i32 %a, %b
  ret i32 %q
}

define ptr @memset(ptr %d, i32 %c, i32 %n) noinline {
  store volatile i32 %c, ptr %d
  ret ptr %d
}

define void @live(ptr %p) {
  call ptr @memset(ptr %p, i32 0, i32 4)
  call void @live_helper(ptr %p)
  ret void
}

define internal void @live_helper(ptr %p) noinline {
  store volatile i8 1, ptr %p
  ret void
}

; OBJ-LABEL: <live>:
; OBJ:       c: 71 99 {{.*}}call -14
; OBJ:       10: 01 81 {{.*}}call 2
