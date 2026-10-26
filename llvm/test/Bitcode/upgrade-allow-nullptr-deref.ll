; RUN: llvm-as < %s | llvm-dis | FileCheck %s

; JieLi's clang (LLVM 4) writes -fallow-pointer-null as "allow-nullptr-deref":
; address 0 is memory on its chips.

; CHECK: define i32 @rd0() [[A:#[0-9]+]]
; CHECK: attributes [[A]] = { null_pointer_is_valid }
; CHECK-NOT: allow-nullptr-deref
define i32 @rd0() "allow-nullptr-deref" {
  %v = load volatile i32, ptr null
  ret i32 %v
}
