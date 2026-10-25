; RUN: llc -mtriple=pi32v2 -O2 < %s | FileCheck %s
; Interrupt handlers, atomics, inline assembly and a dynamic stack frame.

@glob = external global i32

; An interrupt handler saves psr and reti, and whatever it uses or a call may
; clobber, and returns with rti (pi32v2-isa docs/ABI.md section 7.1).
; CHECK-LABEL: irq:
; CHECK:      [--sp] = {psr, reti}
; CHECK-NEXT: [--sp] = {rets, r3-r0}
; CHECK:      call work
; CHECK:      {rets, r3-r0} = [sp++]
; CHECK-NEXT: {psr, reti} = [sp++]
; CHECK-NEXT: rti
define void @irq() "interrupt" nounwind {
  %v = load volatile i32, ptr @glob
  %w = add i32 %v, 1
  store volatile i32 %w, ptr @glob
  call void @work()
  ret void
}
declare void @work()

; CHECK-LABEL: irq_empty:
; CHECK:      [--sp] = {psr, reti}
; CHECK-NEXT: {psr, reti} = [sp++]
; CHECK-NEXT: rti
define void @irq_empty() "interrupt" nounwind {
  ret void
}

; Read-modify-write atomics are calls of the __sync_* functions; loads, stores
; and fences are plain.
; CHECK-LABEL: atomics:
; CHECK:      call __sync_fetch_and_add_4
; CHECK:      call __sync_val_compare_and_swap_4
; CHECK:      csync
define i32 @atomics(ptr %p, i32 %x) nounwind {
  %a = atomicrmw add ptr %p, i32 %x seq_cst
  %c = cmpxchg ptr %p, i32 %a, i32 %x seq_cst seq_cst
  %v = extractvalue { i32, i1 } %c, 0
  fence seq_cst
  %l = load atomic i32, ptr %p seq_cst, align 4
  %s = add i32 %v, %l
  ret i32 %s
}

; CHECK-LABEL: asm:
; CHECK:      cli r0
; CHECK:      sti r0
define i32 @asm() nounwind {
  %s = call i32 asm sideeffect "cli $0", "=r,~{memory}"()
  call void asm sideeffect "sti $0", "r,~{memory}"(i32 %s)
  ret i32 %s
}

; A variable-sized object: r7 is the frame pointer, saved with rets.
; CHECK-LABEL: vla:
; CHECK:      [--sp] = {rets, r7}
; CHECK:      call use
; CHECK:      {pc, r7} = [sp++]
define void @vla(i32 %n) nounwind {
  %b = alloca i8, i32 %n, align 4
  call void @use(ptr %b)
  ret void
}
declare void @use(ptr)
