//===-- PI32V2.h - Top-level interface for PI32V2 ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2_H

#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class PassRegistry;
class PI32V2TargetMachine;

FunctionPass *createPI32V2ISelDag(PI32V2TargetMachine &TM,
                                  CodeGenOptLevel OptLevel);
FunctionPass *createPI32V2IfBlocksPass();
FunctionPass *createPI32V2Div64PreparePass();
FunctionPass *createPI32V2AlignAllocasPass();

namespace PI32V2 {
// An `if (...) {` block opener (PI32V2IfBlocks.cpp).
bool isIfOpener(unsigned Opc);
// The block opener with a compare-and-branch's condition and operands.
unsigned openerForBranch(unsigned Br);
// A select's condition: an ISD::CondCode, SelectFloat added for a float
// compare (PI32V2ISelLowering.cpp); its compare-and-branch, the
// compare-with-constant form of one.
constexpr unsigned SelectFloat = 0x100;
unsigned branchForCond(unsigned Cond);
unsigned branchWithImm(unsigned Br);
} // namespace PI32V2
void initializePI32V2AsmPrinterPass(PassRegistry &);
void initializePI32V2DAGToDAGISelLegacyPass(PassRegistry &);
} // namespace llvm

#endif
