//===-- PI32V2ISelLowering.h - PI32V2 DAG lowering --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_PI32V2_PI32V2ISELLOWERING_H
#define LLVM_LIB_TARGET_PI32V2_PI32V2ISELLOWERING_H

#include "PI32V2SelectionDAGInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/Instructions.h"

namespace llvm {
class PI32V2Subtarget;

class PI32V2TargetLowering : public TargetLowering {
  const PI32V2Subtarget &Subtarget;

public:
  // A global's address plus a constant stays an add (lowerAddress).
  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override {
    return false;
  }
  // No overflow nodes for an add or compare: they would come back as a
  // 0/1 value in a register, not a branch.
  bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                            bool MathUsed) const override {
    return false;
  }
  // Post-increment accesses ([b++=n], n a multiple of the access size).
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;
  // [r+n] (n a multiple of the access size, 9 bits of them), [r+r], and
  // [r+r<<1] and [r+r<<2] for halfwords and words.
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS, Instruction *I = nullptr) const override;
  // only the modes the instructions have (the loop optimizer's cost model)
  bool isExactAddressingMode(const DataLayout &DL, const AddrMode &AM,
                             Type *Ty) const;
  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;
  // clz is one instruction, defined for 0 (32): no test around it.
  bool isCheapToSpeculateCtlz(Type *Ty) const override { return true; }
  bool isCheapToSpeculateCttz(Type *Ty) const override { return true; }
  // The divider is one instruction: optimizing for size, a division by a
  // constant stays a division (as JieLi's compiler makes it), else it is the
  // faster multiply sequence.
  bool isIntDivCheap(EVT VT, AttributeList Attr) const override {
    return Attr.hasFnAttr(Attribute::OptimizeForSize) ||
           Attr.hasFnAttr(Attribute::MinSize);
  }
  // Inline memcpy/memset accesses are as wide as both ends' alignment allows
  // (no misaligned words); a copy that would take too many becomes a call.
  EVT getOptimalMemOpType(LLVMContext &Context, const MemOp &Op,
                          const AttributeList &FuncAttributes) const override;
  // A register pair (an i64 node of the pair multiply) counts as two general
  // registers for the scheduler's register pressure.
  std::pair<const TargetRegisterClass *, uint8_t>
  findRepresentativeClass(const TargetRegisterInfo *TRI,
                          MVT VT) const override;
  // Tail calls (LowerCall): library calls too, when their result is what the
  // function returns.
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override {
    return CI->isTailCall();
  }
  bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;
  // A float constant is an integer constant (li).
  bool isFPImmLegal(const APFloat &Imm, EVT VT,
                    bool ForCodeSize) const override {
    return VT == MVT::f32;
  }
  PI32V2TargetLowering(const TargetMachine &TM, const PI32V2Subtarget &STI);

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  // 64-bit division and remainder by a divisor that fits in 32 bits.
  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  unsigned getNumRegisters(
      LLVMContext &Context, EVT VT,
      std::optional<MVT> RegisterVT = std::nullopt) const override;
  bool splitValueIntoRegisterParts(
      SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
      unsigned NumParts, MVT PartVT,
      std::optional<CallingConv::ID> CallConv) const override;

  SDValue joinRegisterPartsIntoValue(
      SelectionDAG &DAG, const SDLoc &DL, const SDValue *Parts,
      unsigned NumParts, MVT PartVT, EVT ValueVT,
      std::optional<CallingConv::ID> CallConv) const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;

  // No vector registers: a vector compare (from IR vectorized elsewhere, or a
  // struct copy SROA made a vector) is split into word compares, each giving
  // a word.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override {
    return VT.isVector() ? VT.changeVectorElementTypeToInteger() : MVT::i32;
  }

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

private:
  SDValue lowerCopyToPair(SDValue Op, SelectionDAG &DAG) const;
  void expandCopyFromPair(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const;
  SDValue lowerAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSelectCC(SDValue Op, SelectionDAG &DAG) const;
};
} // namespace llvm

#endif
