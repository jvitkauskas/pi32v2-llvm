; llvm.trap, llvm.debugtrap and llvm.ubsantrap: btbclr, which the AC791N takes as an illegal instruction (the CPU
; exception, EMU_MSG 2), then a goto to itself; no call to abort().
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -filetype=obj < %s | llvm-objdump -d - | FileCheck %s --check-prefix=OBJ

; CHECK-LABEL: trap:
; CHECK-NOT:   abort
; CHECK:       btbclr
; CHECK-NEXT:  [[L:\.Ltmp[0-9]+]]:
; CHECK-NEXT:  goto [[L]]
define void @trap() {
  call void @llvm.trap()
  unreachable
}

; CHECK-LABEL: debugtrap:
; CHECK:       btbclr
; CHECK-NEXT:  [[L:\.Ltmp[0-9]+]]:
; CHECK-NEXT:  goto [[L]]
define void @debugtrap() {
  call void @llvm.debugtrap()
  ret void
}

; CHECK-LABEL: ubsantrap:
; CHECK:       btbclr
; CHECK-NEXT:  [[L:\.Ltmp[0-9]+]]:
; CHECK-NEXT:  goto [[L]]
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

; The goto is the 16-bit form, to the address it's at: 0x8000 + (-1 & 0x7ff) style, an offset of -2 bytes.
; OBJ-LABEL: <trap>:
; OBJ:       btbclr
; OBJ-NEXT:  goto -2

declare void @llvm.trap()
declare void @llvm.debugtrap()
declare void @llvm.ubsantrap(i8)
