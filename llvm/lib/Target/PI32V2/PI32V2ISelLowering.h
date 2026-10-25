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

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

private:
  SDValue lowerAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerSelectCC(SDValue Op, SelectionDAG &DAG) const;
};
} // namespace llvm

#endif
