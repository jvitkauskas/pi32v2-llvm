# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj %s -o %t.o
# RUN: llvm-objdump -d --mcpu=r3 %t.o | FileCheck %s

# llvm-objdump names branch targets: an offset is in bytes from the end of the
# instruction, whatever its length and wherever the offset is among the
# operands. The bytes pin one form each, where the assembler would pick others.
# JieLi's objdump gives the same targets for these (ifs2ns, which it does not
# annotate, by the same rule).

	.text
# Registers have no target.
# CHECK:      <start>:
# CHECK-NEXT: goto r6{{$}}
# CHECK-NEXT: call r6{{$}}
start:
	.byte 0xd6,0x00
	.byte 0xc6,0x00
	.space 28, 0

# CHECK: <mid>:
mid:
# CHECK: goto 6 <mid+0x8>
	.byte 0x04,0x83 ; goto.s
# CHECK: goto -6 <start+0x1e>
	.byte 0xf7,0x9d ; goto.s
# CHECK: goto 40 <mid+0x30>
	.byte 0xc0,0xea,0x14,0x00 ; goto.l
# CHECK: call 6 <mid+0x10>
	.byte 0x01,0x83 ; call.s
# CHECK: call -6 <mid+0x8>
	.byte 0xbf,0xea,0xfd,0xff ; call.l
# CHECK: call 6 <mid+0x1a>
	.byte 0x80,0xff,0x06,0x00,0x00,0x00 ; call.xl
# CHECK: gotoss -6 <mid+0x14>
	.byte 0x80,0xff,0xfb,0xff,0xff,0xff ; gotoss.xl
# CHECK: if (r0 == 0) goto 6 <mid+0x22>
	.byte 0x00,0x43 ; beqz
# CHECK: if (r0 != 0) goto -6 <mid+0x18>
	.byte 0xf0,0x5d ; bnez
# CHECK: if (--r0 != 0) goto -6 <mid+0x1c>
	.byte 0x00,0xea,0xfd,0xff ; dbnz.r
# CHECK: if (r0 == r0) goto 6 <mid+0x2c>
	.byte 0x00,0xe8,0x03,0x00 ; beq.rr
# CHECK: if (r0 == 0) goto 6 <mid+0x30>
	.byte 0x00,0xf8,0x03,0x00 ; beq.ri
# CHECK: if ((r0 & 1) == 0) goto -6 <mid+0x28>
	.byte 0x50,0xe8,0xfd,0x01 ; beq.and.ri
# CHECK: if ((r0 & r0) == 0) goto 6 <mid+0x38>
	.byte 0x00,0xfa,0x03,0x00 ; beq.and.rr
# CHECK: if (r0 == 0) goto -6 <mid+0x32>
	.byte 0x00,0xff,0x00,0x00,0xfd,0xff ; beq.ri.l
# CHECK: if (r0 == r0) goto 6 <mid+0x44>
	.byte 0x40,0xff,0x00,0x00,0x03,0x00 ; beq.rr.l
# CHECK: ifeq goto 6 <mid+0x48>
	.byte 0x40,0xe8,0x03,0x00 ; bc.eq
# CHECK: iff (r0 == r0) goto 6 <mid+0x4c>
	.byte 0x00,0xe8,0x03,0x08 ; bfeq.rr
# CHECK: iff (r0 == r0) goto -6 <mid+0x46>
	.byte 0x40,0xff,0x80,0x00,0xfd,0xff ; bfeq.rr.l
# CHECK: ifs2ns goto 6 (c) <mid+0x56>
	.byte 0x50,0xe8,0x03,0x04 ; ifs2ns.c
	.space 64, 0
