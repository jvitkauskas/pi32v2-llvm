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

  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) const override {
    unsigned Bits = Ty->getPrimitiveSizeInBits();
    if (Bits == 0 || Bits > 32 || Imm.isZero())
      return TTI::TCC_Free;
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;
    return 2 * TTI::TCC_Basic; // li.u32
  }

  // A constant an instruction holds itself costs nothing.
  InstructionCost
  getIntImmCostInst(unsigned Opc, unsigned Idx, const APInt &Imm, Type *Ty,
                    TTI::TargetCostKind CostKind,
                    Instruction *Inst = nullptr) const override {
    if (Imm.getBitWidth() <= 32) {
      int64_t V = Imm.getSExtValue();
      bool Mod = PI32V2::modImmRawFor(uint32_t(V)).has_value();
      switch (Opc) {
      case Instruction::Add:
      case Instruction::Sub:
        if (Idx == 1 && (isInt<14>(V) || Mod))
          return TTI::TCC_Free;
        break;
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor:
      case Instruction::Mul:
        if (Idx == 1 && Mod)
          return TTI::TCC_Free;
        break;
      case Instruction::ICmp:
        if (Idx == 1 && isInt<12>(V))
          return TTI::TCC_Free;
        break;
      default:
        break;
      }
    }
    return getIntImmCost(Imm, Ty, CostKind);
  }

  InstructionCost
  getIntImmCostIntrin(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                      Type *Ty, TTI::TargetCostKind CostKind) const override {
    return getIntImmCost(Imm, Ty, CostKind);
  }
};
} // namespace llvm

#endif
