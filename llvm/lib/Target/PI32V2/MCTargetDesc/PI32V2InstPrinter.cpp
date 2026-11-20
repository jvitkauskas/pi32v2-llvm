//===-- PI32V2InstPrinter.cpp - PI32V2 instruction printer ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints instructions as JieLi's objdump does (the syntax of the pi32v2-isa
// instruction table).
//
//===----------------------------------------------------------------------===//

#include "PI32V2InstPrinter.h"
#include "PI32V2BaseInfo.h"
#include "PI32V2MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "PI32V2GenAsmWriter.inc"

void PI32V2InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void PI32V2InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                  StringRef Annot, const MCSubtargetInfo &STI,
                                  raw_ostream &O) {
  O << '\t';
  // A rep block parsed from assembly has its body's length as an expression
  // (its end minus its start): written as a block, `rep r1 {`, the length is
  // the block's own, which is how it is parsed again.
  if ((MI->getOpcode() == PI32V2::REP || MI->getOpcode() == PI32V2::REP_I) &&
      MI->getOperand(0).isExpr()) {
    O << "rep ";
    if (MI->getOpcode() == PI32V2::REP)
      printOperand(MI, 1, O);
    else
      printOpuimm_5_1_1_N_F(MI, 1, O);
    O << " {";
    printAnnotation(O, Annot);
    return;
  }
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void PI32V2InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNo);
  if (MO.isReg()) {
    printRegName(O, MO.getReg());
    return;
  }
  if (MO.isImm()) {
    O << MO.getImm();
    return;
  }
  MAI.printExpr(O, *MO.getExpr());
}

#define PI32V2_GEN_PRINTERS
#include "PI32V2OperandsGen.inc"
