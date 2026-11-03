; RUN: llc -mtriple=pi32v2 -mcpu=r3 < %s | FileCheck %s
;
; Fields past the 14-bit add (a status byte after a large buffer) are reached
; with a constant and an add of two registers. Stores to them on separate
; paths share one such add when they can all follow it, as for nearer fields:
; the constant and the add are made once, the stores are [t+k]. In a function
; optimized for size this holds also when the paths are rarer than the block
; the add goes to.

%state = type { [9284 x i8], i8, i8 }

declare void @f(ptr)

; CHECK-LABEL: status:
; CHECK:       = 9284
; CHECK-NOT:   = 9284
; CHECK-NOT:   = 9285
; CHECK:       .Lfunc_end0:
define void @status(ptr %s, i32 %op) optsize {
entry:
  call void @f(ptr %s)
  switch i32 %op, label %done [
    i32 1, label %a
    i32 2, label %b
    i32 3, label %c
  ]
a:
  call void @f(ptr %s)
  %ea = getelementptr inbounds %state, ptr %s, i32 0, i32 1
  store i8 5, ptr %ea, align 1
  br label %done
b:
  call void @f(ptr %s)
  %eb = getelementptr inbounds %state, ptr %s, i32 0, i32 1
  store i8 7, ptr %eb, align 1
  %sb = getelementptr inbounds %state, ptr %s, i32 0, i32 2
  store i8 0, ptr %sb, align 1
  br label %done
c:
  call void @f(ptr %s)
  %ec = getelementptr inbounds %state, ptr %s, i32 0, i32 1
  store i8 3, ptr %ec, align 1
  br label %done
done:
  ret void
}
