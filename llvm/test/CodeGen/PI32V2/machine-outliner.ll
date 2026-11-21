; The machine outliner (-enable-machine-outliner): a repeated sequence becomes a function. A call writes only rets, so
; the sequence runs there as it did in place, and what decides how it is reached is rets (PI32V2InstrInfo.cpp).
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 -enable-machine-outliner < %s | FileCheck %s
; Not unless asked for.
; RUN: llc -verify-machineinstrs -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s --check-prefix=OFF
; OFF-NOT: OUTLINED_FUNCTION

declare void @ext(i32, i32, i32, i32)
declare void @many(i32, i32, i32, i32, i32, i32)
declare i32 @get()
@g = global [8 x i32] zeroinitializer

; With calls in it: the function saves rets around them.
; CHECK-LABEL: frame_a:
; CHECK:       [--sp] = rets
; CHECK-NEXT:  call [[FRAME:OUTLINED_FUNCTION_[0-9]+]]
; CHECK-NEXT:  [r1+0] = r0
; CHECK-NEXT:  pc = [sp++]
define void @frame_a() minsize optsize {
  call void @ext(i32 1001, i32 2002, i32 3003, i32 4004)
  %v = call i32 @get()
  store i32 %v, ptr @g
  ret void
}

; CHECK-LABEL: frame_b:
; CHECK:       call [[FRAME]]
; CHECK-NEXT:  [r1+4] = r0
define void @frame_b() minsize optsize {
  call void @ext(i32 1001, i32 2002, i32 3003, i32 4004)
  %v = call i32 @get()
  store i32 %v, ptr getelementptr (i32, ptr @g, i32 1)
  ret void
}

; Ending in a call: the function ends in a goto to the callee, which returns to after the call that reached it. rets is
; dead here, as these saved it.
; CHECK-LABEL: thunk_a:
; CHECK:       call get
; CHECK-NEXT:  call [[THUNK:OUTLINED_FUNCTION_[0-9]+]]
; CHECK-NEXT:  r0 = r4 + r0
define i32 @thunk_a(i32 %x) minsize optsize {
  %v = call i32 @get()
  %a = mul i32 %v, 12345
  %b = xor i32 %a, 54321
  %c = add i32 %b, 99999
  %d = mul i32 %c, 77777
  %e = xor i32 %d, %x
  %w = call i32 @get()
  %r = add i32 %e, %w
  ret i32 %r
}

; CHECK-LABEL: thunk_b:
; CHECK:       call [[THUNK]]
; CHECK-NEXT:  r0 = r4 - r0
define i32 @thunk_b(i32 %x) minsize optsize {
  %v = call i32 @get()
  %a = mul i32 %v, 12345
  %b = xor i32 %a, 54321
  %c = add i32 %b, 99999
  %d = mul i32 %c, 77777
  %e = xor i32 %d, %x
  %w = call i32 @get()
  %r = sub i32 %e, %w
  ret i32 %r
}

; Ending in a return: a goto reaches the function, which returns for both. These are leaves, where rets is the way
; back and no call could be used.
; CHECK-LABEL: tail_a:
; CHECK:       goto [[TAIL:OUTLINED_FUNCTION_[0-9]+]]
define i32 @tail_a(i32 %x) minsize optsize {
  %a = mul i32 %x, 12345
  %b = xor i32 %a, 54321
  %c = add i32 %b, 99999
  %d = mul i32 %c, 77777
  ret i32 %d
}

; CHECK-LABEL: tail_b:
; CHECK:       r0 = r0 + r1
; CHECK-NEXT:  goto [[TAIL]]
define i32 @tail_b(i32 %x, i32 %y) minsize optsize {
  %s = add i32 %x, %y
  %a = mul i32 %s, 12345
  %b = xor i32 %a, 54321
  %c = add i32 %b, 99999
  %d = mul i32 %c, 77777
  ret i32 %d
}

; The same middle in two leaves, with different ends: rets is live, so nothing could come back from a shared copy.
; CHECK-LABEL: live_a:
; CHECK-NOT:   OUTLINED_FUNCTION
; CHECK:       rts
define i32 @live_a(i32 %x, ptr %p) minsize optsize {
  %a = mul i32 %x, 23456
  %b = xor i32 %a, 65432
  %c = add i32 %b, 88888
  %d = mul i32 %c, 66666
  store i32 %d, ptr %p
  ret i32 1
}

; CHECK-LABEL: live_b:
; CHECK-NOT:   OUTLINED_FUNCTION
; CHECK:       rts
define i32 @live_b(i32 %x, ptr %p) minsize optsize {
  %a = mul i32 %x, 23456
  %b = xor i32 %a, 65432
  %c = add i32 %b, 88888
  %d = mul i32 %c, 66666
  store i32 %d, ptr %p
  ret i32 2
}

; Calls with arguments on the stack: saving rets there would move them. So the calls stay, though what sets up
; their registers may go.
; CHECK-LABEL: stack_a:
; CHECK:       call many
; CHECK-NEXT:  call get
define void @stack_a() minsize optsize {
  call void @many(i32 1101, i32 2202, i32 3303, i32 4404, i32 5505, i32 6606)
  %v = call i32 @get()
  store i32 %v, ptr @g
  ret void
}

; CHECK-LABEL: stack_b:
; CHECK:       call many
; CHECK-NEXT:  call get
define void @stack_b() minsize optsize {
  call void @many(i32 1101, i32 2202, i32 3303, i32 4404, i32 5505, i32 6606)
  %v = call i32 @get()
  store i32 %v, ptr getelementptr (i32, ptr @g, i32 1)
  ret void
}

; A handler is left alone: it returns with rti, and has rets of its own to save.
; CHECK-LABEL: handler_a:
; CHECK-NOT:   OUTLINED_FUNCTION
; CHECK:       rti
define void @handler_a() minsize optsize "interrupt" {
  %v = call i32 @get()
  %a = mul i32 %v, 12345
  %b = xor i32 %a, 54321
  %c = add i32 %b, 99999
  %d = mul i32 %c, 77777
  store i32 %d, ptr getelementptr (i32, ptr @g, i32 2)
  ret void
}

; CHECK:       [[THUNK]]:
; CHECK:       r4 ^= r0
; CHECK-NEXT:  goto get

; CHECK:       [[TAIL]]:
; CHECK:       r0 = r0 + r1
; CHECK-NEXT:  rts

; CHECK:       [[FRAME]]:
; CHECK:       [--sp] = rets
; CHECK-NEXT:  r0 = 1001
; CHECK:       call ext
; CHECK-NEXT:  call get
; CHECK-NEXT:  r1 = g
; CHECK-NEXT:  pc = [sp++]
