# RUN: llvm-mc -triple=pi32v2 -mcpu=ac791n -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s
# RUN: llvm-mc -triple=pi32v2 -mcpu=ac791n --show-encoding %s 2>/dev/null | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=pi32v2 -mcpu=r3 -filetype=obj -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=R3 --allow-empty
# RUN: not llvm-mc -triple=pi32v2 -mcpu=ac791n -filetype=obj -o /dev/null --fatal-warnings %s 2>&1 | FileCheck %s --check-prefix=FATAL
# The forms the AC791N's hardware does not run as their syntax says (pi32v2-isa's table, key hw: found by running
# them on the chip): with that chip as the CPU the assembler warns, and still assembles them. No other CPU warns.

# R3-NOT: warning
# FATAL: error: popcnt.rr does nothing on the AC791N

# ENC: r0 = popcnt(r1) ; encoding: [0x70,0xe0,0xc0,0x01]
# CHECK: [[#@LINE+1]]:2: warning: popcnt.rr does nothing on the AC791N
	r0 = popcnt(r1)
# CHECK: [[#@LINE+1]]:2: warning: popcnt.r_pair runs as mov.half on the AC791N
	r0 = popcnt(r1_r0)
# CHECK: [[#@LINE+1]]:2: warning: madd.mem2.hh.i.u resets the chip on the AC791N
	r1_r0 += h[r0 ++= 0]*h[r0 ++= 0] (u)
# CHECK: [[#@LINE+1]]:2: warning: copex.ppi gives unpredictable results on the AC791N
	r1_r0 = copex(r1_r0) (0)
# CHECK: [[#@LINE+1]]:2: warning: sat8.u.rr runs as sat16.u.rr on the AC791N
	r0 = sat8(r0) (u)
# An ordinary instruction gives none.
# CHECK-NOT: warning
	r0 = r1 + r2
	rts
