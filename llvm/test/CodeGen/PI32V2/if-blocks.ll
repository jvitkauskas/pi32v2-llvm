; RUN: llc -mtriple=pi32v2 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=pi32v2 -O2 -show-mc-encoding < %s | FileCheck --check-prefix=ENC %s
;
; if (...) { } blocks: a call only as the block's last instruction (it returns
; to the instruction after it, which then runs whatever the condition was), a
; goto in its 32-bit form, the else count only of a real else part.

declare void @use(ptr, i32)
declare void @more(i32, i32)

; was "if (r1 != 0) { r5 = r1; call use } else { r5 = 0 }": the else part ran
; after every call
; CHECK-LABEL: call_else:
; CHECK:       if (r1 == 0) goto
; CHECK:       r5 = r1
; CHECK-NEXT:  call use
; CHECK-NOT:   } else {
; CHECK:       call more
define void @call_else(ptr %p, i32 %n, i32 %k) optsize {
entry:
  %c = icmp ne i32 %n, 0
  br i1 %c, label %then, label %join
then:
  call void @use(ptr %p, i32 %n)
  br label %join
join:
  %r = phi i32 [ %n, %then ], [ 0, %entry ]
  %b = and i32 %r, 255
  %s = add i32 %b, %k
  %t = mul i32 %s, %k
  call void @more(i32 %t, i32 %s)
  call void @more(i32 %s, i32 %t)
  ret void
}

; was "if (r1 != 0) { r6 = r0; r1 = r5; call use; [r6+0] = r4 }"
; CHECK-LABEL: call_mid:
; CHECK:       if (r1 == 0) goto
; CHECK:       call use
; CHECK-NEXT:  [r6+0] = r4
; CHECK-NOT:   }
; CHECK:       {pc, r6-r4} = [sp++]
define i32 @call_mid(ptr %p, i32 %n, i32 %k) {
entry:
  %c = icmp ne i32 %n, 0
  br i1 %c, label %then, label %join
then:
  call void @use(ptr %p, i32 %n)
  store i32 %k, ptr %p
  br label %join
join:
  %s = add i32 %n, %k
  ret i32 %s
}

; A then part leaving with a goto over a long block: no else part (the opener's
; second word 0x4000: two instructions, none else); the goto is 32-bit (0xea).
; CHECK-LABEL: long_else:
; CHECK:       if (r1 != 5) {
; CHECK-NEXT:  r4 = [r0+0]
; CHECK-NEXT:  goto .LBB2_3
; CHECK-NEXT:  }
; CHECK:       r2 *= r2
; ENC-LABEL:   long_else:
; ENC:         if (r1 != 5) { ; encoding: [0xb1,0xe8,0x05,0x40]
; ENC:         goto .LBB2_3 ; encoding: [0xc0'A',0xea'A',A,A]
define void @long_else(ptr %p, i32 %n, i32 %k) optsize {
entry:
  %c = icmp ne i32 %n, 5
  br i1 %c, label %short, label %long
short:
  %v = load volatile i32, ptr %p
  br label %join
long:
  %a = mul i32 %k, %k
  %b = lshr i32 %a, 16
  %d = add i32 %b, -1023
  %e = mul i32 %d, %a
  %f = lshr i32 %e, 16
  store volatile i32 %f, ptr %p
  br label %join
join:
  %r = phi i32 [ %v, %short ], [ %f, %long ]
  %s = add i32 %r, %n
  call void @more(i32 %s, i32 %r)
  call void @more(i32 %r, i32 %s)
  ret void
}
