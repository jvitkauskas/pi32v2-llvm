//===-- PI32V2TargetTransformInfo.h - PI32V2 cost model ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What constants cost: a constant past 16 bits is a 6-byte li, so constant
// hoisting keeps one in a register and reaches its neighbours as offsets
// (register addresses such as 0x11C00 and 0x11C08 become [r+0] and [r+8], as
// JieLi's compiler makes them).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2TARGETTRANSFORMINFO_H

#include "MCTargetDesc/PI32V2BaseInfo.h"
#include "PI32V2Subtarget.h"
#include "PI32V2TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
class PI32V2TTIImpl final : public BasicTTIImplBase<PI32V2TTIImpl> {
  using BaseT = BasicTTIImplBase<PI32V2TTIImpl>;
  using TTI = TargetTransformInfo;
  friend BaseT;

  const PI32V2Subtarget *ST;
  const PI32V2TargetLowering *TLI;

  const PI32V2Subtarget *getST() const { return ST; }
  const PI32V2TargetLowering *getTLI() const { return TLI; }

public:
  explicit PI32V2TTIImpl(const PI32V2TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  // A call here is a 2- or 4-byte instruction and a push of rets, so
  // optimizing for size, a function is inlined only when that is about free
  // (LLVM's -Os threshold, 50, is about 10 instructions; this makes it 10).
  // (The hook's unsigned result is added to the inliner's int threshold
  // before its bonuses; -40 wraps to that subtraction, as it would in C++20.)
  unsigned adjustInliningThreshold(const CallBase *CB) const override {
    const Function *Caller = CB->getCaller();
    bool Size = Caller && Caller->hasOptSize() && !Caller->hasMinSize();
    return Size ? static_cast<unsigned>(-40) : 0;
  }

  // What inlining saves by removing a call: little here (a 2- or 4-byte call),
  // LLVM's default (25, five instructions) inlines helpers like a GPIO write
  // into every caller. 5 is the best for m-bootloader at -Oz (LTO).
  unsigned getInlineCallPenalty(const Function *F, const CallBase &Call,
                                unsigned DefaultCallPenalty) const override {
    return F && F->hasOptSize() ? 5 : DefaultCallPenalty;
  }

  // Loads and stores with the base stepped after them ([b++=n]).
  TTI::AddressingModeKind
  getPreferredAddressingMode(const Loop *L, ScalarEvolution *SE) const override {
    return TTI::AMK_PostIndexed;
  }

  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) const override {
    unsigned Bits = Ty->getPrimitiveSizeInBits();
    if (Bits == 0 || Bits > 32 || Imm.isZero())
      return TTI::TCC_Free;
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;
    return 2 * TTI::TCC_Basic; // li.u32
  }

  // Only an address is worth keeping in a register for its neighbours (a
  // register address in a load or store, or a pointer made from it); any other
  // constant goes with its instruction, or is loaded where it is used, so the
  // hoisting pass does not rebase it on an unrelated one.
  InstructionCost
  getIntImmCostInst(unsigned Opc, unsigned Idx, const APInt &Imm, Type *Ty,
                    TTI::TargetCostKind CostKind,
                    Instruction *Inst = nullptr) const override {
    int64_t V = Imm.getBitWidth() <= 64 ? Imm.getSExtValue() : 0;
    switch (Opc) {
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::IntToPtr:
    case Instruction::GetElementPtr:
      return getIntImmCost(Imm, Ty, CostKind);
    case Instruction::Add:
    case Instruction::Sub:
      // an offset past what the add takes, often an address (0x50000 +
      // p * 64 + k): neighbours become the same base plus k
      if (isInt<14>(V) || PI32V2::modImmRawFor(uint32_t(V)).has_value())
        return TTI::TCC_Free;
      return getIntImmCost(Imm, Ty, CostKind);
    default:
      return TTI::TCC_Free;
    }
  }

  InstructionCost
  getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                      Type *Ty, TTI::TargetCostKind CostKind) const override {
    return getIntImmCost(Imm, Ty, CostKind);
  }
};
} // namespace llvm

#endif
