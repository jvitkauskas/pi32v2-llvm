; RUN: llc -mtriple=pi32v2 -mcpu=r3 -O2 < %s | FileCheck %s
;
; Far accesses (past what [r+n] reaches: 1 KB) to one base share an add when
; one can be placed where they all follow it, and the base's neighbours are
; reached by [t+k] (16 bits within 5 bits of the offset, else 32 bits within 9).

%big = type { [600 x i32] }

; four far words in different blocks: one add, the others [t+k]
; CHECK-LABEL: share:
; CHECK:       = r0 + 2000
; CHECK-NOT:   = r{{[0-9]+}} + 20
; CHECK:       [r{{[0-9]+}}+{{-?[0-9]+}}]
define i32 @share(ptr %s, i32 %c) {
entry:
  %a = getelementptr %big, ptr %s, i32 0, i32 0, i32 500
  %x = load volatile i32, ptr %a
  %p = icmp eq i32 %c, 0
  br i1 %p, label %l, label %r
l:
  %b = getelementptr %big, ptr %s, i32 0, i32 0, i32 501
  %y = load volatile i32, ptr %b
  br label %j
r:
  %d = getelementptr %big, ptr %s, i32 0, i32 0, i32 503
  %z = load volatile i32, ptr %d
  br label %j
j:
  %m = phi i32 [ %y, %l ], [ %z, %r ]
  %e = getelementptr %big, ptr %s, i32 0, i32 0, i32 505
  %w = load volatile i32, ptr %e
  %t = add i32 %x, %m
  %u = add i32 %t, %w
  ret i32 %u
}

; an address that is loaded and stored (x += 1) has one add for both
; CHECK-LABEL: rmw:
; CHECK:       = r0 + 2400
; CHECK-NOT:   = r{{[0-9]+}} + 2
define void @rmw(ptr %s) {
  %a = getelementptr %big, ptr %s, i32 0, i32 0, i32 600
  %v = load volatile i32, ptr %a
  %w = add i32 %v, 1
  store volatile i32 %w, ptr %a
  ret void
}
