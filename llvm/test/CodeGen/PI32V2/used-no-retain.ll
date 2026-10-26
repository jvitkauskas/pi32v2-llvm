; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

; __attribute__((used)) keeps a global from the compiler, not from the
; linker's --gc-sections, as with JieLi's toolchain: no SHF_GNU_RETAIN ("R"),
; and no section of its own.

@handler = internal global [2 x i32] [i32 1, i32 2], section ".bt_sleep", align 4
@llvm.used = appending global [1 x ptr] [ptr @handler], section "llvm.metadata"

; CHECK: .section .bt_sleep,"aw",@progbits{{$}}
; CHECK-NOT: "awR"
