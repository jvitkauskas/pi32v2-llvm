; llvm.trap, llvm.debugtrap and llvm.ubsantrap: btbclr, which the AC791N takes as an illegal instruction (the CPU
; exception, EMU_MSG 2), then a goto to itself; no call to abort().
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ
; With every branch relaxed, a trap is still the four bytes getInstSizeInBytes says (its goto has an immediate
; offset, no fixup), which jump tables and branch ranges are sized by.
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -filetype=obj -mc-relax-all < %s | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; CHECK-LABEL: trap:
; CHECK-NOT:   abort
; CHECK:       btbclr
; CHECK-NEXT:  goto -2
define void @trap() {
  call void @llvm.trap()
  unreachable
}

; CHECK-LABEL: debugtrap:
; CHECK:       btbclr
; CHECK-NEXT:  goto -2
define void @debugtrap() {
  call void @llvm.debugtrap()
  ret void
}

; CHECK-LABEL: ubsantrap:
; CHECK:       btbclr
; CHECK-NEXT:  goto -2
define void @ubsantrap() {
  call void @llvm.ubsantrap(i8 12)
  unreachable
}

; A bounds check: the failing path is the trap, the rest goes on.
; CHECK-LABEL: checked:
; CHECK:       btbclr
; CHECK-NOT:   call
define i32 @checked(ptr %a, i32 %i) {
  %ok = icmp ult i32 %i, 4
  br i1 %ok, label %in, label %out
in:
  %p = getelementptr i32, ptr %a, i32 %i
  %v = load i32, ptr %p
  ret i32 %v
out:
  call void @llvm.trap()
  unreachable
}

; A trap is two instructions, so it is never put in an if block, which counts one per machine instruction: the
; condition branches around both, and the path that doesn't trap goes on (it used to skip only btbclr and land
; on the goto).
; CHECK-LABEL: cond_trap:
; CHECK:       if (r0 != 17) goto [[DONE:\.LBB[0-9_]+]]
; CHECK-NOT:   {
; CHECK:       btbclr
; CHECK-NEXT:  goto -2
; CHECK-NEXT:  [[DONE]]:
define i32 @cond_trap(i32 %a, ptr %p) {
  %c = icmp eq i32 %a, 17
  br i1 %c, label %stop, label %done, !prof !0
stop:
  call void @llvm.trap()
  unreachable
done:
  store volatile i32 42, ptr %p
  ret i32 1
}

; CHECK-LABEL: cond_debugtrap:
; CHECK:       if (r0 != 17) goto [[DONE:\.LBB[0-9_]+]]
; CHECK-NOT:   {
; CHECK:       btbclr
; CHECK-NEXT:  goto -2
; CHECK-NEXT:  [[DONE]]:
define i32 @cond_debugtrap(i32 %a, ptr %p) {
  %c = icmp eq i32 %a, 17
  br i1 %c, label %stop, label %done
stop:
  call void @llvm.debugtrap()
  br label %done
done:
  store volatile i32 42, ptr %p
  ret i32 1
}

; A trap is no terminator: IR may go on after one in the same block (machine verifier).
; CHECK-LABEL: after_trap:
; CHECK:       btbclr
define void @after_trap(ptr %p) {
  call void @llvm.trap()
  store volatile i32 1, ptr %p
  ret void
}

; The goto is the 16-bit form, to its own address: f7 9f, an offset of -2 bytes.
; OBJ-LABEL: <trap>:
; OBJ:       btbclr
; OBJ-NEXT:  goto -2

; In the object, the branch around the trap goes past both of its instructions (4 bytes).
; OBJ-LABEL: <cond_trap>:
; OBJ:       if (r0 != 17) goto 4
; OBJ-NEXT:  btbclr
; OBJ-NEXT:  goto -2
; OBJ-LABEL: <cond_debugtrap>:
; OBJ:       if (r0 != 17) goto 4
; OBJ-NEXT:  btbclr
; OBJ-NEXT:  goto -2

declare void @llvm.trap()
declare void @llvm.debugtrap()
declare void @llvm.ubsantrap(i8)

!0 = !{!"branch_weights", i32 1000, i32 1}
