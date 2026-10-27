//===-- PI32V2FreezeReturns.cpp - A missing return is a value -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A C function that falls off its end without a return gives its caller undef
// (ret undef, or an undef incoming value of the phi it returns), and a branch
// on undef is undefined behaviour: the optimizer deletes both successors.
// JieLi's compiler (LLVM 4) left such code alone, and the AC79 SDK depends on
// it (wf_mem_init has no return when CONFIG_WIFI_USE_TLSF_MEM is 0, and the
// Wi-Fi driver's probe tests that status: without this, LTO removes the
// driver). Each such undef becomes a frozen one, an unknown but fixed value,
// before interprocedural constant propagation sees it.
//
//===----------------------------------------------------------------------===//

#include "PI32V2FreezeReturns.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

PreservedAnalyses PI32V2FreezeReturnsPass::run(Function &F,
                                               FunctionAnalysisManager &) {
  if (F.getReturnType()->isVoidTy())
    return PreservedAnalyses::all();
  bool Changed = false;
  for (BasicBlock &BB : F) {
    auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator());
    if (!Ret || !Ret->getReturnValue())
      continue;
    Value *V = Ret->getReturnValue();
    if (isa<UndefValue>(V) && !isa<PoisonValue>(V)) {
      Ret->setOperand(0, new FreezeInst(V, "ret.undef", Ret->getIterator()));
      Changed = true;
      continue;
    }
    auto *Phi = dyn_cast<PHINode>(V);
    if (!Phi)
      continue;
    for (unsigned I = 0; I < Phi->getNumIncomingValues(); ++I) {
      Value *In = Phi->getIncomingValue(I);
      if (!isa<UndefValue>(In) || isa<PoisonValue>(In))
        continue;
      Instruction *T = Phi->getIncomingBlock(I)->getTerminator();
      Phi->setIncomingValue(I, new FreezeInst(In, "ret.undef", T->getIterator()));
      Changed = true;
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
