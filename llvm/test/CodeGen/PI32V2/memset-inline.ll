; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s

; A memset of a constant size is stores up to JieLi's limits: 8 word stores in
; a size-optimized function (32 bytes), 16 otherwise (64 bytes). Firmware runs
; code from RAM that must not call into flash, memset included.

declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; CHECK-LABEL: os32:
; CHECK-NOT:   call
; CHECK:       rts
define void @os32(ptr align 4 %p) optsize {
  call void @llvm.memset.p0.i32(ptr align 4 %p, i8 0, i32 32, i1 false)
  ret void
}

; CHECK-LABEL: os36:
; CHECK:       call memset
define void @os36(ptr align 4 %p) optsize {
  call void @llvm.memset.p0.i32(ptr align 4 %p, i8 0, i32 36, i1 false)
  ret void
}

; CHECK-LABEL: o2_64:
; CHECK-NOT:   call
; CHECK:       rts
define void @o2_64(ptr align 4 %p) {
  call void @llvm.memset.p0.i32(ptr align 4 %p, i8 0, i32 64, i1 false)
  ret void
}

; CHECK-LABEL: o2_68:
; CHECK:       call memset
define void @o2_68(ptr align 4 %p) {
  call void @llvm.memset.p0.i32(ptr align 4 %p, i8 0, i32 68, i1 false)
  ret void
}
