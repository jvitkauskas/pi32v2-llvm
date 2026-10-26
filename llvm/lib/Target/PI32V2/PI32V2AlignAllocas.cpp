//===-- PI32V2AlignAllocas.cpp - Locals aligned beyond the stack's --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The stack is word-aligned and the frame is not realigned (no frame
// pointer), so a local that needs more (__attribute__((aligned(16))), say)
// gets room for its alignment and its address is rounded up in that room, as
// JieLi's compiler does it (r0 = sp + 15; r0 = r0 & 0xFFFFFFF0).
//
//===----------------------------------------------------------------------===//

#include "PI32V2.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace {
class PI32V2AlignAllocas : public FunctionPass {
public:
  static char ID;
  PI32V2AlignAllocas() : FunctionPass(ID) {}
  StringRef getPassName() const override {
    return "PI32V2 locals aligned beyond the stack's alignment";
  }
  bool runOnFunction(Function &F) override;
};
} // namespace

char PI32V2AlignAllocas::ID = 0;

FunctionPass *llvm::createPI32V2AlignAllocasPass() {
  return new PI32V2AlignAllocas();
}

bool PI32V2AlignAllocas::runOnFunction(Function &F) {
  const DataLayout &DL = F.getDataLayout();
  const Align Stack(4);
  SmallVector<AllocaInst *, 4> Work;
  for (Instruction &I : instructions(F))
    if (auto *AI = dyn_cast<AllocaInst>(&I))
      if (AI->getAlign() > Stack && AI->getAllocationSize(DL))
        Work.push_back(AI);
  for (AllocaInst *AI : Work) {
    uint64_t Size = AI->getAllocationSize(DL)->getFixedValue();
    uint64_t A = AI->getAlign().value();
    IRBuilder<> B(AI);
    AllocaInst *Room = B.CreateAlloca(
        ArrayType::get(B.getInt8Ty(), Size + A - Stack.value()), nullptr);
    Room->setAlignment(Stack);
    Value *P = B.CreateIntrinsic(
        Intrinsic::ptrmask, {AI->getType(), B.getInt32Ty()},
        {B.CreateConstInBoundsGEP1_32(B.getInt8Ty(), Room, A - 1),
         B.getInt32(uint32_t(~(A - 1)))});
    // Lifetime markers name the alloca itself.
    for (Use &U : make_early_inc_range(AI->uses()))
      if (auto *II = dyn_cast<IntrinsicInst>(U.getUser()))
        if (II->isLifetimeStartOrEnd())
          U.set(Room);
    P->takeName(AI);
    Room->setName(P->getName() + ".room");
    AI->replaceAllUsesWith(P);
    AI->eraseFromParent();
  }
  return !Work.empty();
}
