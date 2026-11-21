; A tail call gives up the caller's frame, so it can't take a pointer into it: an aggregate, or a _BitInt wider than
; 64 bits, goes as a pointer to a copy the caller makes there (clang's PI32V2 ABI). Such a call is a plain call, and
; a musttail one is an error rather than a goto with a dangling pointer. Pointers the caller was given still go.
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s
; RUN: sed 's/^; MUST //' %s | not llc -mtriple=pi32v2 -mcpu=r3 -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
; RUN: sed 's/^; MUSTOFF //' %s | not llc -mtriple=pi32v2 -mcpu=r3 -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
; With -pi32v2-large-program there are no tail calls at all.
; RUN: not llc -mtriple=pi32v2 -mcpu=r3 -pi32v2-large-program -o /dev/null < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=LARGE

; ERR: error: {{.*}}in function {{must|must_off}} {{.*}}PI32V2: this musttail call cannot be a tail call
; LARGE: error: {{.*}}in function given {{.*}}PI32V2: no tail calls with -pi32v2-large-program

declare i32 @take(ptr)

; CHECK-LABEL: copy:
; CHECK: call take
; CHECK-NOT: goto take
define i32 @copy(ptr %p) {
  %c = alloca [16 x i8], align 4
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %c, ptr align 4 %p, i32 16, i1 false)
  %r = tail call i32 @take(ptr %c)
  ret i32 %r
}

; CHECK-LABEL: copy_off:
; CHECK: call take
; CHECK-NOT: goto take
define i32 @copy_off(ptr %p) {
  %c = alloca [16 x i8], align 4
  call void @llvm.memcpy.p0.p0.i32(ptr align 4 %c, ptr align 4 %p, i32 16, i1 false)
  %q = getelementptr inbounds i8, ptr %c, i32 4
  %r = tail call i32 @take(ptr %q)
  ret i32 %r
}

; CHECK-LABEL: given:
; CHECK: goto take
define i32 @given(ptr %p) {
  %r = musttail call i32 @take(ptr %p)
  ret i32 %r
}

; MUST define i32 @must(ptr %p) {
; MUST   %c = alloca [16 x i8], align 4
; MUST   call void @llvm.memcpy.p0.p0.i32(ptr align 4 %c, ptr align 4 %p, i32 16, i1 false)
; MUST   %r = musttail call i32 @take(ptr %c)
; MUST   ret i32 %r
; MUST }

; MUSTOFF define i32 @must_off(ptr %p) {
; MUSTOFF   %c = alloca [16 x i8], align 4
; MUSTOFF   call void @llvm.memcpy.p0.p0.i32(ptr align 4 %c, ptr align 4 %p, i32 16, i1 false)
; MUSTOFF   %q = getelementptr inbounds i8, ptr %c, i32 4
; MUSTOFF   %r = musttail call i32 @take(ptr %q)
; MUSTOFF   ret i32 %r
; MUSTOFF }
