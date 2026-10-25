//===-- PI32V2InstrFormats.td - PI32V2 instruction formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An instruction is one, two or three 16-bit words. Inst holds them with the
// first word in the high bits; each word is stored little-endian.
//
//===----------------------------------------------------------------------===//

class PI32V2InstBase<int size, dag outs, dag ins, string asm>
    : Instruction {
  let Namespace = "PI32V2";
  let Size = size;
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asm;
  let DecoderNamespace = "PI32V2";
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
}

class PI32V2Inst16<dag outs, dag ins, string asm>
    : PI32V2InstBase<2, outs, ins, asm> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;
}

class PI32V2Inst32<dag outs, dag ins, string asm>
    : PI32V2InstBase<4, outs, ins, asm> {
  field bits<32> Inst;
  field bits<32> SoftFail = 0;
}

class PI32V2Inst48<dag outs, dag ins, string asm>
    : PI32V2InstBase<6, outs, ins, asm> {
  field bits<48> Inst;
  field bits<48> SoftFail = 0;
}
