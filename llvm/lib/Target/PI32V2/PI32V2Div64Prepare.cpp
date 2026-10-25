//===-- PI32V2Div64Prepare.cpp - 64-bit divisions by 32-bit divisors ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A 64-bit division (or remainder) whose divisor fits in 32 bits is the
// div.u.ppr / div.s.ppr instruction (PI32V2TargetLowering::ReplaceNodeResults),
// but instruction selection sees one basic block: a divisor extended from 32
// bits in another block looks like any 64-bit value, and the division becomes
// a __udivdi3 call (code that links no runtime fails). Value tracking sees the
// whole function: where it proves the divisor fits, the extension is made
// again right before the division.
//
//===----------------------------------------------------------------------===//

#include "PI32V2.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

namespace {
class PI32V2Div64Prepare : public FunctionPass {
public:
  static char ID;
  PI32V2Div64Prepare() : FunctionPass(ID) {}
  StringRef getPassName() const override {
    return "PI32V2 64-bit division by a 32-bit divisor";
  }
  bool runOnFunction(Function &F) override;
};
} // namespace

char PI32V2Div64Prepare::ID = 0;

FunctionPass *llvm::createPI32V2Div64PreparePass() {
  return new PI32V2Div64Prepare();
}

bool PI32V2Div64Prepare::runOnFunction(Function &F) {
  bool Changed = false;
  const DataLayout &DL = F.getDataLayout();
  for (BasicBlock &BB : F)
    for (Instruction &I : BB) {
      auto *BO = dyn_cast<BinaryOperator>(&I);
      if (!BO || !BO->getType()->isIntegerTy(64))
        continue;
      unsigned Opc = BO->getOpcode();
      bool Signed = Opc == Instruction::SDiv || Opc == Instruction::SRem;
      if (!Signed && Opc != Instruction::UDiv && Opc != Instruction::URem)
        continue;
      Value *D = BO->getOperand(1);
      if (isa<Constant>(D) || isa<ZExtInst>(D) || isa<SExtInst>(D))
        continue; // already plain to instruction selection
      bool Fits =
          Signed ? ComputeNumSignBits(D, DL, nullptr, BO) >= 33
                 : computeKnownBits(D, DL, nullptr, BO).countMinLeadingZeros() >= 32;
      if (!Fits)
        continue;
      IRBuilder<> B(BO);
      Value *T = B.CreateTrunc(D, B.getInt32Ty());
      Value *E = Signed ? B.CreateSExt(T, B.getInt64Ty())
                        : B.CreateZExt(T, B.getInt64Ty());
      BO->setOperand(1, E);
      Changed = true;
    }
  return Changed;
}
