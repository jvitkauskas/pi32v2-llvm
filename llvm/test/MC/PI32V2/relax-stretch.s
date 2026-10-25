# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj %s -o - | llvm-objdump -d - | FileCheck %s

# 200 calls grow to 4 bytes (g is out of the 2-byte call's reach) before a
# short forward branch: while relaxing, the branch's offset has moved by
# 400 bytes and its target's not yet. The branch keeps its 2-byte form.

	.text
	.globl f
f:
	.rept 200
	call g
	.endr
	if (r0 == 0) goto .L1
	r1 = 1
.L1:
	rts

# CHECK:      320: 00 41 {{.*}}if (r0 == 0) goto 2
# CHECK-NEXT: 322: 41 21 {{.*}}r1 = 1
# CHECK-NEXT: 324: 80 00 {{.*}}rts

# A call within reach of another function of the same section: 2 bytes.
	.globl k
k:
	rts
	.globl h
h:
	call k
	rts
# CHECK-LABEL: <h>:
# CHECK-NEXT: 328: 71 9e {{.*}}call -4
# CHECK-NEXT: {{[0-9a-f]+}}: 80 00 {{.*}}rts

	.space 1000
	.globl g
g:
	rts
