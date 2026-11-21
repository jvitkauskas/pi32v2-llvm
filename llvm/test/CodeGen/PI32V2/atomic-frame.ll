; An atomic load or store of a byte or a halfword in the function's own frame: one access to its stack slot, as
; a word's is and as one through a pointer is. These had no pattern, and the compiler stopped with "Cannot
; select" on an atomic local narrower than a word (an AtomicU8 on the stack in Rust, an _Atomic char in C).
; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s

declare void @use(ptr)

; CHECK-LABEL: load8:
; CHECK: call use
; CHECK-NEXT: r0 = b[sp+3] (u)
define i32 @load8() {
  %a = alloca i8, align 1
  call void @use(ptr %a)
  %v = load atomic i8, ptr %a seq_cst, align 1
  %z = zext i8 %v to i32
  ret i32 %z
}

; Sign extension is an instruction of its own after the load.
; CHECK-LABEL: load8s:
; CHECK: call use
; CHECK-NEXT: r0 = b[sp+3] (u)
; CHECK-NEXT: r0 = r0.b0 (s)
define i32 @load8s() {
  %a = alloca i8, align 1
  call void @use(ptr %a)
  %v = load atomic i8, ptr %a seq_cst, align 1
  %z = sext i8 %v to i32
  ret i32 %z
}

; CHECK-LABEL: load16:
; CHECK: call use
; CHECK-NEXT: r0 = h[sp+2] (u)
define i32 @load16() {
  %a = alloca i16, align 2
  call void @use(ptr %a)
  %v = load atomic i16, ptr %a seq_cst, align 2
  %z = zext i16 %v to i32
  ret i32 %z
}

; CHECK-LABEL: load32:
; CHECK: call use
; CHECK-NEXT: r0 = [sp]
define i32 @load32() {
  %a = alloca i32, align 4
  call void @use(ptr %a)
  %v = load atomic i32, ptr %a seq_cst, align 4
  ret i32 %v
}

; CHECK-LABEL: store8:
; CHECK: b[sp+3] = r0
; CHECK: call use
define void @store8(i8 %x) {
  %a = alloca i8, align 1
  store atomic i8 %x, ptr %a seq_cst, align 1
  call void @use(ptr %a)
  ret void
}

; CHECK-LABEL: store16:
; CHECK: h[sp+2] = r0
; CHECK: call use
define void @store16(i16 %x) {
  %a = alloca i16, align 2
  store atomic i16 %x, ptr %a seq_cst, align 2
  call void @use(ptr %a)
  ret void
}
