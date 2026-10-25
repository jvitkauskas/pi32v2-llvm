# RUN: llvm-mc -triple=pi32v2 --show-encoding %s | FileCheck %s
# Comments inside and after an instruction's text.

# CHECK: r1 = r1 + 4 ; encoding: [0x19,0x84]
	r1 = /* inside */ r1 + 4	/* after */
# CHECK: rts ; encoding: [0x80,0x00]
	rts	// after
# CHECK: nop ; encoding: [0x00,0x00]
	nop ; after
