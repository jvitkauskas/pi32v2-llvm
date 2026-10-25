//===-- PI32V2CallingConv.td - PI32V2 calling convention ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JieLi's C convention (pi32v2-isa docs/ABI.md sections 4 and 5): arguments
// are a stream of four-byte words, r0..r3 and then the stack, a 64-bit value
// two consecutive words, low first; results in r0 (and r1); r4..r15 are
// preserved, and the return address in rets.
//
//===----------------------------------------------------------------------===//

def CC_PI32V2 : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1, R2, R3]>>,
  CCIfType<[i32], CCAssignToStack<4, 4>>
]>;

def RetCC_PI32V2 : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,
  CCIfType<[f32], CCBitConvertToType<i32>>,
  CCIfType<[i32], CCAssignToReg<[R0, R1]>>
]>;

def CSR_PI32V2 : CalleeSavedRegs<(add RETS, (sequence "R%u", 15, 4))>;
// An interrupt handler saves every register it changes (psr and reti are
// saved by its prologue, PI32V2FrameLowering).
def CSR_PI32V2_Interrupt : CalleeSavedRegs<(add RETS, (sequence "R%u", 15, 0))>;
