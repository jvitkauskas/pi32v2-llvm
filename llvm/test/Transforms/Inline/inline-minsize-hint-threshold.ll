; A minsize caller inlines almost nothing (the threshold is 5). -inline-minsize-hint-threshold gives callees that ask
; to be inlined another threshold there, and leaves the others alone.
; RUN: opt < %s -passes=inline -S | FileCheck %s --check-prefix=DEFAULT
; RUN: opt < %s -passes=inline -inline-minsize-hint-threshold=50 -S | FileCheck %s --check-prefix=HINT

define i32 @hinted(i32 %x, i32 %y) inlinehint minsize {
  %a = mul i32 %x, %y
  %b = add i32 %a, %x
  %c = xor i32 %b, %y
  %d = shl i32 %c, 3
  %e = mul i32 %d, %a
  %f = sub i32 %e, %b
  %g = lshr i32 %f, 5
  %h = or i32 %g, %c
  %i = mul i32 %h, %d
  %j = xor i32 %i, %e
  %k = add i32 %j, %f
  %l = mul i32 %k, %g
  ret i32 %l
}

define i32 @plain(i32 %x, i32 %y) minsize {
  %a = mul i32 %x, %y
  %b = add i32 %a, %x
  %c = xor i32 %b, %y
  %d = shl i32 %c, 3
  %e = mul i32 %d, %a
  %f = sub i32 %e, %b
  %g = lshr i32 %f, 5
  %h = or i32 %g, %c
  %i = mul i32 %h, %d
  %j = xor i32 %i, %e
  %k = add i32 %j, %f
  %l = mul i32 %k, %g
  ret i32 %l
}

; DEFAULT-LABEL: @caller(
; DEFAULT:       call i32 @hinted
; DEFAULT:       call i32 @hinted
; DEFAULT:       call i32 @plain
; DEFAULT:       call i32 @plain
; HINT-LABEL:    @caller(
; HINT-NOT:      call i32 @hinted
; HINT:          call i32 @plain
; HINT:          call i32 @plain
define i32 @caller(i32 %x, i32 %y) minsize {
  %a = call i32 @hinted(i32 %x, i32 %y)
  %b = call i32 @hinted(i32 %y, i32 %x)
  %c = call i32 @plain(i32 %x, i32 %y)
  %d = call i32 @plain(i32 %y, i32 %x)
  %e = add i32 %a, %b
  %f = add i32 %c, %d
  %g = add i32 %e, %f
  ret i32 %g
}
